# MASL Test Results

This file records only commands actually executed. Planned tests are not PASS.

## Initial environment inspection

- `g++ --version`: GCC 14.2.0
- `clang++ --version`: Clang 17.0.0
- `cmake --version`: 3.31.6
- `ninja --version`: 1.12.1
- `git --version`: 2.47.3
- `python3 --version`: Python 3.13.5
- GoogleTest discovery: not found in the initial system package/header checks.

## PHASE 01 — Build foundation

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug | GCC 14.2.0 | PASS | 8/8 passed |
| Release | GCC 14.2.0 | PASS | 8/8 passed |
| Debug | Clang 17.0.0 | PASS | 8/8 passed |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 8/8 passed |

Commands executed (from repository root):

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
cmake --preset release
cmake --build --preset release --parallel 2
ctest --preset release
cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ -DMASL_BUILD_TESTS=ON \
  -DMASL_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build/clang --parallel 2
ctest --test-dir build/clang --output-on-failure
cmake --preset sanitizers
cmake --build --preset sanitizers --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1 ctest --preset sanitizers
```

The foundation checks include version/help CLI smoke tests and C++ library tests. The suite has eight distinct CTest cases in each configuration.
The command `maslc check examples/hello.masl` and `masl run examples/hello.masl`
were also executed and both returned exit code 2 with the explicit
"not implemented" diagnostic, as intended; they are not claimed as working
compiler/runtime commands.

## PHASE 02 — Lexer

The dedicated lexer executable and valid/invalid `maslc lex` fixtures are included in all eight CTest entries above.
It checks language header tokens, keywords (including contextual `output` and `latency`),
unit literals, comments, escaped UTF-8 strings, CRLF locations, malformed
numbers, unknown unit suffixes, unterminated strings/comments, invalid
single-character logical operators, malformed UTF-8, and compound operators.
All 8 CTest cases passed in all four configurations (32/32 configuration-specific test executions). The C++ test executables also reported 30 successful named assertions: 26 lexer checks and 4 foundation/version checks. The valid fixture runs through the actual `maslc lex` CLI and confirms the end-user entry point uses the same lexer implementation. The invalid fixture is expected to fail; a separate manual check confirmed it exits 1 and prints `MASL-E1005` for `3widgets`. This is a focused deterministic
unit suite, not a claim that fuzz testing or exhaustive lexer verification has
been performed.


## PHASE 03 — AST and Parser

- Added 1 parser unit-test executable with typed-AST assertions and targeted
  syntax-negative cases.
- Added `maslc parse` valid-source and invalid-source CTest fixtures.
- The canonical ArithmeticPipeline example parses into seven top-level AST
  declarations. The command prints `Syntax OK: model ArithmeticPipeline`.
- Invalid fixture exits with code 1 and reports `MASL-E2002` at the missing
  semicolon location (line 4, column 1).
- The initial test run exposed comparison-expression consumption in experiment
  expectations; parser precedence handling was corrected and the regression is
  retained in the full arithmetic pipeline test.
- Final validation: GCC Debug 11/11, GCC Release 11/11, Clang 17 Debug 11/11,
  and GCC ASan/UBSan 11/11. Total: 44 successful CTest executions across four
  configurations. Both GCC Debug and Release and Clang Debug were built with
  warnings-as-errors; ASan/UBSan used leak detection and halt-on-error.


## PHASE 04 — Semantic Analysis (supported subset)

- Rebuilt from the clean `v0.2.0` Phase 00–03 baseline on branch
  `rebuild-from-phase-04`; repository version advanced to `0.3.0`.
- Added a semantic analyzer with symbol/type resolution, duplicate and
  declaration checks, selected validation for records/enums, queues, resources,
  pipelines, components, instance arrays, functions, workloads, experiments,
  expressions, and engineering quantities.
- Added `maslc check <file>`, which executes lexing, parsing, and semantic
  validation. It validates only the documented supported subset; no execution
  or simulation is implied.
- Semantic unit executable: 28/28 named assertions passed.
- CLI positive fixtures: `ArithmeticPipeline` and `ComponentArrayCheck` both
  passed semantic validation. CLI negative fixture was rejected with
  `MASL-E3004` for zero queue capacity (exit status 1).

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug, warnings-as-errors | GCC 14.2.0 | PASS | 15/15 passed |
| Release, warnings-as-errors | GCC 14.2.0 | PASS | 15/15 passed |
| Debug, warnings-as-errors | Clang 17.0.0 | PASS | 15/15 passed |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 15/15 passed |

Total Phase 04 regression validation: 60/60 CTest executions passed across
four configurations. The C++ semantic executable independently reported
28/28 assertions. GoogleTest was not available, so the configured self-contained
C++ test harness was used. This is a focused test suite, not exhaustive proof,
fuzzing, or a claim of full MASL type-system conformance.

The invalid semantic fixture was also run manually to inspect its diagnostic:

```text
MASL-E3004: tests/data/semantic_invalid.masl:5:20: Queue capacity must be a positive compile-time integer constant.
```

The Android ARM64 build, MASL bytecode generation, runtime execution, and
source-driven hardware simulation were not part of this phase and remain
unverified/unimplemented.

## PHASE 06 — MASB bytecode compiler, loader and verifier (pure-function subset)

- Defined and implemented MASB 1.0: 32-byte fixed header, three required
  sections (functions/code/strings), little-endian fields, 9-byte fixed
  instructions, source-language version metadata, 64 MiB file cap and
  CRC-32/IEEE checksum.
- Implemented serialization, bounded loader, strict section-range checks,
  instruction decoding, reserved-operand validation, call-graph checks,
  control-flow analysis, abstract stack typing, local-store typing, definite
  local initialization, control-flow merge checks and disassembly.
- Added `maslc compile <source> -o <masb>` and `maslc disasm <masb>`.
- Added end-to-end MASB generation/round-trip/verification/disassembly tests,
  corrupt checksum, truncated file, invalid magic, section-gap, malformed
  opcode/type/stack/call-graph and unsupported-construct tests.
- A dedicated regression now verifies that `ArithmeticPipeline.masl` is
  explicitly rejected with `MASL-B6023` and that no `.masb` output is left
  behind. It is not silently compiled as a function-only program.
- Bytecode suite: 18/18 assertions.

## PHASE 07 — Stack VM (pure-function subset)

- Implemented verified stack interpreter for scalar signed-64 operational
  integer, binary64 floating, boolean and string values.
- Implemented constants, local loads/stores, arithmetic, comparisons, boolean
  operations, conditional/unconditional branches, direct function calls,
  return, assert and trap instructions.
- Added checked signed arithmetic, division/modulo traps, non-finite float
  checks, runtime value-type guards, definite initialization verification,
  and configurable instruction/call-depth/stack/string limits.
- Added `masl verify` and `masl run` with typed scalar entry arguments.
- Tests cover arithmetic (including 42), parameters/locals, calls, positive and
  negative branches, bounded loops, boolean and string returns, assertions,
  wrong arity/type, instruction-budget exhaustion, overflow and divide-by-zero.
- VM suite: 17/17 assertions.

## PHASE 08 — Rational time and deterministic event kernel

- Implemented normalized rational femtosecond time, checked addition and
  multiplication, and overflow-safe exact fraction comparison.
- Implemented exact clock period/edge calculation, event scheduling, target
  handlers, timestamp/priority/insertion-sequence ordering, event and cycle
  runners, processing/pending capacity limits, and schedule/dispatch trace
  records.
- Added repeated cycle-run, same-timestamp priority, stable sequence,
  handler-scheduled event, missing handler, handler exception, no-clock,
  zero-cycle, capacity and event-budget tests.
- A property-style deterministic sweep compares the fraction comparator
  against exact cross-products for 176,400 bounded rational pairs.
- Runtime suite: 29/29 assertions.
- Integration boundary: Phase 08 is currently a tested C++ API. It is not yet
  invoked by MASL Workload/Experiment declarations or by the VM; queue/resource/
  pipeline/component behavior remains in later phases.

### Final multi-configuration validation for repository version 0.7.0

All builds use C++20 and warnings-as-errors. The sanitizer build additionally
uses AddressSanitizer, UndefinedBehaviorSanitizer, leak detection and
halt-on-error. GoogleTest is not installed in the test environment, so the
project's self-contained C++ test harness is used.

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug | GCC 14.2.0 | PASS | 31/31 passed |
| Release | GCC 14.2.0 | PASS | 31/31 passed |
| Debug | Clang 17.0.0 | PASS | 31/31 passed |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 31/31 passed |

Aggregate: 124/124 configuration-specific CTest executions passed. Dedicated
Phase 06–08 tests reported 64 assertions total (18 + 17 + 29), with zero
failures. One initial Release warning and one Clang promotion warning were
fixed in source and the affected configurations were rebuilt and rerun. The
first sanitizer build invocation reached the execution time limit while
compiling; the resumed build completed successfully and all sanitizer tests
passed. This is not a fuzzing campaign, proof of correctness, Android build, or
full hardware-simulation acceptance.


## PHASE 09 — Bounded queues and ownership-aware resources

- Added platform-neutral `BoundedQueue<T>` with positive finite capacity,
  FIFO, stable priority and producer round-robin disciplines; explicit
  Block/Reject/Drop overflow outcomes; separately bounded blocked producers;
  automatic promotion; occupancy/counter statistics; invariant checks; bounded
  transition trace storage.
- Added Resource with positive unit capacity, owner-bound leases, partial
  release, explicit Reject/Block acquisition, bounded FIFO waiters, grants
  retrievable exactly once by ticket, cancellation of queued requests and
  undelivered grants, invariant checks, utilization counters and bounded trace.
- Integrated both APIs with Phase 08 by submitting and resuming work from
  deterministic event handlers in tests.
- Dedicated test executables: Queue 26/26 assertions; Resource 31/31 assertions.

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug, warnings-as-errors | GCC 14.2.0 | PASS | 33/33 |
| Release, warnings-as-errors | GCC 14.2.0 | PASS | 33/33 |
| Debug, warnings-as-errors | Clang 17.0.0 | PASS | 33/33 |
| Debug + ASan/UBSan, leak detection | GCC 14.2.0 | PASS | 33/33 |

All four full configurations passed 33/33 CTest entries (132/132 total
configuration-specific executions). This is not a fuzz campaign or an
Android/Windows/macOS build.

Phase boundary: Queue and Resource are tested C++20 APIs used from Event Kernel
handlers. They are not yet instantiated from `.masl` declarations; complete
source-driven hardware simulation remains future work.


## 2026-11-23 — Android Studio bridge integration (host validation)

- Added the portable `masl_mobile_bridge` used by the Android JNI layer.
- Bridge actions tested: lex, parse, semantic check, Typed IR, compile to
  MASB, execute a zero-argument function, typed integer arguments, branch
  execution, invalid argument rejection, missing function rejection, and
  invalid source diagnostics.
- The JNI translation unit passed a host C++ syntax/compile check against the
  installed JDK JNI headers. This is not an Android NDK compile.

| Configuration | Build | CTest |
|---|---:|---:|
| GCC 14.2 Debug, warnings-as-errors | PASS | 34/34 passed |
| GCC 14.2 Release, warnings-as-errors | PASS | 34/34 passed |

Android SDK/NDK/Gradle were absent from the environment and external package
hostnames could not be resolved. No APK build, APK signing/inspection, device
installation, or Android runtime test is claimed. The separate sanitizer build
was not completed in this execution window; earlier Phase 09 sanitizer evidence
remains the repository's historical report, not a new run in this session.
- `jni_bridge.cpp` compiled successfully on the host using the installed JDK 21
  JNI headers; this only checks C++/JNI declaration syntax and does not replace
  an NDK cross-compile.
- `MainActivity.java` passed `javac` syntax/type checking against minimal local
  Android API stubs. This is not an Android SDK compile or APK build.
- The sample bytecode was separately compiled, loaded and executed by the
  Release CLI: `answer()` → 42, `sum_to()` → 45, `twice(21)` → 42, and
  `choose(-7)` → 0.
