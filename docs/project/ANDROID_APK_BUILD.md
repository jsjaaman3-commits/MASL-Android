# Building MASL Studio 0.10.0 for Android

The Android application uses the shared portable MASL C++20 core and the JNI
bridge in `android/app/src/main/cpp`. The configured APK target is ARM64
(`arm64-v8a`) and the debug application ID is `com.mahdi.masl.debug`.

## Pinned build toolchain

- JDK 17
- Android Gradle Plugin 9.5.2
- Gradle 9.9.2 (AGP 9.5 requires Gradle 9.6 or newer)
- Android SDK Platform 36
- Android Build Tools 36.0.0
- Android NDK `30.0.16248370` (r30 LTS)
- CMake 3.22.1

## Local build

From the repository root, with the toolchain above installed:

```bash
gradle --no-daemon -p android :app:assembleDebug
```

Expected output:

`android/app/build/outputs/apk/debug/app-debug.apk`

The debug APK is signed with the standard debug key. It is suitable for
installation/testing, not for public release. A release build must be signed
with a key controlled by the app owner.

## GitHub Actions

Run **Build MASL Studio Android APK** from the Actions tab, or push a change
to the configured source paths on `main` or `masl-android-build`. The workflow
runs the portable C++ test suite, builds the ARM64 APK, verifies the APK
signature, checks the application ID, and checks that `libmasl_jni.so` is an
AArch64 ELF before uploading the APK artifact.

A successful workflow creates the artifact:

`MASL-Studio-0.10.0-Android-ARM64-debug`

The CI workflow has not been executed until a successful run is visible in the
GitHub Actions history. A source archive or a passing host test suite is not an
APK and must not be represented as one.
