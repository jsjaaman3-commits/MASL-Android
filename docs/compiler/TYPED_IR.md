# MASL Typed Intermediate Representation (IR) — v1.0

Status: implemented and tested for the AST and semantic-analysis subset present
in repository version 0.4.0. This is an in-memory compiler IR; it is not the
MASB bytecode format and is not executable by the runtime.

## Purpose

Typed IR is the normalized boundary between semantic analysis and later
compiler backends. It removes parser-owned AST node shapes from downstream
passes while retaining concrete semantic types and source locations. The
lowering pass is deliberately gated: it accepts only a successful
`semantic::Result` and fails if any source expression has no inferred type.

## Pipeline

`maslc ir <file>` executes:

1. Read source bytes.
2. Lex and report source-positioned lexical diagnostics.
3. Parse into the owning AST.
4. Run Phase 04 semantic analysis.
5. Lower AST nodes to `masl::ir::Module`.
6. Run the structural IR verifier.
7. Print the normalized, human-readable Typed IR dump.

No IR is emitted when parsing, semantic analysis, lowering, or verification
fails. `maslc ir` currently writes a diagnostic to stderr and returns non-zero
in these cases.

## Module model

An IR module carries its own schema version (`ir_major=1`, `ir_minor=0`), the
source-language version, the model name, and an ordered declaration list.
Top-level declaration variants cover clocks, records, enums, queues, resources,
pipelines, components, component instances, workloads, experiments, and
functions. Component members have normalized variants for parameters, ports,
queues, resources, pipelines, child instances, and connections.

Record fields, queue element types, pipeline input/output types, component
parameters/ports, and function parameters/return values hold
`semantic::Type` values rather than unvalidated source type-name strings.
Statement-local declarations retain both the source spelling and their
resolved semantic type. All expressions carry their inferred type and source
span. Engineering-unit expressions retain the unit and clock-domain identity.

Expressions cover numeric/string/boolean literals, names, unit literals,
unary/binary operations, direct function calls, and record-field reads. A call
stores its direct target symbol separately from its operands. The current
`completed(pipeline)` experiment intrinsic retains its pipeline argument as a
symbol reference, because a pipeline handle is not yet a first-class runtime
value in the Phase 04 type model.

Statements cover `let`, `set`, `if/else`, bounded `repeat`, `submit`, `assert`,
`return`, and expression statements. Blocks preserve lexical nesting and
source spans. Experiments preserve run configuration, expectations, and export
metadata.

## Structural verifier

`masl::ir::verify` checks the schema version, module/declaration identity,
resolved types, expression operand arity, required call targets, unit metadata,
statement expression requirements, boolean branch/assert conditions, integer
repeat bounds, typed pipeline endpoints/timings, and recursively nested blocks
and component members. Diagnostics use `MASL-I5xxx` codes.

This verifier is a structural safety gate for subsequent compiler passes. It
does not replace semantic analysis, bytecode verification, runtime checks, or a
future whole-program control-flow verifier.

## Supported usage

```sh
maslc ir examples/language_basics/arithmetic_pipeline.masl
maslc ir examples/hierarchy/component_array.masl
```

The output is intended for compiler developers and test inspection. It is not a
stable text interchange format. The in-memory `Module` API is the stable input
contract for the Phase 06 backend within this pre-1.0 repository revision.

## Explicit limitations

- There is no MASB serialization, bytecode generation, VM execution, or
  simulation execution in this phase.
- There is no SSA conversion, basic-block CFG, optimization, constant-folding,
  register allocation, or scheduling pass.
- The IR preserves the current AST's source-level control flow; later phases
  may add a separate lower-level instruction IR for bytecode generation.
- Direct named calls are supported. Indirect/function-value calls are not part
  of the current grammar/type system.
- `completed(pipeline)` uses the symbol-reference representation described
  above; pipeline handles are not general first-class values.
- The textual dump is diagnostic output, not a serialized format with a
  compatibility guarantee.
- The lowering pass depends on Phase 04's current supported semantic subset.
