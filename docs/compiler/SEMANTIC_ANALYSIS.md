# MASL Semantic Analysis — Phase 04

Status: implemented for the currently supported AST subset; not a complete
specification or a claim of full language conformance.

## Pipeline

`maslc check <file>` performs source loading, lexing, parsing, then semantic
analysis. The analyzer reports diagnostics with source spans and stable
`MASL-E3xxx` codes. It returns expression-type information for expressions it
successfully analyzes. Syntax errors prevent semantic analysis from proceeding.

## Implemented checks

- Collects top-level declarations before resolving references, allowing
  forward references for the declaration categories currently supported.
- Detects duplicate top-level declarations, duplicate record fields, enum
  variants, and duplicate properties in the supported AST declarations.
- Resolves built-in scalar/engineering types and nominal record/enum types.
- Validates empty records/enums, field types, and selected record initialization
  and mutation rules.
- Checks queue/resource capacities and supported policy/unit values.
- Checks pipeline clock references, endpoints, record handler inputs, timing
  values, and supported queue/port type relationships.
- Builds component signatures before validation so tested forward component
  references and instance arrays do not depend on declaration order.
- Checks component parameter overrides, required parameters, queue/resource
  declarations, child instances, and supported port directions.
- Checks workload scopes, `let`/`set`, boolean conditions, bounded `repeat`,
  submit queue and record compatibility, and function-only `return` use.
- Checks function references, argument counts/types, return types, and
  recognized return paths.
- Infers expression types for supported literals, names, field reads, calls,
  unary/binary operations, and engineering-unit literals. It applies selected
  integer/float bounds and clock-domain checks.
- Checks experiment workload/mode/stop configuration, positive budgets,
  `completed(...)` expectations, and supported exports.

## Type model

The analyzer distinguishes booleans, strings, signed and unsigned bounded
integers, floating-point types, nominal records/enums, and engineering types
including time, frequency, cycles, bytes, bandwidth, latency, count, and
address. Record and enum types are nominal. Integer signedness is not silently
converted. Cycle and bandwidth values retain their declared clock domain.
Some engineering types can be used in the limited contexts currently
implemented; this is not yet a general dimensional-analysis system.

## Explicit limitations

- This phase validates AST declarations; it does not instantiate runtime
  objects or execute a simulation.
- Component connection syntax is not yet a fully elaborated, typed hardware
  graph. Graph-wide connectivity, cycle detection, and elaboration belong to
  later phases.
- The unit system has limited compatibility rules; arbitrary unit products,
  quotients, conversions, and rational dimensions are not implemented.
- Compile-time evaluation is deliberately limited to supported constant and
  range expressions; there is no general constant-folding pass.
- Control-flow return analysis handles only the recognized AST patterns; it is
  not a full theorem of path completeness for arbitrary future control flow.
- Diagnostics and accepted grammar will evolve before MASL 1.0.0.

## CLI examples

```sh
maslc check examples/language_basics/arithmetic_pipeline.masl
maslc check examples/hierarchy/component_array.masl
```


## Phase 05 integration correction

The Phase 05 Typed IR pass requires an inferred type for every value expression.
During integration, the component-local queue and resource capacity validators
were found to perform constant-positivity checks without recording inferred
expression types. They now also run integer type checks and retain their inferred
types in `Result::expression_types`, matching top-level queue/resource behavior.
