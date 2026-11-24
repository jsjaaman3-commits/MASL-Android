# MASL Standard Simulation Library — Design Baseline 0.1

Status: Design baseline updated through Phase 09. Queue and Resource have
working C++ runtime APIs; other listed components remain design targets unless
an implementation status is explicitly recorded in the project state.

The standard library is divided into semantic primitives and domain templates.
A standard component is executable runtime code with documented state,
parameter validation, events, resource accounting, and tests. A type alias or
name-only placeholder does not count as an implementation.

## Core primitives

- Queue: typed bounded C++ runtime API implemented in Phase 09; FIFO,
  priority and producer-round-robin selection; Block/Reject/Drop policies;
  separately bounded pending submissions and bounded transition trace. It is
  not yet instantiated from MASL source declarations.
- Buffer: bounded storage with explicitly declared read/write port count and
  latency; no implicit queue semantics.
- Resource: finite-capacity C++ runtime API implemented in Phase 09; owner-bound
  leases, partial release, bounded FIFO waiters, one-shot grants and cancellation.
  It is not yet instantiated from MASL source declarations.
- Clock: rational frequency, exact edge generation.
- Counter: monotonic checked count and optional sampling.
- Barrier: finite participant count, generation ID, release event.
- Profiler: simulation-time intervals and counters, not host wall-clock data.

## Scheduling and interconnect

- Dispatcher: maps a request to a declared destination set using a policy.
- Arbiter: FIFO, round robin, priority, weighted round robin. Weighted round
  robin requires a positive integer weight per source and deterministic ties.
- Link: bandwidth, propagation latency, transfer occupancy, and finite buffer.
- Router: input/output ports, per-port buffers, routing function, arbitration,
  contention, and optional virtual channels only when explicitly configured.

## Pipelines and execution units

Pipeline templates model acceptance interval independently of service latency.
Execution-unit templates may add operation classes, per-op latency, issue
width, and per-cycle capacity. Scheduler models select ready work and account
for fairness/priority. Warp and lane templates describe SIMT grouping; they do
not assume an NVIDIA-specific behavior unless parameterized and sourced.

## Memory

- Register file: capacity in registers, bank/port limits, read/write timing,
  and conflict policy.
- Shared/local SRAM: byte capacity, banking, ports, latency, arbitration.
- Cache: capacity, line size, sets/ways consistency, replacement (LRU/FIFO/
  seeded random), hit/miss latency, MSHRs, ports, and write policy.
- Banked memory: bank mapping, per-bank queue, access latency and conflicts.
- Memory controller: channel/bank/row state, scheduling, queueing, read/write
  turnaround, refresh, and explicitly parameterized timing.
- DRAM: configurable command/timing model (ACT, READ, WRITE, PRE, REF) with
  row-buffer policy and timing constraints. Parameter profiles are external
  data, not hard-coded undocumented claims.

## GPU/CPU templates

The GPU package is intended to provide reusable GPU, GPC, TPC, SM, warp
scheduler, warp, lane, register-file, shared-memory, L1/L2, memory-controller,
and DRAM templates. A Mini GPU must be instantiable entirely from MASL source.
No specific commercial GPU is implied by the generic templates.

The CPU package may provide core, fetch/decode/execute/retire pipelines,
branch predictor abstractions, register file, cache hierarchy, coherent
interconnect hooks, and memory controller. Coherence protocols are deferred
until a complete state/event model and tests are specified.
