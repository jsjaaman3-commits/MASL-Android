# MASL PHASE 00–03 Validation Report

Date: 2026-11-18  
Repository version: 0.2.0  
Language source version: `masl 0.1;`  
Host: Linux x86_64

## Scope completed

- **PHASE 00 — Blueprint:** language scope, EBNF baseline, static type and
  engineering-unit direction, exact simulation-time policy, Compiler/VM/
  Runtime boundary, event-ordering policy, queue/pipeline semantics, standard
  library scope, and implementation plan.
- **PHASE 01 — Build foundation:** C++20 project, CMake presets, static core
  library, `maslc` / `masl` CLI entry points, CTest, optional GoogleTest
  integration, and an offline self-contained test harness.
- **PHASE 02 — Lexer:** tokenization, numeric and unit suffixes, comments,
  strings and escapes, operators, UTF-8 validation, source spans, and stable
  lexical diagnostics.
- **PHASE 03 — AST and Parser:** owning typed AST, recursive-descent declaration
  and statement parser, Pratt expression parser, parser diagnostics and
  recovery, and `maslc parse`.

## Build and test matrix

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug | GCC 14.2.0 | PASS | 11/11 PASS |
| Release | GCC 14.2.0 | PASS | 11/11 PASS |
| Debug | Clang 17.0.0 | PASS | 11/11 PASS |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 11/11 PASS |

Total: **44/44 successful CTest executions** across four independent build
configurations. All normal Debug/Release/Clang builds used warnings-as-errors.
The sanitizer run used leak detection and halt-on-error. GoogleTest was not
available in the environment, so the self-contained C++20 test harness was
used; the optional GoogleTest branch is present but was not exercised.

The three C++ test executables report **68 successful named assertions** in
the current Debug build: foundation/version, lexer, and parser/AST assertions.
The CTest total also includes CLI positive and negative fixtures.

## Parser acceptance evidence

- `maslc parse examples/language_basics/arithmetic_pipeline.masl` exits 0 and
  prints `Syntax OK: model ArithmeticPipeline (7 top-level declaration(s)).`
- The parsed model has a clock, a four-field record, two queues, a pipeline
  with clock-relative latency and initiation interval, a workload with bounded
  repeat/submit, and an experiment with `completed(alu)` expectation and two
  export declarations.
- Component tests parse typed input/output ports, a queue, nested pipeline,
  connection, and a fixed two-element component array.
- Additional tests cover enums, resources, queue policies, `cycles(clock)`,
  `bytes_per_cycle(clock)`, function AST, unary/binary expressions, operator
  precedence and associativity, conditional branches, contextual names,
  malformed versions, and syntax recovery.
- The invalid parser fixture exits 1 and reports `MASL-E2002` at line 4,
  column 1 for a missing declaration semicolon.

## Implemented commands

- `maslc --help`
- `maslc --version`
- `maslc lex <file>`
- `maslc parse <file>`
- `masl --help`
- `masl --version`

`maslc parse` performs lexical analysis and syntax parsing, and constructs an
AST. It does **not** perform semantic analysis, type checking, bytecode
emission, or simulation execution. The `masl` executable is a CLI foundation
stub; `masl run` is not implemented.

## Not implemented

Semantic analysis, static type checking and dimensional analysis, typed IR,
constant folding, MASB bytecode writer/loader/verifier, VM interpreter, exact
simulation time and event runtime, clock domains, queues/resources/pipeline
execution, component elaboration/distribution, interconnect, cache/DRAM,
workload/experiment execution, trace export, profiler, GPU/CPU/network standard
library, and actual end-to-end model execution.

**Release status:** repository version 0.2.0 is a tested syntax-front-end
checkpoint. MASL 1.0.0 is not complete and has not been declared.
