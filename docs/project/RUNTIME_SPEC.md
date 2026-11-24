# MASL Simulation Runtime — Phases 08–09

Status: deterministic simulation-time/event-kernel API and Phase 09 typed
Queue/Resource C++ APIs are implemented and tested. Queue and Resource can be
used from deterministic event handlers, but none of these runtime objects are
yet instantiated from MASL source declarations or connected to the pure-
function VM by a source-to-runtime elaborator.

## 1. Responsibilities and boundary

The current Phase 08 runtime owns exact rational time, clock-period calculation,
event scheduling, deterministic event ordering, handler invocation, basic
schedule/dispatch trace records, event budgets, pending-event capacity, and
cycle/event execution entry points. Phase 09 adds bounded typed queues and
owner-aware finite resources as independent runtime APIs. The runtime still
does not elaborate a hardware topology from source, create component instances,
execute pipelines, run Workload/Experiment declarations, or export complete
traces to JSON/CSV.

## 2. Exact simulation time

`RationalTime` stores an unsigned numerator in femtoseconds and an unsigned
non-zero denominator. `make` reduces the fraction by the greatest common
divisor. Addition and multiplication use checked integer operations and return
`nullopt` on overflow. Comparisons avoid floating point and avoid cross-product
overflow by using a continued-fraction comparison algorithm.

The representation supports fractional femtoseconds. It is exact only while
numerator and denominator fit in `uint64_t`; overflow is surfaced to the caller.

## 3. Clock domains

`ClockDomain::create(name, frequency_hz)` requires a non-empty unique name at
registration and a positive frequency. A period is computed as
`10^15 / frequency_hz` femtoseconds and normalized as a rational. `edge_time(n)`
returns the exact time of edge index `n`; no cumulative floating-point addition
is used.

The current implementation's cycle runner schedules the next edge strictly
after `Simulation::now()` for each registered clock. It does not yet model
clock-domain crossing, phase offsets, gating, resets, or per-clock edge enable.

## 4. Event model and ordering

An `Event` contains exact time, signed 32-bit priority, insertion sequence,
target name, `EventKind`, and signed 64-bit payload. Event kinds currently
include User, ClockEdge, PipelineComplete, QueueRetry, and Timeout; the latter
kinds are identifiers only until later phases add their semantics.

The event calendar is deterministic and ordered by:

1. earlier timestamp first;
2. lower numeric priority first;
3. lower insertion sequence first.

Sequence numbers are assigned monotonically by `schedule`. Scheduling in the
past, an empty target, invalid time denominator, pending-capacity overflow, or
sequence exhaustion is rejected. A handler may schedule additional events;
they enter the same deterministic calendar.

A non-ClockEdge event with no registered target handler fails when dispatched.
ClockEdge events without handlers are permitted. Handler exceptions are caught
and converted into a failed `RunResult` diagnostic.

## 5. Execution modes

- `run_events(until)` dispatches in calendar order and optionally stops before
  the first event later than the requested timestamp.
- `run_cycles(n)` requires at least one registered clock when `n > 0`, plans
  the next `n` edges for every registered clock, validates all times/capacities
  before committing them, schedules ClockEdge events, then dispatches the
  calendar. A zero-cycle request is a successful no-op.

`Simulation` has independent maximum processed-event and maximum pending-event
limits. Both default to 1,000,000. The processing budget is cumulative for the
lifetime of a Simulation instance. A limit result is reported as a failure with
`limit_reached=true`; it is not reported as successful idle completion.

## 6. Trace records

The current in-memory trace records only `schedule` and `dispatch` actions and
copies the associated event. It is an API-level diagnostic trace, not yet a
complete transition trace, counter source, or export format. Deterministic JSON
and CSV export are reserved for Phase 14.

## 7. Threading and determinism

The current event kernel is single-threaded. It does not depend on wall-clock
time or worker scheduling. Parallel dispatch is not implemented. Determinism
is guaranteed for the same sequence of API calls, registration order, and
handler behavior; external mutable state or nondeterministic handler code is
outside the kernel's guarantee.

## 8. Phase 09 queue and resource APIs

`masl/runtime/queue.hpp` provides `BoundedQueue<T>` with FIFO, stable priority,
and declared-producer round-robin selection; explicit Block/Reject/Drop
outcomes; a separately bounded pending-producer FIFO; automatic promotion;
queue statistics; invariant checks; and bounded transition records.
`masl/runtime/resource.hpp` provides finite unit capacity, owner-bound leases,
partial release, explicit Reject/Block acquisition, strict FIFO waiters,
ticket-based one-shot grants, cancellation, utilization counters, invariant
checks and bounded transition records.

Both are single-simulation-thread-owned APIs and are intentionally free of
platform-specific system calls. Their state transitions can be called from
registered Simulation handlers, as verified by integration tests. The queue
and resource objects do not schedule their own host threads or busy-spin.

## 9. Required future integration

Later phases must add typed payload/work-item serialization, component identity,
source-driven queue/resource construction, pipeline state/backpressure,
workload generation, experiment termination, full transition tracing, and
source-to-runtime elaboration. EventKind values reserved for pipeline/queue
operations must not be interpreted as automatic semantics until those
integrations are implemented.
