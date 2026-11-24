# PHASE 04 — Semantic Analysis Validation Report

Date: 2026-11-19  
Repository version: 0.3.0  
Base: Git tag `v0.2.0` (`a05d6c7`)  
Branch: `rebuild-from-phase-04`

## Scope

Phase 04 implements a supported-subset semantic analyzer and static type
checker over the AST produced by Phase 03. The public CLI entry point is:

```sh
maslc check <source.masl>
```

The command performs source loading, lexing, parsing, then semantic analysis.
A successful result means that source conforms to the currently implemented
semantic subset. It does not mean that MASL can compile to executable bytecode
or run a hardware simulation.

## Implemented behavior

- Top-level symbol collection and duplicate-name detection.
- Built-in and nominal type resolution, record/enum validation, duplicate
  properties, and selected field-initialization rules.
- Queue/resource/pipeline checks, including positive compile-time capacities,
  clock and endpoint references, supported policies, record handler inputs,
  timing expressions, and supported type relationships.
- Component signatures and instance-array checks, including component
  references, parameter overrides, required parameters, and selected port and
  child-instance validations.
- Workload scopes, `let`/`set`, conditions, repeat bounds, submit queue/type
  compatibility, and function-only return statements.
- Function resolution, argument count/type checks, return types, and
  recognized return-path validation.
- Expression type inference and checks for supported literals, names, record
  fields, calls, unary/binary operations, numeric ranges, selected unit types,
  and clock-domain compatibility.
- Experiment workload/mode/stop configuration, budgets, expectations, and
  supported exports.

## Test results

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug, warnings-as-errors | GCC 14.2.0 | PASS | 15/15 |
| Release, warnings-as-errors | GCC 14.2.0 | PASS | 15/15 |
| Debug, warnings-as-errors | Clang 17.0.0 | PASS | 15/15 |
| Debug + ASan/UBSan | GCC 14.2.0 | PASS | 15/15 |

Total: 60/60 CTest executions. Semantic unit assertions: 28/28.
GoogleTest was unavailable; the self-contained C++ test harness was used.

### CLI tests

Valid:

```text
Semantic check OK: model ArithmeticPipeline (7 top-level declaration(s)).
Semantic check OK: model ComponentArrayCheck (6 top-level declaration(s)).
```

Invalid zero-capacity queue:

```text
MASL-E3004: tests/data/semantic_invalid.masl:5:20: Queue capacity must be a positive compile-time integer constant.
```

The invalid source exits with status 1.

## Known boundaries

- No typed IR lowering or generic constant-folding pass.
- No complete engineering-unit dimensional algebra or general conversion
  system.
- Component connection declarations are not yet elaborated into a fully
  typed hardware graph; graph-wide connectivity and cycle checks remain future
  work.
- Return-path analysis handles recognized current AST patterns only.
- No MASB bytecode writer, loader, verifier, VM, or runtime integration.
- No executable queue/pipeline/memory/GPU simulation.
- No Android ARM64 build, APK, or MASL Studio application.

## Reproduction

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug -j2
ctest --preset debug

cmake --preset release
cmake --build --preset release -j2
ctest --preset release

cmake -S . -B build/clang-debug -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug \
  -DMASL_BUILD_TESTS=ON -DMASL_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build/clang-debug -j2
ctest --test-dir build/clang-debug --output-on-failure

cmake --preset sanitizers
cmake --build --preset sanitizers -j2
ctest --preset sanitizers
```

The report records only the commands/configurations actually executed in this
working tree. It is not a statement of exhaustive verification.
