# MASL Language Specification — Design Baseline 0.1

Status: **PHASE 00 design baseline; not yet implemented**  
Language identity: `MASL — Mahdi Architecture Simulation Language`  
Source extension: `.masl`  
Bytecode extension: `.masb`  
Compiler command: `maslc`  
Runtime command: `masl`

This document is normative for the initial implementation unless a later
versioned decision explicitly supersedes it. A design rule is not evidence of
an implemented feature; implementation status is tracked in `PROJECT_STATE.md`.

## 1. Scope and non-goals

MASL is a statically typed, deterministic, domain-specific language for
structural and temporal modeling of computer and digital systems. Its core
abstractions are fixed hardware instances, bounded queues, resources,
clocked pipelines, links, memory structures, event handlers, workloads, and
experiments.

MASL 1.0 is not a general-purpose operating-system, web, GUI, or native systems
programming language. It will not execute arbitrary native code. The language
will not claim transistor-level, RTL, gate-level, or cycle accuracy unless a
model explicitly represents the corresponding detail and has validation to
support that claim.

## 2. Core execution model

The architectural principle is:

> Hardware is stationary. Workloads, data, requests and events move.

A model elaborates into a fixed topology of hardware instances. Each instance
owns persistent state: queues, counters, capacity, resource occupancy, and
component-local state. A submitted work item is data; it does not construct a
new hardware unit. A dispatcher/scheduler routes it to an already elaborated
unit. Operations consume modeled time and may contend for finite resources.

A simulation run consists of:

1. Parse and validate the source model.
2. Elaborate component definitions and instance arrays into a topology.
3. Initialize clocks, hardware state, and the workload generator.
4. Execute deterministic events or clock steps.
5. Collect completion state, counters, statistics, and trace records.
6. Evaluate experiment expectations and export permitted artifacts.

Compile-time model construction and run-time workload processing are separate.

## 3. Language identity and compatibility

The initial source header is `masl 0.1;`. The repository package version is
SemVer and may advance independently as implementation milestones are added.
A source version selects language grammar/semantics; an executable version
identifies the compiler/runtime build. A future incompatible language revision
must use an explicit source-version change.

A MASB bytecode file has its own format version, independent of the source
language version and executable package version.

## 4. Source file and declaration rules

- Source text is UTF-8. Identifiers use ASCII letters/underscore initially;
  Unicode identifiers are deferred to avoid normalization ambiguity.
- A source file contains exactly one top-level `model` declaration in 0.1.
- Declarations are collected before reference resolution, so declaration order
  within the model does not affect name visibility, except where an explicit
  initialization/connection dependency is defined.
- Duplicate names in the same namespace are compile-time errors.
- Braces delimit blocks; semicolons terminate declarations and simple
  statements. A trailing semicolon after a block is not required.
- `//` line comments and `/* ... */` block comments are lexical trivia. Nested
  block comments are not supported in 0.1.
- Imports, modules, packages, macros, and dynamic loading are deferred.

## 5. Declaration and execution constructs

The 0.1 grammar includes model, clock, record, enum, queue, resource, pipeline,
component, component instance, workload, experiment, function, and bounded
repeat constructs. Exact formal productions are in `GRAMMAR.md`.

The initial executable subset will be staged: lexical/syntactic constructs
may be accepted by later parser milestones before they become executable.
Compiler diagnostics must distinguish unsupported constructs from malformed
source; an unsupported construct must never be silently ignored.

## 6. Type system

The type system is static and strong. Values do not undergo implicit
reinterpretation. Numeric widening is permitted only where explicitly
specified by the type rules; narrowing conversions require an explicit checked
cast. Record values have a declared field layout. Enum values are nominal and
cannot be mixed with integers without explicit conversion.

Built-in scalar types: `bool`, signed and unsigned 8/16/32/64-bit integers,
`f32`, `f64`, and `string`.

Engineering quantities are dimension-aware values, not decorative suffixes.
They include time, frequency, cycle count tied to a clock domain, byte count,
bandwidth, latency/duration, count, and address. Addition/subtraction require
compatible dimensions. Multiplication/division follow a documented dimension
algebra; dimension combinations not represented by the type system are
rejected. A `cycles(core)` quantity is not interchangeable with `ns` until an
explicit clock-domain conversion is performed.

An unsuffixed integer literal may be contextually typed as a count only in
comparison/expectation contexts where the other operand is a count. It does
not make arbitrary integer/count mixing legal.

## 7. Numeric and failure semantics

- Signed integer overflow: deterministic runtime trap.
- Unsigned integer overflow: deterministic runtime trap; no silent wrap.
- Integer division: truncates toward zero; division by zero traps.
- Floating point: IEEE-754 binary32/binary64 behavior of the target compiler;
  the runtime must not enable fast-math or contraction that changes specified
  operation order. NaN and infinity are representable; ordered comparisons
  follow IEEE-754 rules. Experiments may assert `is_finite` where required.
- Floating-point bit-identical determinism across different CPU architectures
  is not promised in 1.0. Integer and event-order determinism are required.
- Checked conversions trap on out-of-range values. No implicit narrowing.
- All runtime traps carry a MASL diagnostic code and source mapping when the
  operation originated in user bytecode.

## 8. Units and simulation time

The time engine must not use host wall-clock time as simulation time. Wall-clock
runtime is only a performance measurement.

SI duration literals: `fs`, `ps`, `ns`, `us`, `ms`, `s`. Frequency literals:
`Hz`, `kHz`, `MHz`, `GHz`. Binary storage units: `B`, `KiB`, `MiB`, `GiB`.
Decimal and binary byte scales must remain distinct.

A clock declaration has positive frequency. Clock period is represented
exactly as a rational number of femtoseconds. Simulation timestamps use a
normalized rational femtosecond representation with checked integer
arithmetic; no `double` value is the authoritative time representation. The
implementation may use a checked 128-bit intermediate where supported and
must report overflow rather than wrap. Portability fallback behavior must be
specified before claiming support on a compiler without the required integer
width.

Cycle-to-time conversion is exact rational arithmetic. Conversions to integer
output units specify a rounding mode; internal scheduling does not round.

`latency = N cycles(clock)` is an integral number of edges in that named clock
domain. A duration literal such as `4 ns` is a physical-time duration. These
represent different types. A pipeline's latency is one of these duration
forms, but the selected form and clock relationship are validated at compile
and elaboration time.

`bandwidth = N bytes_per_cycle(clock)` is clock-domain-specific. A rate in
bytes/second, if added, is a separate physical-time rate. A conversion requires
the declared clock frequency.

## 9. Queue contract

Every queue has finite capacity. An enqueue operation has one of three
outcomes: accepted, blocked, or rejected/dropped according to its configured
policy. Queue statistics count each outcome separately.

- `fifo`: oldest accepted item leaves first.
- `priority`: highest priority leaves first; ties are stable by insertion
  sequence. Priority must be declared/provided by the item or enqueue action.
- `round_robin`: selects among named input producers in cyclic producer order;
  it is not a synonym for FIFO. A queue using round-robin must have at least
  two declared producer identities at elaboration.
- `block`: preserve the pending producer operation and retry when capacity is
  available. It must not busy-spin or silently discard the item.
- `reject`: fail the submit/enqueue operation with a deterministic result that
  the model can observe.
- `drop`: discard the new item and increment a drop counter. Drop-oldest is
  not implied; it would require a distinct named policy.

Queue capacity counts queued items only unless a particular hardware component
explicitly documents reservation semantics. A pipeline separately accounts
for in-flight and completed-but-output-blocked items.

## 10. Resource contract

A resource has a positive capacity measured in declared slots/units. Acquire
succeeds only if enough capacity is free. Release must match a prior successful
acquire by the same owner/lease. Releasing twice, releasing more than acquired,
or exceeding capacity is a runtime error. A resource's queueing/arbitration
policy is explicit; the runtime may not invent one.

## 11. Pipeline contract

A pipeline has input/output queues, a clock domain, latency, initiation
interval, and maximum in-flight count. Latency and initiation interval are
separate properties.

At a clock edge, eligible queued requests may be admitted while all conditions
hold: the next-accept edge has arrived, an in-flight slot is available, and an
input item exists. An admission executes `on_start` exactly once and schedules
completion at the start timestamp plus configured latency. Initiation interval
constrains admission spacing; latency constrains completion time.

For a pipeline with latency 3 cycles and initiation interval 1 cycle, if the
first item is accepted at cycle 0, subsequent items may be accepted at cycles
0, 1, 2, ... and their nominal completions occur at cycles 3, 4, 5, ... . This
is the baseline convention for the arithmetic acceptance example.

`max_in_flight` counts admitted requests until they have successfully entered
the output queue. A completed result blocked by a full output queue remains
owned by the pipeline, continues consuming an in-flight slot, and is retried
when output capacity becomes available. It is never dropped due to output
backpressure. Output-wait time is measured separately from service latency.

The `on_start(job)` handler mutates the request's pipeline-owned record copy.
It cannot change the already scheduled completion timestamp. More handler
hooks may be added only with explicit event ordering semantics.

## 12. Component and topology contract

A component is a reusable structural template. Its parameter values are
validated and resolved during elaboration. An instance owns independent
mutable state; instances share immutable component definitions and code.

`instances sms: SM[170]` creates 170 instance identities and 170 independent
state sets from one definition. It must not clone source files or generated
C++ classes per instance.

Ports are typed endpoints. Connection validation checks endpoint existence,
input/output direction, record type, multiplicity, and graph constraints.
A component graph may contain cycles only when the cycle is broken by an
explicit queue/buffer or another construct with well-defined temporal storage.
Zero-delay combinational cycles are rejected in the initial implementation.

Distribution policies: `round_robin`, `least_loaded`, `random_seeded`,
`hash_by_field`, `explicit_index`. Random selection must use the experiment's
explicit seed and a specified PRNG algorithm. `least_loaded` tie-breaking is
stable by instance index. Hash policy identifies both field and hash algorithm.

## 13. Event and cycle engines

Event ordering key is `(simulation_timestamp, event_priority, sequence_number)`.
Sequence numbers are monotonically increasing and unique per run. For equal
timestamp and priority, lower sequence executes first. Event IDs are stable
within one run but are not promised across semantically changed programs.

The initial event priority table is:

| Priority | Event class | Purpose |
|---:|---|---|
| 0 | clock edge / completion publication | complete work whose service time has elapsed |
| 10 | external workload submission | make generated requests visible |
| 20 | arbitration / admission | select work and start eligible operations |
| 30 | observation / experiment sampling | capture end-of-edge counters and assertions |

If a future feature needs another phase, the table is versioned and tested.
Events scheduled at the current timestamp into a priority already passed are
moved to the next legal delta phase or rejected; the precise mechanism must be
settled before implementation of zero-time handlers.

Cycle-stepped execution visits configured clock edges and advances state
according to the same event-priority contract. Event-driven execution jumps to
the next pending timestamp. For models whose components use only declared
event/edge semantics, both modes must produce equivalent architectural
completion and statistics results.

Clock-domain crossing requires an explicit synchronization/bridge model in
which sampling edge and latency are stated. No hidden asynchronous behavior is
inferred from connecting different clocks.

## 14. VM and bytecode boundary

The compiler and simulator runtime are separate subsystems. The compiler
parses, validates, lowers, compiles, and verifies. The simulation runtime
owns topology, time, events, queues, resources, and statistics. The MASL VM
executes verified user-defined pure functions and bounded handlers under a
budget; it does not own global simulation time.

The initial VM is stack-based. Every function has declared input/output types,
local-slot count, maximum stack depth, instruction budget, and source map.
Recursion is prohibited in 1.0 unless a later explicit bounded-recursion
feature is designed. `repeat` has a statically known finite upper bound.

MASB files use a fixed magic, bytecode format version, endianness marker,
feature flags, section directory, length fields, and checksum. The loader
validates all offsets/lengths before allocation. The verifier checks opcode,
operand type, stack depth at each control-flow merge, branch targets, constant
indices, function indices, and source-map ranges. No native-code payload is
accepted.

The checksum algorithm selected for the first binary format is CRC-32/IEEE;
it detects accidental corruption, not malicious tampering. Bytecode is
untrusted input and still requires full structural verification.

## 15. Sandbox and output

MASL bytecode cannot invoke shell commands, spawn processes, open arbitrary
paths, access network sockets, or load native libraries. Runtime exports are
limited to explicit experiment export declarations and a configured output
root. Resolved output paths must remain inside that root after normalization;
symlink handling must not permit escape. The runtime has explicit limits for
instructions per handler, call depth, events, work items, memory, queue
capacity, and trace size.

## 16. Trace and statistics

Trace records represent actual runtime transitions and include a simulation
timestamp, sequence, event kind, component/instance identity, work-item ID,
and relevant queue/resource/pipeline details. Trace output is not fabricated
from expected results. JSON and CSV schemas are separately versioned.

Counters include submitted, accepted, blocked, rejected, dropped, started,
completed, failed, occupancy samples, wait cycles/time, service latency,
output wait, and utilization where meaningful. Every metric defines its
numerator, denominator, sampling points, and unit.

## 17. Diagnostics

Diagnostic IDs are stable and categorized:

- `MASL-E1xxx`: lexical
- `MASL-E2xxx`: syntax
- `MASL-E3xxx`: semantic/elaboration
- `MASL-E4xxx`: type/unit
- `MASL-E5xxx`: compiler/IR
- `MASL-E6xxx`: bytecode/verification
- `MASL-E7xxx`: VM/runtime
- `MASL-E8xxx`: simulation
- `MASL-E9xxx`: I/O/configuration

A diagnostic includes code, severity, file, source span, concise reason, and
optional suggestion. Runtime diagnostics include the originating source span
when available.

## 18. Version 1.0 release boundary

The release requirements are the acceptance criteria enumerated in the master
prompt and summarized in `MASTER_PLAN.md`. A feature is complete only when
implemented, covered by tests, and documented against the implementation.
A design-only item is not complete. A passing build alone is not evidence that
a language feature works.
