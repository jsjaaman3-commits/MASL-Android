# MASL — Mahdi Architecture Simulation Language

**Repository version: 0.10.0 (development release).** MASL is an Android-first,
C++20 language and toolchain with two planned profiles: **MASL Sim** for
computer-architecture modelling and **MASL App** for general-purpose application
development. This release adds an executable, deterministic synthetic CPU/GPU
architecture simulator and connects it to the shared native core used by MASL
Studio.

This is a substantive simulator implementation, not a cycle-accurate model of
any specific commercial processor. It models a declared set of architecture
parameters, generated workloads, pipeline/resource limits, cache hierarchy,
DRAM channel contention, GPU warps, memory coalescing and transfer costs. The
model assumptions and exclusions are specified in
[`docs/simulation/SIMULATOR_0_10_SPEC.md`](docs/simulation/SIMULATOR_0_10_SPEC.md).

## Implemented in 0.10.0

- Multi-core CPU trace model with fetch, issue and retire widths; bounded reorder
  buffer; dependency scoreboard; integer, floating-point, load/store and branch
  execution resources; configured branch-misprediction front-end penalty.
- Separate private L1 instruction and data caches, shared L2 and last-level
  cache, deterministic set-associative LRU tag model, and a channel-contention
  memory controller with configurable transfer bandwidth and latency.
- GPU SIMT model with configurable warp width, SM count, issue slots, register
  and shared-memory resource limits, resident-warp calculation, round-robin
  warp scheduling, integer/FP/tensor/branch/shared-memory operation latencies.
- Global-memory lane-address generation for coalesced, strided and random
  patterns; cache-line transaction coalescing; shared-memory bank-conflict
  estimation using the configured bank width.
- Host-to-device and device-to-host transfer latency/bandwidth accounting.
- `maslsim` host CLI, portable C++ API, Android JNI bridge, and Studio buttons for
  CPU/GPU simulations. All generated workload streams are deterministic for a
  fixed configuration and seed.
- Explicit simulation limits, validation budgets and regression tests.

## Build requirements

- C++20 compiler: GCC 12+ or Clang 15+ (MSVC support is configured but has not
  been validated in this release).
- CMake 3.25+.
- Ninja or another supported CMake generator.
- GoogleTest is optional; a self-contained C++ test harness is used if it is
  unavailable.

## Build and test (host)

Debug:

```sh
cmake -S . -B build-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMASL_BUILD_TESTS=ON \
  -DMASL_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

Release:

```sh
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMASL_BUILD_TESTS=ON \
  -DMASL_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

The repository also provides CMake presets for Debug, Release and sanitizer
builds. When available, run the ASan/UBSan preset as part of local validation.

## Run the architecture simulator

```sh
# CPU: dynamic instruction count and deterministic seed
./build-release/maslsim cpu 50000 20261123

# GPU: instructions per thread, thread count, seed, memory access pattern
./build-release/maslsim gpu 64 32768 20261123 coalesced
./build-release/maslsim gpu 64 32768 20261123 strided
./build-release/maslsim gpu 64 32768 20261123 random
```

The CLI prints cycles, estimated time, IPC, stall counters, cache hit rates,
DRAM traffic, GPU occupancy/lane efficiency, shared-memory bank conflicts and
host/device link cost. The C++ API exposes `SimulatorConfig`, `Workload`,
`CpuConfig`, `GpuConfig`, `SimulationResult`, `simulate_cpu`, `simulate_gpu`
and `simulate` in `include/masl/sim/simulator.hpp`.

## MASL compiler and runtime

```sh
./build-release/maslc --version
./build-release/maslc lex tests/data/lexer_sample.masl
./build-release/maslc parse examples/language_basics/arithmetic_pipeline.masl
./build-release/maslc check examples/language_basics/arithmetic_pipeline.masl
./build-release/maslc ir examples/language_basics/arithmetic_pipeline.masl
./build-release/maslc compile examples/language_basics/executable_functions.masl -o app.masb
./build-release/maslc disasm app.masb
./build-release/masl verify app.masb
./build-release/masl run app.masb answer
./build-release/masl run app.masb twice 21
```

The existing bytecode backend still compiles the supported pure-function
subset. Hardware/workload declarations are not yet automatically elaborated
from `.masl` source into `SimulatorConfig`; the simulator is currently invoked
through its C++ API, host CLI, or Studio's generated-workload controls.
Unsupported source declarations are rejected rather than silently discarded.

## Android Studio

The Android project is under [`android/`](android/). It targets ARM64
(`arm64-v8a`), Android API 26+, compile/target SDK 36, NDK
`30.0.16248370`, Android CMake `3.22.1`, Android Gradle Plugin `9.5.2`,
and Gradle `9.9.2`. It uses Java 17 and a JNI boundary over the same C++20 core.

Studio includes a MASL source editor, private app storage, lexical/syntax/
semantic/IR analysis, MASB compile/run controls, and CPU/GPU synthetic-workload
simulation. Simulation output is textual in this release; a live visualizer and
source-declared hardware elaboration are not included.

Build a debug APK on a configured machine:

```sh
cd android
gradle --no-daemon :app:assembleDebug
```

The expected output is:
`android/app/build/outputs/apk/debug/app-debug.apk`.
The included `.github/workflows/android-apk.yml` installs the pinned Android
components and creates an ARM64 debug APK artifact when run in GitHub Actions.

**APK status for this delivery:** the source and CI build definition are present,
but no APK was built in the current environment. Android SDK, NDK, Gradle and
APK packaging tools are absent here, and Android artifact download endpoints
were not reachable from the build container. Therefore no Android binary or
real-device test is claimed. Use Android Studio or the GitHub Actions workflow
to produce the installable APK, then complete the device acceptance checklist
in [`docs/project/ANDROID_STUDIO_PREVIEW.md`](docs/project/ANDROID_STUDIO_PREVIEW.md).

## Model boundaries

The 0.10 simulator is a deterministic architectural approximation driven by
synthetic instruction streams. It is not a real CPU/GPU instruction-set
emulator, RTL simulator, CUDA runtime, or cycle-accurate reproduction of an
RTX 5090 or another specific chip. It does not model cache coherence, TLB/page
tables, detailed DRAM row/refresh timing, a complete branch predictor/wrong-path
recovery, full GPU thread-block execution semantics, asynchronous streams,
OS scheduling noise, DVFS, thermal behaviour or measured silicon power. The
limitations are enumerated in the simulator specification and
[`docs/project/KNOWN_LIMITATIONS.md`](docs/project/KNOWN_LIMITATIONS.md).

The simulator is also not yet source-driven by the MASL compiler. A later
compiler/elaboration milestone must connect MASL model declarations and
workloads to this engine before MASL can claim end-to-end executable hardware
models.

## Cross-platform direction

The portable C++20 core remains free of direct OS APIs. Android ARM64 is the
first native application target; Windows, Linux and macOS are later build and
application targets that must pass their own conformance and packaging gates.
The planned general-purpose MASL App profile, native/AOT backends, portable UI
and platform SDK integration remain future work. See
[`docs/project/PORTABILITY_AND_APP_BUILD_ROADMAP.md`](docs/project/PORTABILITY_AND_APP_BUILD_ROADMAP.md).

## License

MIT. See [`LICENSE`](LICENSE).
