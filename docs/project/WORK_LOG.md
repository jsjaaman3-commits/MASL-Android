# MASL Work Log

## 2026-11-18 — Project initialization

- Inspected actual toolchain: GCC 14.2.0, Clang 17.0.0, CMake 3.31.6,
  Ninja 1.12.1, Git 2.47.3, Python 3.13.5.
- GoogleTest was not detected in the system package/header checks.
- Created the PHASE 00 baseline: language contract, EBNF, type dimensions,
  VM boundary, runtime/event rules, standard-library scope, phase plan, and
  architecture decisions.
- Created the PHASE 01 C++20/CMake foundation: core version library, CLI
  launchers, CTest, optional GoogleTest, and offline test fallback.
- Added `maslc lex <file>`; an end-to-end fixture lexes a valid model and a negative fixture confirms malformed units are rejected.
- Implemented PHASE 02 lexer: token kinds, keywords, unit tokens, operators, comments, strings, numeric literal scanning, UTF-8 validation, source spans, and stable lexical diagnostic codes.
- GCC Debug CTest: 8/8 passed. GCC Release CTest: 8/8 passed. Clang 17 Debug CTest: 8/8 passed. GCC ASan/UBSan CTest: 8/8 passed with leak detection and halt-on-error enabled.


## 2026-11-18 — PHASE 03 AST and Parser

- Added owning typed AST nodes for expressions, statements, records, enums,
  queues, resources, pipelines, components, component arrays, workloads,
  experiments, functions, and model declarations.
- Implemented recursive-descent parsing for declarations/statements and Pratt
  parsing for expression precedence, calls, field access, and unary operators.
- Added contextual `output`/`completed` names while preserving
  `completed(pipeline)` expression parsing.
- Added parser diagnostic identifiers MASL-E2001 through MASL-E2003 with
  source locations and declaration/statement recovery.
- Added `maslc parse <file>` and valid/invalid full-model CLI fixtures.
- Parser coverage includes the complete arithmetic example, component hierarchy,
  a fixed instance array, enum/resource/queue forms, clock-relative units,
  function calls/expressions, precedence, associativity, conditional branches,
  missing punctuation, malformed version, and unknown declarations.
- Fixed an expectation parsing bug found by the end-to-end fixture: comparison
  operators are preserved as expectation delimiters.

- Extended parser validation to enums, resources, priority/reject queue policies,
  clock-relative unit types/literals, left associativity, contextual `output`
  references, and malformed multi-component source versions.
- GCC Release exposed a GCC 14 standard-library `istreambuf_iterator`
  `-Wnull-dereference` diagnostic under `-Werror`; source reading now uses a
  stream-buffer copy rather than suppressing the warning.
- Final PHASE 03 validation: GCC Debug 11/11, GCC Release 11/11, Clang 17 Debug
  11/11, and GCC ASan/UBSan 11/11 (44/44 CTest executions).


## 2026-11-19 — PHASE 04 semantic-analysis rebuild

- Created the `rebuild-from-phase-04` branch from tag `v0.2.0`, after confirming
  the 0–3 baseline passed 11/11 CTest cases.
- Implemented a standalone semantic analyzer API and `maslc check <file>` CLI
  path; added `MASL-E3xxx` diagnostics and semantic unit coverage.
- Implemented supported checks for declarations, types, record/enum identity,
  queue/resource/pipeline declarations, component parameters and instance
  arrays, function calls/returns, workload statements, experiment properties,
  expression typing, numeric ranges, and selected engineering units.
- Added semantic-positive `ArithmeticPipeline` and `ComponentArrayCheck` CLI
  fixtures and an invalid queue-capacity fixture.
- Added Phase 04 scope/limitations documentation; updated README, project state,
  master plan, type-system status, known limitations, and changelog.
- Final validation: GCC Debug 15/15, GCC Release 15/15, Clang 17 Debug 15/15,
  and GCC ASan/UBSan 15/15. The semantic executable reported 28/28 assertions.
  Total: 60/60 CTest executions.
- Phase boundary: no typed IR, MASB generation, VM, runtime object creation,
  complete hardware graph elaboration, or Android binary/application is claimed.


## 2026-11-19 — Phase 05 Typed IR

- Cloned the `v0.3.0` Phase 04 bundle and created the Phase 05 working branch
  from the clean rebuild branch.
- Implemented `masl::ir::Module`, typed declarations/expressions/statements,
  AST-to-IR lowering, recursive structural verification, and textual dump.
- Added `maslc ir <file>` and CTest coverage for arithmetic and nested component
  examples; invalid semantic input is rejected before lowering.
- Fixed Phase 04 metadata omission for component-local queue/resource capacity
  expressions by recording and checking their integer semantic types.
- Debug CTest: 19/19 passed after the above corrections.
- Final matrix: GCC Debug 19/19, GCC Release 19/19, Clang 17 Debug 19/19, GCC ASan/UBSan 19/19; 76 successful CTest executions in total.
- Dedicated IR suite: 25 assertions, zero failures.

## 2026-11-19 — PHASES 06–08 executable compiler/runtime slices

- Continued from the provided `v0.4.0` Phase 05 source snapshot and implemented
  MASB 1.0 binary serialization/loading, CRC-32/IEEE, strict section validation,
  disassembly, and control-flow/abstract-type bytecode verification.
- Added `maslc compile` and `maslc disasm`; source modules with unsupported
  hardware/workload declarations are rejected with `MASL-B6023` rather than
  silently reduced to their functions.
- Added a bounded scalar stack VM and `masl verify` / `masl run`; implemented
  checked arithmetic, branches, direct calls, loops, assertion and resource
  limits. The operational type mapping is explicitly documented as a subset.
- Added exact rational time, rational clock edges, stable event ordering,
  event/cycle runners, handler callbacks, limits and schedule/dispatch trace
  records as a standalone C++ runtime kernel.
- Added tests for malformed MASB, verifier edge cases, VM runtime traps,
  deterministic event order, event capacity and rational comparison. Added a
  CLI integration regression that proves unsupported hardware source is
  rejected without producing a bytecode artifact.
- Release GCC surfaced an istreambuf_iterator `-Wnull-dereference` warning;
  the CLI byte reader now uses size-checked `ifstream::read`. Clang surfaced an
  implicit double-to-long-double promotion; the VM now uses an explicit cast.
- Final validation for repository version 0.7.0: GCC Debug 31/31, GCC Release
  31/31, Clang 17 Debug 31/31, GCC ASan/UBSan 31/31. Dedicated assertions:
  bytecode 18/18, VM 17/17, runtime 29/29. Aggregate CTest executions: 124/124.
- Phase boundaries: bytecode/VM support pure functions only; Phase 08 is a C++
  API and is not driven from MASL source. Queue/resource/pipeline/component,
  workload, trace export, GPU, Android Core and MASL Studio remain unimplemented.


## 2026-11-19 — PHASE 09 queues and resources

- Continued from the verified `v0.7.0` commit and advanced the repository to
  `0.9.0` on a dedicated Phase 09 branch.
- Added `BoundedQueue<T>` with FIFO, stable priority and declared-producer
  round-robin selection; explicit Block/Reject/Drop behavior; separately
  bounded pending producer operations; deterministic promotion; counters,
  invariants and bounded transition traces.
- Added Resource with owner-bound unique leases, partial release, strict FIFO
  waiter fairness, bounded waiter capacity, ticket-based one-shot grants,
  pending/grant cancellation, utilization counters, invariants and bounded
  transition traces.
- Added event-kernel integration tests proving that queue backpressure and
  resource grants/resumption occur through scheduled simulation handlers.
- Added Phase 09 test executables and expanded CTest from 31 to 33 cases.
- GCC 14.2 Debug, GCC 14.2 Release and Clang 17 Debug each passed 33/33 CTest.
  Dedicated suites passed Queue 26/26 and Resource 31/31 assertions.
- Completed the full GCC ASan/UBSan build and 33/33 CTest matrix with leak
  detection and halt-on-error. All four complete matrices (GCC Debug, GCC
  Release, Clang Debug and GCC ASan/UBSan) passed 33/33, or 132/132 total.
- Recorded Android-first portability rules and Phase 20 MASL App goals for
  general-purpose application development targeting Android, Windows, Linux
  and macOS. These native app-building capabilities are not implemented yet.
- Phase boundary: queues/resources are C++ runtime APIs and are not yet
  instantiated from MASL source declarations.
