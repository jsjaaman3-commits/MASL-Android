# MASL Architecture Simulator 0.10.0 — Model and Contract

Status: implemented C++20 simulation engine; host Debug/Release test suites pass.

## 1. Purpose and execution contract

The 0.10 simulator executes bounded, deterministic *synthetic workloads* against
parameterized CPU or GPU architecture models. It is intended for comparative
experiments (for example, changing cache size, issue width, DRAM latency, warp
width or memory pattern), instruction/resource accounting and learning how
architecture parameters affect an abstract model.

It is not a model of one identified commercial processor. A result is valid
only relative to the exact configuration and assumptions below. The same
configuration and seed produce the same synthetic instruction stream and
counters. The engine does not read machine code, CUDA kernels, MASB bytecode or
hardware declarations from `.masl` source.

Public API:

- `SimulatorConfig`: CPU/GPU configuration, workload, cycle budget and dynamic
  instruction budget.
- `Workload`: instruction/thread counts, working-set size, operation mix,
  memory pattern and reproducibility seed.
- `CpuConfig` / `GpuConfig`: capacities, widths, frequencies, execution-unit
  limits, latencies and memory-system parameters.
- `SimulationResult`: completion status, simulated cycles, estimated time,
  operation counters, stall counters, cache/DRAM/link traffic and report text.
- `simulate_cpu`, `simulate_gpu`, `simulate`: bounded entry points.

Invalid configurations return `ok == false` with a diagnostic. Hard limits are
checked before large allocations or long event loops. A run that reaches
`maximum_cycles` or `maximum_dynamic_instructions` is an error, not a partial
success.

## 2. Determinism

The workload generator uses `std::mt19937_64` seeded from `Workload::seed`.
CPU cores use deterministic per-core seeds derived from that value and the core
index. GPU warp instructions are generated in deterministic SM/warp issue order.
No wall clock, host scheduling, thread race or global random state is used.
A fixed seed and identical configuration must yield identical architectural
counters. Estimated time is calculated from simulated cycle counts and the
configured model clock; it is not a measurement of host execution time.

## 3. CPU model

### 3.1 Front end and instruction window

Each configured core owns an independent synthetic instruction stream and a
bounded reorder buffer (ROB). The model applies configurable:

- instruction fetch width;
- issue width;
- retirement width;
- ROB entry capacity;
- integer, floating-point and load/store execution-unit counts;
- integer and floating-point operation latencies.

A dependency scoreboard tracks synthetic producer sequence numbers and their
completion cycles. Instructions may issue out of program order when their
synthetic inputs are ready and a matching functional unit is available. The ROB
retires only completed instructions from its head, subject to the configured
retire width. This captures a bounded out-of-order issue/ordered-retirement
approximation; it is not a complete microarchitectural reorder engine.

### 3.2 Branches

The workload generator inserts branch operations according to the configured
percentage. A configured misprediction probability produces a deterministic
misprediction count and blocks new fetch for the configured penalty. There is no
branch predictor table, speculative rename map, wrong-path instruction
execution, checkpointing or precise misprediction flush/replay. Therefore the
branch model is a front-end penalty approximation, not a predictor simulator.

### 3.3 Instruction and data caches

Each CPU core has separate private L1I and L1D caches. L1I traffic is generated
from a synthetic 4-byte instruction address stream. Data loads/stores generate
addresses in the configured working set. L1I and L1D share the next levels:

`per-core L1I/L1D -> shared L2 -> shared LLC -> DRAM controller`

Each cache uses configurable capacity, line size, associativity and fixed hit
latency. It is a set-associative tag store with LRU replacement at the line
level. A miss is looked up in the next level; a returned line is installed in
all traversed levels. The hierarchy is treated as inclusive for the purpose of
this model. Stores use the same simplified cache-line access/fill path as loads.

The model does not implement dirty-line tracking, write-through/write-back
policy, write combining, store buffers, MSHR limits, prefetchers, coherence,
false sharing, snoop traffic, cache inclusivity evictions or atomic operations.
L2 and LLC are shared by the simulated cores, but there is no coherence protocol
between private caches.

### 3.4 DRAM controller abstraction

The memory controller has a configurable number of independent channels,
bytes-per-cycle transfer bandwidth per channel, fixed access latency and cache
line size. A line maps deterministically to one channel. Each channel serializes
its transfer service; a request waits until that channel is available, pays the
transfer cycles, then pays the configured access latency.

This is a channel queue abstraction. It has no DRAM row-buffer state, bank
parallelism/timing, command bus, read/write turnarounds, refresh, rank, burst,
CAS/RCD/RP/RFC timing, ECC or memory-controller scheduling policy.

## 4. GPU model

### 4.1 Thread, block, warp and SM mapping

The configured thread count is rounded up into warps using `warp_width`. The
current implementation uses a fixed 256-thread block for occupancy accounting.
Warps are assigned to SMs deterministically by warp index modulo SM count.
Per-SM resident warps are bounded by the minimum of:

- the 16-block model limit;
- register-file capacity divided by estimated per-block register use;
- shared-memory capacity divided by configured per-block shared memory;
- the maximum resident-warp limit.

The block register estimate treats each configured register as four bytes.
The model does not emulate actual PTX/SASS allocation granularity or architecture-
specific block scheduling rules.

### 4.2 Warp scheduler and instruction issue

Each SM has a round-robin warp scheduler and a configurable number of issue
slots per cycle. A warp can issue when it is active, has remaining instructions
and its previous operation's ready cycle has passed. Operation classes include
integer, floating point, global load/store, branch, tensor and shared-memory
operations, each with a configured latency. This is a warp-level throughput
model, not a GPU ISA interpreter.

### 4.3 Lane utilization and divergence

The workload has a configurable synthetic divergence percentage. For each
issued warp instruction, active lanes are rounded from
`warp_width * (100 - divergence_percent) / 100`; zero divergence keeps every
lane active and 100-percent synthetic divergence leaves no active-lane slots.
The result is an aggregate lane-efficiency proxy. It is not control-flow graph
reconvergence, SIMT stack execution or per-lane predication.

### 4.4 Global memory and coalescing

Each memory instruction generates one address per lane. The available patterns
are:

- `coalesced`: adjacent lane addresses;
- `strided`: lane addresses separated by a larger stride;
- `random`: deterministic random cache-line addresses.

Duplicate cache-line addresses are collapsed into one memory transaction. The
model then passes unique line addresses through per-SM L1D, shared L2, shared
LLC and the DRAM channel controller. It does not model exact byte masks, sectors,
partial-line accesses, per-lane data widths, atomic serialization, page
translation or detailed outstanding request queues.

### 4.5 Shared-memory bank conflicts

The shared-memory operation uses the configured number of banks and bank width.
For a lane's synthetic byte address, bank index is
`(address_bytes / bank_width_bytes) % bank_count`. Coalesced addresses are
`lane * 4`; strided addresses are `lane * 8`; random addresses select a bank
with the fixed workload RNG. The maximum lane count per bank is used as a
conflict/serialization proxy. Broadcast/multicast rules and architecture-
specific bank modes are not modelled.

### 4.6 Host/device link

The GPU result reports host-to-device and device-to-host transfer cycles from
configured bytes, link bandwidth and per-transfer latency. These costs are
reported separately from kernel cycles and included in estimated total elapsed
time. The transfer abstraction is serialized; it does not model DMA engines,
copy/compute overlap, pageable/pinned memory, PCIe packetization, NVLink lanes,
coherency or queue contention.

## 5. Workload parameters

`Workload` exposes:

- CPU dynamic instruction count;
- GPU thread count and instructions per thread;
- working-set byte size;
- host-to-device and device-to-host byte counts;
- memory, store, floating-point, branch, tensor and shared-memory operation
  percentages;
- dependency and divergence percentages;
- memory access pattern;
- deterministic seed.

Operation mix percentages are validated to remain within the supported range.
The generated sequence is synthetic. It does not represent any real compiler's
instruction mix unless an external study independently calibrates it.

## 6. Result semantics

- `cycles`: CPU completion cycles or GPU kernel cycles; GPU link transfer cycles
  are separate.
- `elapsed_nanoseconds()`: total model time from `cycles + link_transfer_cycles`
  divided by the configured engine clock.
- `instructions`: retired CPU instructions or issued GPU warp instructions.
  GPU instructions are warp-level, not multiplied by lane count.
- `memory_instructions`: generated CPU data-memory operations or GPU warp-level
  global-memory operations; CPU instruction-fetch accesses are counted
  separately.
- `memory_transactions`: unique cache-line requests entering L1 from CPU
  instruction fetches + CPU data (for CPU) or coalesced GPU global memory (for
  GPU). `instruction_cache_transactions` isolates CPU L1I accesses.
- `dram_transactions` / `dram_bytes`: requests and bytes that reach the memory
  controller after cache filtering.
- `ipc()`: engine instruction count divided by engine cycles. It is a model IPC,
  not the IPC of the host CPU executing the simulator.
- `effective_occupancy_percent_x100`: GPU active warp-cycle occupancy scaled by
  100; lane utilization is a separate ratio.

All counters are model counters. The host wall-clock runtime and measured
physical power are not reported.

## 7. CLI and Studio entry points

Host CLI:

```text
maslsim cpu [dynamic-instructions] [seed]
maslsim gpu [instructions-per-thread] [threads] [seed] [coalesced|strided|random]
```

Android MASL Studio calls the exact same native engine through
`masl_mobile_bridge.cpp` and JNI. In the UI, users can select CPU/GPU and bound
the generated workload size. The current app UI uses the default architecture
configuration and coalesced pattern; the public C++ API and host CLI expose more
configuration and access-pattern control.

## 8. Explicit exclusions and next integration boundary

This engine is not a real ISA emulator, RTL/gate-level simulator, CUDA/OpenCL
runtime, measured performance model or cycle-accurate clone of a specific CPU or
GPU. It does not run user-supplied native binaries. CPU and GPU are separate
simulation modes; there is no concurrent heterogeneous system model or shared
virtual memory.

The MASL parser/semantic analyzer currently does not lower `.masl` hardware,
queue, cache, component, workload or experiment declarations into this engine.
The 0.10 Android UI creates synthetic workloads through native parameters. A
source-driven elaborator, a typed executable architecture IR, a configurable
model schema, JSON/CSV trace export and visualization are required before a
complete MASL-language hardware model can be claimed.
