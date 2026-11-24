# MASL Project State

Last updated: 2026-11-23
Repository version: 0.10.0
Target language release: 1.0.0

## Overall status

**Executable compiler/runtime slice plus a standalone synthetic CPU/GPU
architecture simulator. Android Studio source is connected to the shared core,
but the Android NDK build, APK and physical-device acceptance are still
pending.**

Version 0.10.0 adds a trace-driven CPU/GPU simulator with an explicit public C++
configuration API, host CLI and native bridge. It is deterministic for a fixed
configuration/seed and has bounded input/cycle/instruction budgets. It does not
yet compile hardware declarations from `.masl` into an executable model, and
it is not a cycle-accurate reproduction of a real CPU/GPU.

## Baseline and environment

- Host validation target: Linux x86_64.
- GCC: 14.2.0.
- CMake: 3.31.6.
- Ninja build system.
- GoogleTest unavailable; CMake uses the self-contained C++ test harness.
- Debug and Release builds use `-Wall -Wextra -Wpedantic -Wconversion`
  `-Wsign-conversion -Wshadow -Wformat=2 -Wnull-dereference`
  `-Wdouble-promotion -Werror`.
- Android SDK, NDK, Gradle and APK packaging binaries are absent in this
  execution container; the Android project has not been built here.

## Phase status

| Phase | Status | Notes |
|---|---|---|
| 00 — Language Blueprint | IMPLEMENTED | Design artifacts. |
| 01 — Build Foundation | IMPLEMENTED_AND_TESTED | C++20/CMake/CTest, warning gates and CLI. |
| 02 — Lexer | IMPLEMENTED_AND_TESTED | Tokens, units, UTF-8, spans and diagnostics. |
| 03 — AST and Parser | IMPLEMENTED_AND_TESTED | Owning AST and recursive-descent/Pratt parser. |
| 04 — Semantic Analysis | IMPLEMENTED_AND_TESTED (subset) | Supported declarations and expression checks. |
| 05 — Typed IR | IMPLEMENTED_AND_TESTED (current AST subset) | IR schema 1.0, lowering and verifier. |
| 06 — MASB Bytecode | IMPLEMENTED_AND_TESTED (pure-function subset) | Writer, loader, CRC, disassembler and CFG/type verifier. Hardware declarations rejected. |
| 07 — VM | IMPLEMENTED_AND_TESTED (pure-function subset) | Scalar stack VM, bounded calls/loops and traps. |
| 08 — Time/Event Kernel | IMPLEMENTED_AND_TESTED (C++ API) | Rational time, clock edges, deterministic events and cycle/event runners; not source-integrated. |
| 09 — Queues/Resources | IMPLEMENTED_AND_TESTED (C++ API) | Bounded queues and owner-bound resource leases; not source-integrated. |
| 10 — Pipelines/Interconnect | NOT_STARTED | Link-transfer cost exists only as a GPU host/device abstraction; no pipeline/interconnect fabric. |
| 11 — Components/Arrays | PARTIAL | Parser/semantic/IR metadata exists; no whole-model runtime elaborator. |
| 12 — Cache/DRAM | PARTIAL_IMPLEMENTED | Deterministic tag-based L1I/L1D/L2/LLC hierarchy and channel queue; no coherence or full DRAM timing. |
| 13 — Workload/Experiment | PARTIAL_IMPLEMENTED | Synthetic CPU/GPU Workload config runs; `.masl` Workload/Experiment declarations are not connected. |
| 14 — Trace/Profiler | PARTIAL | Aggregate counters/reports exist; no cycle-by-cycle trace or JSON/CSV profiler. |
| 15 — GPU/CPU Libraries | PARTIAL_IMPLEMENTED | CPU ROB/scoreboard approximation and GPU warp/SIMT approximation; not an ISA or RTL model. |
| 16 — Hardening/Benchmark | PARTIAL | Strict warnings and bounded tests; no long fuzz campaign or calibrated benchmark suite. |
| 17 — Packaging/Release | PARTIAL | Host build validated and Android CI workflow supplied; no Android APK artifact/device report. |
| 18 — Android Core Port | PARTIAL | Portable C++ bridge host-tested and JNI source connected; NDK ARM64 not built. |
| 19 — MASL Studio | PARTIAL | Android source provides editor, diagnostics, MASB compile/run, CPU/GPU synthetic simulation; no APK/device acceptance or visualization. |
| 20 — MASL App / Cross-platform App Toolchain | PLANNED | General-purpose application profile, UI/stdlib, AOT/native backends and desktop packaging. |

## Added in 0.10.0

- `include/masl/sim/simulator.hpp` and `src/sim/simulator.cpp`: public simulator
  API and deterministic CPU/GPU model.
- CPU: multi-core synthetic instruction streams; fetch/issue/retire widths;
  bounded ROB; dependency scoreboard; integer/FP/load-store/branch units;
  branch penalty; L1I/L1D, shared L2/LLC and DRAM channel queues.
- GPU: warp-level SIMT scheduling; SM issue slots; fixed 256-thread block
  occupancy accounting; register/shared-memory/warp resident limits; integer,
  FP, tensor, branch and shared operations; global-memory coalescing; bank
  conflict estimation; host/device link cycles.
- `maslsim` CLI for CPU and GPU workloads with configurable size, seed and GPU
  memory access pattern.
- Shared mobile bridge and JNI entry point; Android Studio CPU/GPU controls.
- Updated Android build configuration and GitHub Actions workflow targeting
  ARM64 debug APK generation.
- Simulator model specification and explicit limitations.

## Validation state

- Debug build with warnings-as-errors: PASS.
- Release build with warnings-as-errors: PASS.
- GCC Debug CTest: 39/39 PASS.
- GCC Release CTest: 39/39 PASS.
- GCC AddressSanitizer + UndefinedBehaviorSanitizer CTest: 39/39 PASS.
- Clang 17 Debug CTest: 39/39 PASS.
- Stress: CPU 4,000,000 dynamic instructions completed on the Linux host;
  GPU 4,000,000 threads × 1 instruction completed; repeated CPU seed/config
  output was byte-identical.
- JNI C++ source passed a host JDK-header syntax check; Android workflow YAML
  parsed successfully. These do not replace an Android NDK/Gradle build.
- Android NDK/Gradle build, APK inspection/signing/install, Android JNI runtime
  tests on device: NOT RUN in this container.

## Not implemented

MASL-source-to-simulator elaboration; executable component graph; pipeline and
network-on-chip models; cache coherence; realistic DRAM timing; TLB/page tables;
store buffers/MSHR/prefetchers; complete branch prediction and wrong-path
recovery; full GPU block/warp reconvergence, PTX/SASS or CUDA execution;
DMA/stream overlap; CPU+GPU heterogeneous shared memory; cycle traces and
JSON/CSV profiler; measured power/thermal/DVFS; fuzz campaigns; Android APK and
physical-device acceptance; general-purpose MASL App and desktop native/AOT
backends.

## Release policy

Do not label the language `1.0.0` until the parser/IR/compiler, source-driven
simulation, platform and Android acceptance criteria pass clean builds and
supported-device tests. A successful synthetic simulation is evidence only
for the documented model and configuration, not for the accuracy of a physical
chip prediction.
