# Android Studio Preview Validation Report

Date: 2026-11-23  
Source version: 0.9.0  
Git commit: `4beac489fb68e1d1ca1e4349a4bbfc93a13de001`  
Target ABI configured: `arm64-v8a`

## Source integrity and review

- The supplied source ZIP was compared file-by-file and SHA-256-by-SHA-256
  against the Git bundle's `HEAD` tree: 80/80 files matched with no missing,
  extra, or content-mismatched files.
- The separate cross-platform roadmap was byte-identical to the roadmap inside
  the source tree.
- Reviewed README, project state, phase plan, language/compiler/IR/bytecode/VM
  specifications, known limitations, test matrix/results, phase 09 validation,
  public headers, CMake configuration, CLI entry points, and source inventory.
- The existing implementation is a compiler/runtime vertical slice, not a
  complete source-driven hardware simulator or general-purpose application
  compiler. Documented boundaries were preserved rather than silently
  represented as implemented.

## Changes in this preview

- Added a portable C++ mobile bridge around the existing lexer, parser,
  semantic analyzer, Typed IR, MASB compiler/serializer, bytecode loader, and VM.
- Added JNI exports for analysis, compilation, and bytecode execution.
- Added an Android Java application shell with a source editor, an example,
  private app-storage save/load, Lex, Parse, Check, Typed IR, Build MASB and Run.
- Added Android Gradle/CMake configuration, an Android build guide, and a
  GitHub Actions workflow for producing an ARM64 debug APK after push.
- Added dedicated host regression tests for the shared mobile bridge.

## Executed validation

| Check | Result |
|---|---|
| GCC 14.2 Debug build, warnings-as-errors | PASS |
| Debug CTest | 34/34 PASS |
| GCC 14.2 Release build, warnings-as-errors | PASS |
| Release CTest | 34/34 PASS |
| Mobile bridge lex/parse/check/IR/compile/run/error tests | PASS (included above) |
| Host compilation of JNI C++ translation unit with JDK headers | PASS |
| Java source syntax/type check using minimal local Android API stubs | PASS |
| JNI/Java native method descriptors cross-check | PASS |
| Android manifest/styles XML parse | PASS |
| GitHub Actions workflow YAML parse | PASS |
| Release CLI compile/verify/run sample | PASS |
| Android NDK ARM64 cross-build | NOT RUN |
| APK package/signature/manifest inspection | NOT RUN |
| Emulator or physical Android install/run | NOT RUN |

Sample runtime results from the compiled `ExecutableFunctions.masb`:

| Function | Result |
|---|---:|
| `answer()` | 42 |
| `sum_to()` | 45 |
| `twice(21)` | 42 |
| `choose(-7)` | 0 |

## APK blocker

The execution environment did not contain Android SDK/NDK, Gradle, `aapt2`,
`d8`, `apksigner`, or `zipalign`. Attempts to reach the Android and Gradle
artifact hosts failed at DNS resolution. Therefore a real APK could not be
built, signed, installed, or tested in this environment. No placeholder APK
has been created or represented as installable.

The Android project is configured for Android Gradle Plugin 9.4.0, Gradle
9.6.0, SDK platform 36 (target 35), NDK 30.0.16248370 and CMake 3.22.1. The
included GitHub Actions workflow can build and upload the debug APK after the
source is pushed to a GitHub repository, but that workflow itself has not been
run here.

## Explicit remaining acceptance work

1. Run the Android Gradle/NDK build for `arm64-v8a`.
2. Inspect package metadata, native ABI contents, app permissions and APK
   signature.
3. Install on a physical Android ARM64 device and exercise Example → Check →
   Build MASB → Run `answer` (expected result: 42).
4. Test syntax/semantic error paths, invalid function args, bad bytecode,
   app lifecycle changes and project persistence.
5. Implement the missing simulation visualization and source-driven simulation
   phases before claiming the fuller Phase 19 product target.
