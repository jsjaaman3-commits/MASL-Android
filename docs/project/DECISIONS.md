# MASL Architecture Decision Log

Decisions are append-only. Reversals require a new entry explaining migration.

## ADR-0001 — C++20 and CMake build system

Status: ACCEPTED. The compiler, VM, and runtime are implemented in C++20 with
CMake 3.25+ and CTest. This gives a stable portable baseline and keeps Python
out of the execution core.

## ADR-0002 — Recursive descent plus Pratt expressions

Status: ACCEPTED. Declarations/statements use recursive descent; expressions
use Pratt parsing. The language is intentionally small and benefits from
precise diagnostics and explicit precedence.

## ADR-0003 — Typed IR between semantic analysis and bytecode

Status: ACCEPTED. The simulation runtime must not depend on source AST shape.
The IR is the stable compiler/runtime boundary.

## ADR-0004 — Stack-based MASB VM

Status: ACCEPTED. The first VM is stack-based and verified. LLVM/native code is
not a 1.0 dependency.

## ADR-0005 — Rational simulation time

Status: ACCEPTED. Simulation time is rational femtoseconds with checked
arithmetic. Host floating point is not the authoritative timebase.

## ADR-0006 — Deterministic event key

Status: ACCEPTED. Events sort by `(timestamp, priority, sequence_number)`.
No pointer/hash iteration ordering may affect simulation results.

## ADR-0007 — Pipeline output backpressure ownership

Status: ACCEPTED. A service-completed result remains in-flight until it is
successfully enqueued to the output. A full output queue cannot discard it.

## ADR-0008 — Exact queue round-robin semantics

Status: ACCEPTED. Round-robin is defined across named producers; FIFO is a
separate policy. Queue overflow policies are block, reject, and drop-new.

## ADR-0009 — Offline-capable test foundation

Status: ACCEPTED. GoogleTest is used when already available. The project does
not require network access just to configure/build the foundation; a small
self-contained C++ harness covers the foundation when GoogleTest is absent.

## ADR-0010 — Bytecode integrity format

Status: PROPOSED. MASB v1 uses CRC-32/IEEE for accidental corruption detection.
This is not cryptographic authenticity and does not replace verifier checks.
Freeze the complete header/section layout before the bytecode writer ships.
