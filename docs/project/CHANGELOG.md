# Changelog

All notable changes are recorded here. The repository is pre-1.0; compatibility
is not guaranteed between minor development snapshots.

## [0.10.0] — 2026-11-23

### Added — deterministic CPU/GPU architecture simulator
- Multi-core synthetic CPU execution model with configurable fetch, issue and
  retire widths; bounded ROB; dependency scoreboard; functional-unit counts;
  execution latencies and branch front-end penalty.
- Separate private L1I/L1D caches, shared L2/LLC, set-associative LRU tag
  replacement, fixed hit latency and a configurable DRAM channel queue.
- GPU warp-level SIMT engine with SM scheduling, issue slots, occupancy limits
  from register/shared-memory/warp budgets, integer/FP/tensor/branch/shared
  operations and deterministic warp scheduling.
- Global-memory address-pattern generation and cache-line coalescing for
  coalesced, strided and random access; shared-memory bank-conflict proxy using
  configured bank count and bank width.
- Host/device transfer bandwidth-plus-latency accounting, `maslsim` CLI,
  portable `SimulatorConfig` API, and CPU/GPU controls in Android Studio.
- Model specification, known limitations, project state and test matrix updated.

### Android source integration
- Updated Android Gradle configuration to AGP 9.5.2, Gradle 9.9.2, NDK
  `30.0.16248370`, Android API 36 and ARM64-only output.
- Added/updated GitHub Actions workflow to build an ARM64 debug APK artifact.
- Android SDK/NDK/Gradle were unavailable in the authoring container, so APK
  packaging and physical-device acceptance are explicitly pending.

### Validation
- GCC 14.2 Linux Debug and Release builds use warnings-as-errors.
- CTest includes compiler/runtime regression cases, simulator API cases, mobile
  bridge cases and CLI smoke tests. Final count and exact run outputs are in the
  generated 0.10.0 delivery report.

### Explicit boundaries
- Workloads remain synthetic; no CPU/GPU ISA, PTX/SASS, CUDA, RTL or real-chip
  cycle-accurate execution.
- MASL source hardware/workload declarations are not yet lowered into the
  simulator; Studio invokes the same engine through built-in workload controls.
- No cache coherence, full DRAM timing, detailed branch recovery, GPU
  reconvergence, cycle trace export, measured power/thermal model or APK/device
  acceptance is claimed.

## [0.9.0] — 2026-11-19

### Added — Phase 09
- Platform-neutral typed `BoundedQueue<T>` runtime API with finite capacity,
  FIFO, stable priority and producer round-robin disciplines.
- Explicit Block/Reject/Drop overflow outcomes; blocked submissions are retained
  in a separately bounded FIFO and promoted deterministically when capacity
  becomes available.
- Queue statistics, occupancy high-watermark, invariant checks and bounded
  transition tracing.
- Ownership-aware Resource runtime with finite unit capacity, unique leases,
  partial release, strict FIFO blocked acquisitions, waiter tickets, one-shot
  grants, cancellation, utilization counters and bounded traces.
- Integration tests run queue and resource transitions from Phase 08 event
  handlers; C++ runtime APIs remain separate from MASL-source elaboration.
- Android-first portability policy and explicit future MASL App profile for
  general-purpose Android/Windows/Linux/macOS application development.

### Validation
- GCC 14.2 Debug: 33/33 CTest.
- GCC 14.2 Release: 33/33 CTest.
- Clang 17 Debug: 33/33 CTest.
- GCC ASan/UBSan: full 33/33 CTest matrix passed with leak detection and
  halt-on-error; Queue 26/26 and Resource 31/31 dedicated assertions passed.

### Explicit boundaries
- Queue/Resource are C++ runtime APIs and are not yet created from `.masl`
  declarations by a source-driven model elaborator.
- No Android NDK/device build, APK, MASL Studio, native desktop application
  backend, or general-purpose MASL App profile is included in 0.9.0.

## [0.7.0] — 2026-11-19

### Added — Phase 08
- Exact rational femtosecond time with normalization, checked arithmetic and
  comparison without cross-product overflow.
- Exact clock-period/edge calculations.
- Deterministic event calendar ordered by time, priority and insertion sequence.
- Event handlers, event-mode and cycle-mode runners, independent event/pending
  limits, schedule/dispatch trace records and exception diagnostics.

### Added — Phase 07
- Bounded stack virtual machine for verified MASB 1.0 functions.
- Scalar values, direct function calls, conditional branches, bounded loops,
  integer/floating arithmetic, comparisons, booleans, strings, assertions and
  deterministic traps.
- Per-function and global instruction budgets, call-depth, stack and string
  limits; runtime type checks and checked signed arithmetic.
- `masl verify` and `masl run` CLI commands.

### Added — Phase 06
- MASB 1.0 little-endian binary format with 32-byte header, three-section
  directory, 9-byte instructions, source-version metadata and CRC-32/IEEE.
- Bytecode serialization/loading, disassembly, structural/control-flow/type
  verifier, definite local initialization and recursion rejection.
- `maslc compile` and `maslc disasm` commands.
- Explicit rejection of unsupported hardware declarations so bytecode generation
  cannot silently discard model data.

### Corrected
- GCC 14 Release warning in bytecode file reading by using a size-checked
  `ifstream::read` path instead of `istreambuf_iterator`.
- Clang 17 `-Wdouble-promotion` by making VM numeric promotion explicit.

### Validation
- 31 CTest cases in GCC Debug, GCC Release, Clang 17 Debug and GCC ASan/UBSan.
- Dedicated suites: Bytecode 18 assertions, VM 17 assertions, Runtime 29
  assertions; all reported zero failures.
- See `PHASE_06_VALIDATION.md`, `PHASE_07_VALIDATION.md`, and
  `PHASE_08_VALIDATION.md`.

### Explicit boundaries
- MASB backend and VM execute pure functions only; hardware/workload declaration
  serialization and simulation operations remain future work.
- Phase 08 is a C++ kernel API not yet connected to MASL source or the VM.
- No Android ARM64 target or MASL Studio APK is included.

## [0.4.0] — 2026-11-19

### Added
- Typed IR schema 1.0, AST-to-IR lowering and structural verification.
- `maslc ir <file>` with type/source-span preservation.

### Corrected
- Component-local queue/resource capacity expressions retain their semantic
  types and enforce integer typing.

## [0.3.0] — 2026-11-19

### Added
- Rebuilt supported-subset semantic analyzer and `maslc check <file>`.
- Symbol/type checks for current records, enums, queues, resources, pipelines,
  components, instances, workloads, functions and experiments.

## [0.2.0] — 2026-11-18

### Added
- Typed AST, recursive-descent parser, Pratt expression parser and
  `maslc parse <file>`.

## [0.1.0] — 2026-11-18

### Added
- Initial C++20/CMake/CTest foundation, CLI launchers and PHASE 00 design
  baseline.


## 2026-11-23 — Android Studio preview integration (unreleased)

- Added an Android Java Studio shell targeting ARM64 with private source/bytecode
  storage and editor actions for lexing, parsing, semantic checking, Typed IR,
  MASB compilation, and scalar-function execution.
- Added a portable C++ bridge with JNI adapters and host regression tests.
- Added Android Gradle/CMake project configuration. Android NDK build and APK
  packaging remain unverified because SDK/NDK/Gradle tools were unavailable.
