# MASL PHASE 00–02 Validation Report

Date: 2026-11-18  
Repository version: 0.1.0  
Host: Linux x86_64

## Scope completed

- PHASE 00: language design baseline, EBNF grammar, type system, exact-time
  policy, VM/bytecode boundary, runtime/event semantics, standard-library
  scope, architecture decisions, and release plan.
- PHASE 01: C++20/CMake project, version library, `maslc` and `masl` CLI
  foundations, Debug/Release/Sanitizer presets, CTest and offline test harness.
- PHASE 02: Lexer, token kinds, source spans, keyword/unit recognition, number
  and string scanning, comments, operators, UTF-8 validation, diagnostics, and
  the `maslc lex <file>` command.

## Build/test matrix

| Configuration | Compiler | Build | CTest |
|---|---|---|---|
| Debug | GCC 14.2.0 | PASS | 8/8 PASS |
| Release | GCC 14.2.0 | PASS | 8/8 PASS |
| Debug | Clang 17.0.0 | PASS | 8/8 PASS |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 8/8 PASS |

Total: 32 successful CTest executions across four configurations. The test
executables print 30 successful named assertions (26 lexer and 4 version /
foundation assertions). No GoogleTest package was detected; the tested path uses
the repository's self-contained C++ test harness.

## Functional checks

- `maslc --version`, `maslc --help`, `masl --version`, and `masl --help` work.
- `maslc lex tests/data/lexer_sample.masl` lexes a valid source file and emits
  line/column token output.
- `maslc lex tests/data/lexer_invalid.masl` exits 1 and reports
  `MASL-E1005` for the adjacent unknown suffix `3widgets`.
- `maslc check ...` and `masl run ...` return exit code 2 with an explicit
  not-implemented message; they are not claimed to work.

## Not yet implemented

Parser/AST, semantic analysis, type checker, IR, bytecode compiler and verifier,
VM, simulation time/event runtime, queues/resources/pipelines, component
hierarchy, memory/cache/DRAM, experiments, trace/profiler, GPU/CPU library, and
executable end-to-end simulation programs. Accordingly, MASL 1.0.0 is not
complete and has not been declared.
