# MASL AST and Parser

Status: implemented and tested for the syntax listed below. This is a syntax
front-end only; it does not imply semantic validation or executable simulation.

## Architecture

`masl::parser::Parser` consumes the token stream emitted by
`masl::lexer::Lexer`. It builds an owning AST in `masl::ast`. The AST does not
contain runtime state and does not execute declarations.

Declarations and statements use recursive-descent parsing. Expressions use a
Pratt parser with the following precedence, from lowest to highest:

| Precedence | Operators |
|---:|---|
| 1 | `||` |
| 2 | `&&` |
| 3 | `==`, `!=` |
| 4 | `<`, `<=`, `>`, `>=` |
| 5 | `+`, `-` |
| 6 | `*`, `/`, `%` |
| 8 | Prefix `+`, `-`, `!` |
| 9 | Function call and field access |

Binary operators are left-associative. Function call and field access bind more
tightly than unary and binary operators. Experiment expectation parsing keeps
the comparison operator as the expectation delimiter instead of consuming it
as part of the left-hand arithmetic expression.

## AST families

- Expressions: error, number/string/boolean literal, name, unit literal, unary,
  binary, function call, and field access.
- Statements: typed local declaration, assignment, conditional, bounded repeat,
  queue submit, assertion, return, and expression statement.
- Model declarations: clock, record, enum, queue, resource, pipeline,
  component, component-array instance, workload, experiment, and function.
- Component members: parameter, directional port, queue, resource, pipeline,
  nested instance array, and connection.

All nodes retain a source span. The AST owns expression and statement children
through `std::unique_ptr`; declaration collections use typed `std::variant`
values.

## Contextual names

`output` and `completed` are reserved lexical tokens because they are also
language constructs. The parser accepts them as names in declaration/reference
positions where required by the canonical examples. `completed(...)` remains
available as a built-in expression form for experiment expectations.

## Supported source forms

The parser currently accepts the complete arithmetic pipeline source fixture
in `tests/data/parser_sample.masl`, including:

- `masl major.minor;` and one `model` declaration;
- clocks, records, enums, queues, and resources;
- typed pipelines, timing properties, and `on_start` handlers;
- components, parameters, input/output ports, connections, and fixed instance
  arrays;
- workloads, bounded `repeat`, and `submit` record construction;
- experiments, cycle/event modes, stop conditions, expectations, and exports;
- typed functions and the supported statement/expression forms.

Use `maslc parse file.masl` to run lexical analysis followed by syntax parsing.
A successful result means only that the source is syntactically accepted and
an AST was constructed. No name resolution, static type checking, resource
validation, model elaboration, bytecode generation, or execution happens in
this command yet.

## Diagnostics

- `MASL-E2001`: unexpected syntax / expression token.
- `MASL-E2002`: required name, type, delimiter, or punctuation is missing.
- `MASL-E2003`: source language version does not match `major.minor`.

Diagnostics carry a source span. Error recovery synchronizes at declaration
and statement boundaries to avoid cascading indefinitely after a malformed
construct.

## Current parser limitations

- No imports or multiple source modules.
- No semantic checks for duplicate symbols, unresolved references, component
  ports, graph cycles, or parameter compatibility.
- No type checking, dimensional analysis, constant range checking, or
  narrowing-conversion analysis.
- No parser-driven runtime evaluation; function bodies are AST only.
- No unbounded loops are in the grammar; only bounded `repeat` is present.
- Some grammar productions are intentionally stricter than future extensible
  forms; parser recovery is designed for diagnostics, not AST repair.
