# MASL Test Matrix — 0.10.0

Status: Linux x86_64 GCC 14.2 Debug and Release test runs are distinguished
from Android tests that have not been executed.

| Area | Test class | Evidence | Status |
|---|---|---|---|
| Build | GCC Debug | CMake/Ninja clean configure, strict warnings, compile and link | PASS |
| Build | GCC Release | Optimized build with warnings-as-errors | PASS |
| Build | GCC ASan/UBSan | AddressSanitizer + UndefinedBehaviorSanitizer, warnings-as-errors | PASS |
| Build | Clang 17 Debug | Independent compiler build with warnings-as-errors | PASS |
| Compiler CLI | smoke | version/help, lex/parse/check positives and negatives | PASS |
| Lexer | unit | tokens, operators, comments, strings, numeric/unit literals, UTF-8, spans and diagnostics | PASS |
| Parser | unit | valid/invalid grammar, precedence, AST shape and diagnostics | PASS |
| Semantic | unit/CLI | supported symbols, declarations, references and invalid fixtures | PASS (supported subset) |
| Typed IR | unit/golden | lowering and structural verification | PASS (current AST subset) |
| MASB | unit/adversarial | serialization, CRC, malformed inputs, opcode/CFG/type checks | PASS (pure-function subset) |
| VM | unit/CLI | scalar arithmetic/control flow/calls/loops/assertions/budgets/traps | PASS (no aggregates) |
| Event kernel | deterministic | rational time, ordering, cycle/event runners and limits | PASS (C++ API) |
| Queue | unit/property/integration | FIFO/priority/round-robin, overflow policies, pending promotion, traces | PASS (prior baseline suite) |
| Resource | unit/property/integration | leases, capacity, partial release, FIFO waiters, cancellation, traces | PASS (prior baseline suite) |
| CPU simulator | unit/integration | multi-core stream, ROB, issue/retire, dependencies, branch penalty, L1I/L1D, determinism, invalid config | PASS |
| GPU simulator | unit/integration | SM/warp scheduling, occupancy, coalescing, divergence proxy, bank width/conflicts, link accounting, deterministic run | PASS |
| Simulator CLI | smoke | CPU, GPU coalesced and GPU strided command execution | PASS |
| Mobile bridge | host unit | C++ API to compiler/VM/simulator bridge | PASS |
| JNI source | host syntax | C++ JNI bridge compiled with JDK 21 headers and strict warnings | PASS (syntax only) |
| Android workflow | YAML syntax | GitHub Actions workflow parsed by PyYAML | PASS (syntax only) |
| Stress | CPU/GPU | 4M CPU instructions; 4M GPU threads × 1 instruction; same-seed replay | PASS (Linux host only) |
| Android NDK | ARM64 build | Android externalNativeBuild with pinned NDK r30 | NOT RUN (SDK/NDK unavailable) |
| APK | package inspection | ZIP, manifest, ABI, permissions, signature | NOT RUN (no APK generated) |
| Android device | installation/runtime | physical device install, app lifecycle, CPU/GPU UI, logcat | NOT RUN |
| Fuzzing | parser/MASB/runtime | sustained fuzz campaigns and crash corpus | NOT RUN |
| Calibration | CPU/GPU accuracy | compare model output with hardware counters and independent tools | NOT RUN |

Every simulation test uses a fixed seed. Determinism tests compare model
counters across repeated runs; they do not establish physical accuracy. See
[`../simulation/SIMULATOR_0_10_SPEC.md`](../simulation/SIMULATOR_0_10_SPEC.md)
for the model contract and exclusions.
