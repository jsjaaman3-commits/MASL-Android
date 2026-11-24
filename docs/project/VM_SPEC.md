# MASL Virtual Machine — Implemented Subset 1.0

Status: implemented and tested as a deterministic, bounded stack-based
interpreter for MASB 1.0 pure functions. It is not yet a hardware event-handler
VM and does not execute `submit`, queues, pipelines, component methods, or
experiment declarations.

## 1. Boundary

The VM executes verified MASB function bodies. It does not parse MASL source,
create hardware instances, own simulation time, dispatch simulation events, or
perform host I/O. The bytecode loader/verifier and VM are separate subsystems.
`masl::vm::VirtualMachine::execute` re-verifies the module before execution.

## 2. Runtime values and types

The operational value representation is:

- `std::int64_t` for MASL integer/count/cycle/byte/time/frequency/latency/
bandwidth/address values that currently lower to integer code 1;
- `double` for floating values (code 2);
- `bool` (code 3);
- `std::string` (code 4).

This operational representation is deliberately bounded and explicit. The
current VM does not preserve all MASL integer widths, unsigned ranges, or f32
rounding semantics. Physical-unit expressions are rejected by the pure-function
backend rather than evaluated after erasing their dimensions.

## 3. Instruction set

The VM implements the MASB 1.0 opcode table documented in
[`../compiler/BYTECODE_SPEC.md`](../compiler/BYTECODE_SPEC.md): constants,
local loads/stores, stack pop, signed/floating arithmetic, comparisons,
boolean logic, branches, direct function calls, returns, assertions, and traps.
Record-field access, aggregate values, indirect calls, native calls, and
simulation scheduling operations are outside this executable subset.

## 4. Invocation and state isolation

Each function invocation creates its own operand stack and local frame.
Arguments initialize the first `parameter_count` local slots. The verifier
checks definite initialization before execution; the VM still checks every
index and runtime type before access/store/return. Calls are direct module-local
calls; recursion is rejected by MASB 1.0 verification.

## 5. Resource limits

`masl::vm::Limits` defaults:

| Limit | Default |
|---|---:|
| Total instructions per top-level execution, including callees | 10,000,000 |
| Maximum nested call depth | 128 |
| Operand stack values per frame | 4,096 |
| String value bytes | 1 MiB |

Each function also carries its own instruction budget (default 1,000,000).
MASB functions are limited to 4,096 locals and a declared maximum stack no
larger than 4,096. The binary file limit is 64 MiB. Exceeding a limit produces
a runtime diagnostic; there is no silent wrap or unbounded recursion.

## 6. Arithmetic and traps

Signed 64-bit add/subtract/multiply/negate/divide/modulo check overflow and
zero divisors. Floating operations reject non-finite constants/results and
floating division by zero. Boolean operators require booleans. Comparisons are
limited to compatible operational values; ordered comparisons require numeric
values. A failed `assert` traps. A reached compiler-generated `TRAP` is an
invalid fall-through condition.

## 7. CLI

```sh
masl verify program.masb
masl run program.masb function_name [scalar-arguments...]
```

Arguments are parsed and checked against the declared runtime value type. The
CLI does not load MASL source directly; source compilation is performed by
`maslc compile`.

## 8. Known boundaries

- No record/array/object value layout or field load/store.
- No source maps or source-level stack traces in MASB 1.0.
- No closure, indirect call, recursion, native extension, shell, network, or
  arbitrary file-access instruction.
- No simulation-specific `submit`/schedule opcode or event callback bridge.
- Integer source widths/signedness are collapsed to signed 64-bit operational
  values, and floating source widths are represented as binary64.
- No persistent VM heap or garbage collector is required by the current scalar
  execution subset; string values are bounded by the configured byte cap.

These are explicit limitations, not implemented features.
