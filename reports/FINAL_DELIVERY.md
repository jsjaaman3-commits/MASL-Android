# MASL 0.10.0 — Integrated CPU/GPU Simulator Delivery Report

Date: 2026-11-23

## Delivered

- Portable C++20 simulation engine with public `SimulatorConfig`, `CpuConfig`,
  `GpuConfig`, `Workload` and `SimulationResult` APIs.
- `maslsim` host CLI for CPU and GPU synthetic workloads.
- CPU model: multi-core deterministic synthetic streams, fetch/issue/retire
  width, bounded reorder buffer, dependency scoreboard, functional-unit
  contention, branch front-end penalty, private L1I/L1D, shared L2/LLC and
  channel-queued DRAM abstraction.
- GPU model: SM/warp round-robin scheduling, issue slots, register/shared-memory
  occupancy constraints, operation latency classes, memory coalescing,
  shared-memory bank-conflict proxy, lane-utilization proxy and host/device
  transfer-cost accounting.
- Android Studio source with MASL editor, Lex/Parse/Check/Typed-IR, MASB
  compile/run and CPU/GPU generated-workload controls connected through JNI.
- GitHub Actions workflow to build an ARM64 debug APK with pinned Android
  toolchain components.
- C++ model contract, limitations, test matrix, Android APK build guide and
  deterministic sample reports.

## Validation performed

All 39 CTest cases passed in each of four independent build/test matrices:

| Build matrix | Result |
|---|---:|
| GCC 14.2 Debug, strict warnings | 39/39 passed |
| GCC 14.2 Release, strict warnings | 39/39 passed |
| GCC 14.2 Debug + AddressSanitizer + UndefinedBehaviorSanitizer | 39/39 passed |
| Clang 17 Debug, strict warnings | 39/39 passed |

Additional checks:

- CPU 4,000,000-instruction workload completed on the Linux host in 2.58 s
  wall time with 41,448 KiB peak RSS.
- GPU 4,000,000-thread × 1-instruction workload completed in 0.08 s wall time
  with 5,120 KiB peak RSS.
- A repeated CPU run using the same workload and seed produced byte-identical
  report output.
- Coalesced, strided and random GPU access patterns each completed and
  generated different transaction/conflict statistics.
- Invalid GPU memory-pattern input returned exit code 2 and a diagnostic.
- JNI C++ source passed a host syntax-only compile using JDK 21 JNI headers
  and strict warnings.
- GitHub Actions YAML passed a YAML parse check.
- Release host binaries `maslc`, `masl` and `maslsim` were built and inspected
  as Linux x86_64 ELF executables.

Host stress timings are host-specific and are not Android benchmarks.

## APK status — not built

There is no APK in this delivery. The container has no `gradle`, Android
`sdkmanager`, `adb`, Android SDK or NDK installed, and Android tool downloads
were not reachable from the container. The Android Java source received a
syntax/parser check only; the real Android Gradle and NDK build was not run.
No APK ZIP inspection, signing verification, installation or physical-device
runtime test is claimed.

The repository includes `.github/workflows/android-apk.yml`. Run the workflow
in GitHub to install the toolchain, execute `:app:assembleDebug`, and publish
the resulting ARM64 APK as the artifact
`MASL-Studio-0.10.0-Android-ARM64-debug`.

## Model scope — not a complete physical CPU/GPU simulator

The 0.10.0 engine is a deterministic synthetic architectural approximation.
It is configurable and useful for comparative experiments within its stated
model; it is not an ISA emulator, CUDA/PTX/SASS executor, RTL simulator, or
cycle-accurate model of an RTX 5090 or another physical part.

Not implemented: source-driven lowering of `.masl` hardware declarations into
simulator configurations; full pipeline/NoC models; cache coherence; TLB and
page-table behavior; detailed DRAM row/bank/refresh timing; MSHRs, prefetchers,
writeback and store-buffer behavior; a complete branch predictor and
wrong-path recovery; full GPU block execution/reconvergence; CUDA runtime or
real GPU code execution; DMA/stream overlap; simultaneous heterogeneous
CPU/GPU memory coherence; cycle-by-cycle trace/JSON profiler; thermal, DVFS or
measured power modeling.

These limits are documented rather than hidden. A truly complete simulator
must first define the architecture/ISA and timing contract it is expected to
reproduce, then be calibrated against independent hardware/RTL/reference
models. The 0.10.0 release provides the first executable, testable simulator
and Android Studio integration, not that final calibrated model.
