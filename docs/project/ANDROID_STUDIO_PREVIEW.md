# MASL Studio Android — Version 0.10.0

Status: source implementation and host-tested native bridge available;
Android ARM64 NDK build, APK packaging and physical-device acceptance pending.

## Included behavior

- MASL source editor with an executable scalar-function example.
- Save/load one source project in application-private storage.
- Native Lexer, Parser, Semantic Checker and Typed IR actions.
- MASB compilation, storage, bytecode loading/verification and scalar VM run.
- CPU/GPU synthetic-workload simulation through the shared native C++ simulator.
- CPU instruction-count and GPU instruction/thread-count controls.
- Text report with modeled cycles, estimated time, memory/caches, DRAM,
  occupancy, lane utilization, bank conflicts and host/device transfer cost.
- No Internet, external storage or shell/process permission is required by the app.

## Architecture

`MainActivity.java` provides the Android UI and app-private file handling.
`jni_bridge.cpp` is the JNI adapter. `masl_mobile_bridge.cpp` is portable C++
that calls the compiler, bytecode, VM and simulation APIs. The same bridge is
built by the root CMake project and exercised by host tests in
`tests/mobile_bridge_tests.cpp`.

The Android CMake project compiles shared core sources for the `arm64-v8a` ABI
and links them into `libmasl_jni.so`. The core has no Android framework calls;
UI, lifecycle and private-file responsibilities stay in the Java/JNI layer.

## Pinned build configuration

- Android application namespace: `com.mahdi.masl`.
- Package ID: `com.mahdi.masl` (debug suffix: `.debug`).
- ABI: `arm64-v8a` only.
- Minimum SDK: API 26.
- Compile SDK / target SDK: API 36.
- Android NDK: `30.0.16248370`.
- Android CMake: `3.22.1`.
- Android Gradle Plugin: `9.5.2`.
- Gradle distribution: `9.9.2`.
- Java source/target compatibility: 17.

The selected AGP 9.5 line requires Gradle 9.6.0 or newer; Gradle 9.9.2 exceeds
that minimum. The selected NDK revision is the current r30 LTS. See the official
Android/Gradle release references cited in the delivery report.

## Build on Android Studio or a configured host

Open the `android/` directory in Android Studio. Install Platform 36, Build
Tools 36.0.0, NDK `30.0.16248370`, CMake `3.22.1`, and JDK 17 when prompted.
Select the `app` debug build variant and run **Build > Build Bundle(s) / APK(s)
> Build APK(s)**.

On a host where Gradle and the pinned Android components are installed:

```sh
cd android
gradle --no-daemon :app:assembleDebug
```

Expected output:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

The GitHub Actions workflow at `.github/workflows/android-apk.yml` installs
pinned components, runs the same Gradle task and uploads the ARM64 debug APK as
an artifact. Push this repository to GitHub and run the workflow manually or
push a change to one of its watched paths.

## Runtime boundary

Studio runs the existing scalar pure-function compiler/VM subset. The simulator
buttons invoke the native synthetic-workload engine directly through JNI. The
MASL parser/IR does not yet lower hardware declarations into this simulation
engine. The app UI currently uses default architecture parameters and a
coalesced memory pattern; deeper model configuration is available in the C++
API and host CLI. Results are textual; no cycle timeline or live visualization
is included.

## Acceptance still required

1. Build the APK with the pinned NDK and Android SDK for `arm64-v8a`.
2. Inspect APK ZIP contents, manifest, package ID, ABI libraries, SDK levels,
   permissions and debug signature.
3. Install and launch on a physical ARM64 Android device.
4. Run Example → Check → Build MASB → Run `answer`; verify result `42`.
5. Run CPU and GPU simulations; verify a completed report with nonzero cycles
   and expected engine type.
6. Exercise invalid counts, very large workloads, syntax errors, invalid
   arguments, missing functions, oversized MASB and application lifecycle
   interruption.
7. Force-stop/relaunch and verify source/bytecode private-storage behavior.
8. Capture Android logcat and record memory/latency on at least one supported
   device before claiming Android support.

Until those gates pass, Android is a source/CI build target, not a verified
installable product release. The current delivery does not contain an APK.
