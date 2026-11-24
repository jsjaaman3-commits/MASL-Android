# PHASE 09 Validation — Bounded Queues and Ownership-Aware Resources

## Implemented scope

### Bounded typed queue (`include/masl/runtime/queue.hpp`)

- Generic `BoundedQueue<T>` with a positive finite item capacity.
- FIFO discipline with stable accepted-item order.
- Priority discipline: greatest signed priority leaves first; equal-priority
  items are stable by original submission sequence.
- Round-robin discipline among at least two unique declared producer names;
  order is cyclic across producers and FIFO within each producer.
- Explicit overflow outcomes: `Block`, `Reject`, and `Drop`.
- `Block` retains a producer operation in a separately bounded pending FIFO.
  A dequeue promotes pending operations automatically into the newly available
  capacity; new submissions cannot overtake already blocked producers.
- Queue capacity counts queued items only; blocked producer operations are
  separately counted and bounded.
- Independent submitted/accepted/blocked/rejected/dropped/dequeued/promoted
  counters, occupancy high-watermark, overflow detection, and bounded transition
  trace storage.
- Invalid producer identities, exhausted IDs, zero capacities, missing
  round-robin producer declarations and invalid limits are rejected explicitly.

### Ownership-aware resource (`include/masl/runtime/resource.hpp`)

- Positive capacity measured in unsigned units.
- Successful acquisitions create unique leases bound to an owner and exact
  acquired amount. Multiple leases per owner are permitted.
- Partial release is supported; zero, over-release, unknown lease, wrong owner,
  and double release are rejected with distinct statuses.
- Explicit acquisition policy: immediate `Reject` or FIFO `Block`.
- Blocked requests are separately bounded and receive unique waiter tickets.
- Releases service waiters immediately in strict FIFO order. The implementation
  deliberately preserves head-of-line fairness and does not let a later small
  request bypass an earlier larger request.
- Grants allocate capacity at the instant they are made and are retrieved once
  with `take_grant(ticket)`. An undelivered grant can be cancelled safely and
  its capacity returned.
- Independent acquisition/block/rejection/release/cancellation/grant counters,
  peak utilization, invariant checks, and bounded transition trace storage.

### Event-kernel integration

Both runtime APIs are exercised from registered Phase 08 `Simulation` event
handlers. Queue enqueue/backpressure/dequeue/promotion and resource
release/grant/resume transitions therefore run in deterministic event time;
they are not simulated by a fake expected-output trace.

## Dedicated test evidence

- Queue suite: 26 named assertions, zero failures.
- Resource suite: 31 named assertions, zero failures.
- Combined existing regression suite: 33 CTest entries.
- Additional hostile/edge cases include queue overflow policy outcomes,
  separate pending capacity, round-robin producer validation, priority ties,
  lease ownership, partial/double/over release, FIFO resource contention,
  cancelled requests and undelivered grants, trace-memory caps, and event-kernel
  integration.

## Build matrix

| Configuration | Compiler | Build | CTest |
|---|---|---:|---:|
| Debug, warnings-as-errors | GCC 14.2 | PASS | 33/33 |
| Release, warnings-as-errors | GCC 14.2 | PASS | 33/33 |
| Debug, warnings-as-errors | Clang 17 | PASS | 33/33 |
| Debug + ASan/UBSan, leak detection | GCC 14.2 | PASS | 33/33 | 

All four complete configurations passed all 33 CTest cases (132/132 total
configuration-specific test executions). The sanitizer run used
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`.

## Integration boundary and limitations

- These are platform-neutral C++20 runtime APIs. No POSIX, Win32, JNI, or
  Apple-specific API is used in the queue/resource implementation.
- Queue and Resource are not yet instantiated from `.masl` declarations by a
  source-to-runtime elaborator. They are currently consumed through the C++ API
  and Phase 08 event handlers.
- `Block` is implemented as a retained pending operation with deterministic
  promotion, not as a coroutine or host thread suspension.
- The runtime is single-thread-owned by a `Simulation`; these containers are
  not internally synchronized and must not be mutated concurrently.
- Transition traces and pending producer/waiter lists are explicitly bounded.
  Applications must size these limits for their model and inspect the dropped
  trace-record counters.
- The Android NDK/ARM64 build and on-device execution are not part of this phase.

Phase 09 is complete for its documented C++ runtime API contract. It is not a
claim that the full MASL hardware language is now executable from source.
