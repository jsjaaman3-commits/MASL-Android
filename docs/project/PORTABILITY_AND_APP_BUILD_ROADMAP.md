# MASL Portability and Cross-Platform Application Roadmap

## Product direction

MASL is being developed Android-first. Android is the first product platform
and the first required target for the shared core. Windows, Linux, and macOS
are later first-class build and application targets, not separate language
ports. The same language front end, Typed IR, MASB format, VM, and simulation
semantics should be shared wherever practical.

The product direction now has two related profiles:

- **MASL Sim**: architecture description and deterministic hardware simulation
  (CPU, GPU, queues, memory, interconnect, workloads and experiments).
- **MASL App**: general-purpose application development using portable language
  modules, standard libraries, platform services, UI facilities and native
  application packaging.

Both profiles share the core language, type system, compiler infrastructure,
bytecode conventions, diagnostics, project format and tooling. Simulation-only
semantics must not be forced into ordinary application code.

## Target platform policy

1. Keep `masl_core` free of direct Android, Windows, POSIX, and Apple framework
   dependencies.
2. Put platform-specific code behind narrow interfaces and separate adapters:
   `platform/android`, `platform/windows`, `platform/linux`, and
   `platform/macos`.
3. Use Android NDK/ARM64 as the first native target and validate on a real
   Android device before declaring Android support.
4. Preserve portable MASB execution across architectures; define explicit
   numeric and serialization rules before promising cross-architecture
   bit-for-bit reproducibility.
5. Add Windows, Linux and macOS toolchains only after the shared core passes
   Android acceptance, and run the same conformance suite on each platform.
6. Keep build-time SDK/compiler integrations separate from MASL program
   execution. MASL bytecode must not gain arbitrary shell, process, network or
   filesystem privileges as a side effect of adding application build support.

## Required architecture layers

- **Portable core**: lexer, parser, semantic analysis, IR, MASB, VM, simulator,
  standard language/runtime algorithms.
- **Platform services**: filesystem sandbox, app lifecycle, permissions,
  clocks, input, display, networking and process integration through reviewed
  capability interfaces.
- **Application UI layer**: a common declarative UI abstraction with separate
  native adapters; no OS-specific UI object is embedded in the portable IR.
- **Build backends**: portable bytecode execution first, then a documented
  ahead-of-time/native backend where native performance or platform packaging
  requires it. Target ABI, SDK version, architecture and linker metadata must
  be explicit in the project manifest.
- **Packaging/signing**: Android APK/AAB, Windows app/executable, Linux
  executable/package, and macOS app bundle. Signing keys remain user-controlled
  and are never embedded in ordinary project files.

## Cross-platform app-development acceptance target

MASL must eventually support creating and building real applications for:

- Android ARM64 (first target).
- Windows x64 and ARM64 where supported by the selected backend/toolchain.
- Linux x86_64 and ARM64 where supported by the selected backend/toolchain.
- macOS Apple Silicon and Intel only where the selected SDK/backend supports
  those architectures.

A platform is supported only after clean build, run, file/package inspection,
permissions review, and the same language/runtime conformance tests pass on
that platform. Merely generating C++ source or a project template is not
platform support.

## Planned gates

- Phase 09 onward: all new runtime code remains portable C++20 and avoids
  platform APIs.
- Phase 17: add reproducible packaging and CI matrix design; no platform is
  marked supported without its own test evidence.
- Phase 18: build the shared core with Android NDK for ARM64 and run core tests
  on device; add platform service/JNI boundary only where required.
- Phase 19: deliver Android MASL Studio with editor, project management,
  diagnostics, build/run controls and simulation visualization.
- Phase 20: expand MASL into the general-purpose MASL App profile; implement
  portable standard libraries, application/UI abstractions, AOT/native backend,
  cross-compilation manifests, SDK discovery, packaging and conformance on
  Windows, Linux and macOS.

## Important constraint

The current MASL 0.9.0 release is still a hardware-simulation language/runtime
slice, not a general-purpose native app builder. Phase 20 is a substantial
language, compiler, standard-library and product milestone. It is recorded as
an explicit acceptance target rather than being implied by the existing C++20
compiler implementation.
