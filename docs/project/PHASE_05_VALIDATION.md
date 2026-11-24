# Phase 05 — Typed IR Validation

Repository version: 0.4.0
Baseline: `v0.3.0` (`6c0ce82`) on branch `rebuild-from-phase-04`
Scope: AST-to-Typed-IR lowering, IR structural verifier, and `maslc ir`.

## Implemented

- Added `include/masl/ir/ir.hpp` and `src/ir/ir.cpp`.
- Added a versioned in-memory IR module schema (1.0).
- Lowered all current top-level AST declaration variants and current component
  member variants.
- Lowered supported expressions, statements, nested blocks, function bodies,
  workload bodies, pipeline handlers, experiment expectations, and exports.
- Preserved inferred `semantic::Type` values and source spans on expressions.
- Preserved declared semantic types on record fields, queue elements, pipeline
  endpoints, component parameters/ports, function signatures, local `let`
  declarations, and submitted record types.
- Added recursive structural verification with `MASL-I5xxx` diagnostics.
- Added `maslc ir <file>`; invalid source is rejected before IR output.
- Added CTest coverage for arithmetic, nested component hierarchy, invalid
  semantic source, IR shape, function calls, control flow, and verifier errors.

## Phase 04 integration correction

The component-local Queue and Resource capacity validators previously checked
compile-time positivity but did not register their expression types in the
semantic result. Typed IR exposed this omission. The semantic pass now checks
that those expressions are integer-typed and records their inferred types,
matching the existing top-level Queue/Resource behavior.

## Verification matrix

All four matrix entries completed successfully. Each configuration ran all 19
CTest cases with zero failures:

| Configuration | Compiler | Result |
|---|---|---:|
| Debug | GCC 14.2.0 | 19/19 passed |
| Release | GCC 14.2.0 | 19/19 passed |
| Debug + warnings-as-errors | Clang 17.0.0 | 19/19 passed |
| Debug + ASan/UBSan | GCC 14.2.0 | 19/19 passed |

Total: 76 successful CTest executions across four independent build
configurations. The dedicated IR test executable reports 25 assertions with zero
failures. The arithmetic and component-array `.masl` examples both lower through
the CLI; the intentionally invalid semantic fixture is rejected. `git diff
--check` reports no whitespace errors.

## Limitations

Typed IR is not MASB, is not executable, and does not imply that the simulation
runtime exists. There is no CFG/SSA, optimization, IR serialization, code
emission, or VM execution in this phase. See `docs/compiler/TYPED_IR.md`.
