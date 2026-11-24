# PHASE 08 Validation — Rational Time and Event Kernel

## Implemented scope

- Normalized rational time in femtoseconds.
- Checked addition/multiplication and exact comparison without cross-product
  overflow.
- Exact clock period and edge time calculation from positive frequency.
- Event calendar ordered by timestamp, numeric priority (lower first), and
  insertion sequence (earlier first).
- Handler registration and deterministic event/cycle runners.
- Independent processed-event and pending-event limits.
- In-memory schedule/dispatch trace and handler-exception diagnostics.

## Executed tests

- Dedicated runtime test executable: 29 assertions, 0 failures.
- Fraction comparator checked against cross-product ordering for 176,400
  bounded rational pairs.
- 3 GHz clock test confirms exact fractional femtosecond period and edge time.
- Event tests cover same-time priorities, insertion tie-break, handler-created
  events, cumulative processing limit, pending limit, repeated cycle calls,
  missing handler, thrown handler, no-clock cycle mode and zero-cycle no-op.

## Build matrix

| Configuration | Build | CTest |
|---|---:|---:|
| GCC 14.2 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 Release, `-Werror` | PASS | 31/31 |
| Clang 17 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 ASan/UBSan, leak detection | PASS | 31/31 |

## Integration boundary

Phase 08 delivers an independently testable C++ kernel. It is not yet
instantiated from MASL source; it does not provide typed work items, hardware
instances, queue/resource/pipeline semantics, complete transition traces or
JSON/CSV export. Those are later phases and are required before claiming a
working MASL hardware simulator.
