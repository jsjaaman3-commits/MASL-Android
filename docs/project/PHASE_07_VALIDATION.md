# PHASE 07 Validation — MASL Stack VM

## Implemented scope

The VM executes a verified MASB module and selected entry function. Runtime
values are signed-64 operational integers, binary64 floating values, booleans
and strings. Instructions include literal pushes, local load/store, scalar
arithmetic, comparisons, boolean logic, branches, direct calls, return,
assert, and trap.

Resource safeguards include per-function and total instruction budgets,
maximum call depth, per-frame stack value cap and string byte cap. Signed
integer arithmetic is checked; integer/floating divide-by-zero and invalid
floating results trap. The VM verifies the module before execution and checks
runtime argument/store/return types.

## Executed tests

- Dedicated VM test executable: 17 assertions, 0 failures.
- CLI tests exercise `masl verify`, no-argument entry function, parameterized
  call returning 42, bounded sum loop, branch behavior and wrong argument
  rejection.
- Negative cases cover invalid function, argument count/type, instruction
  budget exhaustion, overflow, division by zero, failed assertion and
  floating-point division by zero.

## Build matrix

| Configuration | Build | CTest |
|---|---:|---:|
| GCC 14.2 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 Release, `-Werror` | PASS | 31/31 |
| Clang 17 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 ASan/UBSan, leak detection | PASS | 31/31 |

## Explicit non-claims

The VM does not implement record/array/object values, field access, indirect
calls, recursion, native extensions or simulation operations. Source integer
widths/unsigned semantics and f32 rounding are not preserved by the current
operational value representation. This phase is not a hardware simulation
engine; the separate Phase 08 event kernel is not yet connected to VM bytecode.
