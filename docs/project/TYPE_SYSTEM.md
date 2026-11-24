# MASL Type System — Design Baseline 0.1

Status: PHASE 00 design baseline; a partial implementation exists in PHASE 04. See `docs/compiler/SEMANTIC_ANALYSIS.md` for implemented behavior and limitations. This document remains the intended design, not a claim that every rule below is implemented.

## 1. Goals

The type system prevents accidental mixing of machine integers, addresses,
counts, physical units, and clock-relative quantities. It is static, strong,
nominal for records/enums, and dimension-aware for engineering quantities.

## 2. Type categories

### Scalars

| Type | Domain |
|---|---|
| `bool` | `true`, `false` |
| `i8/i16/i32/i64` | signed fixed-width integers |
| `u8/u16/u32/u64` | unsigned fixed-width integers |
| `f32/f64` | IEEE-754 floating point |
| `string` | immutable UTF-8 text |

### Engineering types

- `time`: rational physical duration.
- `frequency`: positive rational frequency.
- `cycles(clock)`: non-negative integer edge count associated with a named clock.
- `bytes`: non-negative storage quantity.
- `bandwidth(clock)`: bytes per edge in a named clock domain; a future
  bytes/second form is a distinct physical rate.
- `latency`: a duration accepted by a latency-bearing construct; it preserves
  whether it is physical-time or clock-cycle based until elaboration.
- `count`: non-negative quantity of discrete objects/events.
- `address`: unsigned address-space value; arithmetic rules are restricted.

The syntax-level type names `cycles`, `bandwidth`, etc. map to typed internal
representations. Clock identity is part of the type where applicable.

## 3. Dimension rules

- `time + time -> time`; `time - time -> time` when the result is non-negative
  in contexts requiring a duration.
- `cycles(C) + cycles(C) -> cycles(C)`; different clock domains do not add.
- `bytes + bytes -> bytes`.
- `count + count -> count`.
- `frequency * time -> dimensionless ratio` (only an integral edge count can
  be produced by explicit conversion with a rounding policy).
- `bytes / time -> bytes-per-time rate`; `bytes / cycles(C)` is a
  clock-specific bytes-per-cycle rate.
- `address + bytes -> address` with checked range; `address - address` yields
  a byte-distance only where both addresses share an address space.
- `address + address` is invalid.
- A time value cannot be added to a frequency, count, or cycle count.

The initial parser/type checker may implement only the subset required by
clock, pipeline, memory, and workload declarations; unsupported dimensional
combinations must diagnose rather than coerce.

## 4. Numeric conversions

Same-signedness integer widening is allowed only where value-preserving.
Signed/unsigned conversion is not implicitly allowed if the full source domain
is not representable by the destination. Integer-to-float conversion is
explicit or contextually permitted only when the language spec proves the
conversion policy; it must not be silently used for exact addresses/time.
Narrowing always requires explicit checked conversion.

Overflow traps for all integer arithmetic. Integer division truncates toward
zero. `%` requires integer operands and a non-zero divisor. `MIN / -1` traps
because the mathematical result is outside the signed range.

## 5. Literals and inference

Integer literals begin as arbitrary-precision compile-time values, then are
checked against the expected concrete type. This avoids host compiler overflow
while parsing. Decimal literals are exact decimal rationals during constant
analysis where practical; conversion to f32/f64 follows IEEE-754 round-to-nearest,
ties-to-even. String literals decode only the escapes specified in GRAMMAR.md.

A declaration `let x: i32 = 10;` is explicit. Unannotated `let` is not part of
the initial 0.1 grammar.

## 6. Records and enums

Records are nominal immutable-layout value types. A record field has one
statically declared type. Runtime handler code may update a mutable local
record instance through `set`; assignment copies a record value unless an
explicit reference/handle type is introduced in a later revision.

Enums are nominal finite sets. Enum variants do not implicitly convert to
integers or strings.

## 7. Function types and effects

A pure function has typed parameters and one typed return value; it cannot
schedule simulation events, mutate global hardware state, perform I/O, or
access the host environment. A handler has an explicit capability context
provided by the runtime (e.g. the currently owned job record), is instruction-
budgeted, and can only use operations permitted for that handler kind.

The initial implementation will use separate semantic effect categories for
pure function and event handler bytecode. Effect violations are compile-time
errors where statically visible and verifier/runtime errors otherwise.

## 8. Runtime representation

Scalar values use fixed-width representations. Strings are immutable and
bounded by VM memory budget. Record layout is compiler-generated and encoded in
IR/bytecode metadata; runtime values are type-tagged or layout-identified so
untrusted bytecode cannot reinterpret a field with a different type.

Engineering values retain exact rational/integer payloads and unit/domain
identity. They are never stored as an untyped floating-point number.
