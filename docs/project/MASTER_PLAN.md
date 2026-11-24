# MASL Master Implementation Plan

## Objective

Build MASL as a usable C++20 language toolchain and hardware-simulation system:
source language, compiler, typed IR, verified bytecode, VM, deterministic
simulation runtime, standard hardware components, CLI, tests, documentation,
and Android-first graphical development environment, followed by general-purpose cross-platform application development.

## Engineering invariants

1. Compiler, VM, and simulation runtime remain separate subsystems.
2. Hardware topology is elaborated into persistent instances; work items move
   through those instances and finite resources.
3. Simulation time is exact/deterministic and independent of host wall time.
4. Component capacities and overflow/arbitration policies are explicit.
5. Output backpressure never silently loses pipeline results.
6. Bytecode is verified before execution and has a versioned binary format.
7. Every implemented feature has tests and source-aligned documentation.
8. A phase is not marked fully complete merely because an API or a subset exists.

## Phase gates

| Phase | Scope | Exit evidence | Current status |
|---|---|---|---|
| 00 | Research/design baseline | consistent language/grammar/type/time/VM/runtime specifications | IMPLEMENTED (design artifacts) |
| 01 | Build foundation | CMake builds, CLI version/help, CTest | IMPLEMENTED_AND_TESTED |
| 02 | Lexer | tokens, units, locations, diagnostics, tests | IMPLEMENTED_AND_TESTED |
| 03 | AST/parser | AST, recursive-descent + Pratt parser, tests | IMPLEMENTED_AND_TESTED |
| 04 | Semantics/types | symbol/type checks, diagnostics, negative fixtures | IMPLEMENTED_AND_TESTED (supported subset) |
| 05 | Typed IR | typed AST lowering, structural verifier, tests | IMPLEMENTED_AND_TESTED (current AST subset) |
| 06 | MASB compiler/format/verifier | binary writer/loader, CRC, malformed-file tests, CFG/type verification | IMPLEMENTED_AND_TESTED (pure-function subset; hardware declarations intentionally rejected) |
| 07 | VM | verified stack VM, scalar calls/branches/loops, traps and resource budgets | IMPLEMENTED_AND_TESTED (pure-function subset) |
| 08 | Time/event kernel | rational time, clocks, deterministic calendar, event/cycle APIs and limit tests | IMPLEMENTED_AND_TESTED (C++ API; not source-integrated) |
| 09 | Queues/resources | finite typed queues, leases, contention, backpressure | IMPLEMENTED_AND_TESTED (C++ runtime API; not source-integrated) |
| 10 | Pipelines/interconnect | latency/II, dispatch, links, arbiters, routing | NOT_STARTED |
| 11 | Components/arrays | elaborated hierarchy, ports, connections, graph checks | NOT_STARTED |
| 12 | Memory/cache/DRAM | address mapping, sets/ways/MSHR, banks/timing | NOT_STARTED |
| 13 | Workload/experiment | submission, state transitions, stop conditions, expectations | NOT_STARTED |
| 14 | Trace/profiler | complete transition trace, counters, JSON/CSV, reconciliation | NOT_STARTED |
| 15 | Domain libraries | runnable CPU/GPU/network models and Mini GPU | NOT_STARTED |
| 16 | Quality/security/performance | fuzzing, benchmarks, stress/resource-limit and reproducibility tests | NOT_STARTED |
| 17 | Documentation/package/release | installable cross-platform package and full acceptance suite | NOT_STARTED |
| 18 | Android Core Port | NDK ARM64 compiler/runtime build and on-device validation | NOT_STARTED |
| 19 | MASL Studio | Android editor, project management, compile/run, diagnostics and visualization UI | NOT_STARTED |
| 20 | MASL App / Cross-platform App Toolchain | general-purpose application profile, portable libraries, UI abstraction, native/AOT backends and Android/Windows/Linux/macOS packaging | PLANNED |

## Phase 00 design resolutions

- One model per source file; declarations are collected before reference checks.
- Pratt parser for expressions; recursive descent for declarations/statements.
- Exact rational simulation time in femtoseconds; integer overflow is checked.
- Event ordering: timestamp, priority, sequence number.
- Pipeline latency and initiation interval are independent. `max_in_flight`
  is retained until output enqueue succeeds.
- Queue `round_robin` means round-robin among named producers, not FIFO.
- Integer overflow traps; unsigned operations do not silently wrap.
- MASB checksum is CRC-32/IEEE for corruption detection; structural/type
  verification is also mandatory.
- No native-code execution, shell, network, or arbitrary filesystem access.
- Generic GPU/CPU templates are behavioral models, not claims about undocumented
  silicon.

## Dependencies and implementation order

Phases 02–07 establish the source-to-bytecode path. Phase 08 creates the exact
time/event foundation. Phases 09–14 turn this into an executable hardware
simulation runtime and observability pipeline. Phase 15 supplies verified domain models. Phases 16–17 harden and package the
full product. Phase 18 delivers the Android-first portable core, Phase 19
delivers MASL Studio, and Phase 20 expands MASL into a general-purpose
cross-platform application language and toolchain.

The arithmetic pure-function example is already executable through MASB/VM. Phase 09 now provides C++ runtime APIs for bounded typed queues and ownership-aware resources, including contention and backpressure behavior; these APIs are not yet instantiated from MASL source declarations.
The `ArithmeticPipeline` model is currently parseable, semantically checkable,
and lowerable to Typed IR, but is not yet instantiated or executed by the
simulation kernel. It must not be described as a completed hardware simulation.

## Phase completion policy

A status of `IMPLEMENTED_AND_TESTED (subset)` means that the explicitly listed
subset has an implementation and repeatable tests. It does not satisfy the
complete MASL 1.0.0 acceptance target. Each future phase must publish a precise
supported-syntax contract, positive and negative tests, full integration tests,
and a limitations update.
