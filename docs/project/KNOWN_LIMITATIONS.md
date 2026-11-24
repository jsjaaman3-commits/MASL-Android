# Known Limitations — MASL 0.10.0

These are current implementation boundaries, not future optimizations hidden
behind a finished feature claim.

## Compiler front end and IR

- Semantic analysis implements a supported AST subset; it is not full MASL
  1.0.0 type-system conformance.
- Full dimensional algebra, general constant evaluation, broad implicit
  conversions and whole-hardware-graph elaboration are not implemented.
- Typed IR describes parts of the source model but does not yet provide the
  executable architecture graph consumed by the simulator.

## MASB 1.0 compiler and verifier

- MASB generation accepts the supported pure-function subset. Hardware,
  workload and experiment declarations are rejected with diagnostics rather
  than being silently erased.
- Record-field access, aggregate objects, indirect calls, event handlers,
  physical-unit expressions and simulation side effects are outside the current
  bytecode subset.
- MASB has no source map, debug-symbol section, import/linker model or signed
  authenticity mechanism. CRC-32 detects accidental corruption only.

## CPU simulator

- Input is a generated synthetic instruction stream, not x86-64, ARM64, RISC-V
  or another ISA binary.
- ROB/dependency behavior is an approximation. No architectural register
  renaming, physical register file, precise exception/replay, wrong-path
  execution, predictor structures, branch checkpointing, store-to-load
  forwarding, load/store queue, MSHRs or prefetcher is implemented.
- Branch misprediction applies a configured front-end fetch penalty; it does
  not flush/replay the ROB or model speculative side effects.
- L1I and L1D are distinct private tag caches. L2/LLC are shared tag models.
  No coherence protocol, MESI states, invalidations, snoops, false sharing,
  atomic ordering, dirty-line writeback, write buffer or cache-to-cache transfer.
- Cache latencies are fixed; contention at cache banks/ports is not modeled.
- DRAM is a simple channel availability queue with fixed latency/bandwidth.
  No DRAM bank/row/timing/refresh/rank/command scheduling.

## GPU simulator

- GPU code is not CUDA, PTX, SASS, OpenCL or a binary kernel. Each warp
  consumes a generated sequence of operation classes.
- CTA size is fixed at 256 threads in occupancy accounting. Register allocation
  assumes four bytes per register; architecture-specific allocation granularity
  and block limits are not configurable in this release.
- Warp issue is round-robin with a configurable number of issue slots per SM.
  There is no detailed scheduler partition, scoreboard bank, register bank,
  pipeline partition or architecture-specific dual-issue rules.
- Divergence is an aggregate lane-efficiency percentage, not branch-path
  execution, reconvergence or SIMT-stack simulation.
- Global memory coalescing collapses identical cache-line addresses. It does
  not model exact byte masks, sectors, per-lane request sizes, atomics or
  outstanding request queues.
- Shared-memory bank conflict is an analytical proxy. Broadcast, multicast,
  bank modes, swizzles and architecture-specific rules are excluded.
- No CUDA API, device runtime, real kernel execution, tensor-core format
  semantics, scheduling of multiple streams, HBM/GDDR timing or GPU ECC.

## Memory, interconnect, performance and power

- CPU and GPU run as separate modes, not as concurrent agents in one shared
  heterogeneous system.
- Host/device transfer cost is a serialized bandwidth-plus-latency estimate;
  no PCIe/NVLink protocol, DMA engine, copy/compute overlap, host memory pinning
  or queue arbitration.
- IPC, occupancy, bytes and nanoseconds are outputs of the configured model.
  They do not predict a real processor without calibration against hardware
  counters and a validated architecture-specific model.
- No measured silicon power, energy, thermal, voltage, frequency scaling or
  cooling model. The simulator must not claim to estimate RTX 5090 power or
  performance precisely.

## MASL language integration

- The simulator is callable from the C++ API, `maslsim` CLI and Android Studio
  native bridge. `.masl` hardware, queue, component, cache, workload and
  experiment declarations are not yet elaborated into the simulator.
- Android Studio currently uses its default architecture parameters and
  generated workloads. A source-driven model loader/editor and visual trace
  viewer are future work.

## Android and portability

- The Android source targets ARM64 and contains JNI integration. It has not
  been NDK-compiled or installed in this execution environment. The mobile
  bridge caps generated CPU/GPU work at four million to bound memory use on phones.
- No signed or installable APK is included in this delivery because Android SDK,
  NDK, Gradle and packaging tools were unavailable here. GitHub Actions build
  instructions are included but that workflow has not been run from this
  environment.
- Android app lifecycle, physical-device performance, memory pressure and
  native crash behavior remain to be validated on an actual Android phone.
- Windows, Linux and macOS are future native application packaging targets;
  the host Linux C++ build is not evidence of full desktop app support.

## Remaining engineering work

- Source-driven architecture elaboration and validation from MASL AST/Typed IR.
- Cycle-level trace and JSON/CSV export, visualizer, workload compare/experiment
  runner and profiling tools.
- More realistic CPU branch recovery, memory ordering/coherence and DRAM timing.
- Configurable GPU blocks/warps/resources, divergence paths, DMA and memory
  request queues.
- Fuzzing, property-based testing, calibrated architecture benchmarks and
  external conformance comparisons.
- NDK ARM64 build, APK inspection/signing, installation and on-device acceptance.
