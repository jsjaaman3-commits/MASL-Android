# MASL Grammar — EBNF Baseline 0.1

Status: PHASE 00 design baseline, updated against the PHASE 03 parser
implementation. The grammar is intended for a hand-written recursive-descent
parser with Pratt expression parsing. See
`docs/language/AST_AND_PARSER.md` for implemented behavior and limitations.
Terminals are quoted. `{ X }` means zero or more repetitions and `[ X ]` means
optional.

## 1. Lexical conventions

```ebnf
letter          = "A"…"Z" | "a"…"z" | "_" ;
digit           = "0"…"9" ;
identifier      = letter, { letter | digit } ;
name            = identifier | "output" | "completed" ;  (* contextual names; see note below *)
integer         = digit, { digit | "_", digit } ;
real            = integer, ".", integer, [ exponent ] | integer, exponent ;
exponent        = ("e" | "E"), [ "+" | "-" ], integer ;
number          = integer | real ;
string          = '"', { string-char | escape }, '"' ;
escape          = "\\", ( '"' | "\\" | "n" | "r" | "t" | "0" ) ;
line-comment    = "//", { any-character-except-newline } ;
block-comment   = "/*", { any-character-except-terminator }, "*/" ;
```

Whitespace and comments are ignored between tokens. A minus sign is a unary
operator, not part of the numeric token. Source positions are 1-based line and
column positions; spans are half-open `[start, end)`.

## 2. Program and declarations

```ebnf
program             = language-version, model-declaration, EOF ;
language-version    = "masl", version-number, ";" ;
version-number      = number ;  (* semantic form: major.minor, no exponent *)
model-declaration   = "model", name, "{", { model-member }, "}" ;
model-member        = clock-declaration
                    | record-declaration
                    | enum-declaration
                    | queue-declaration
                    | resource-declaration
                    | pipeline-declaration
                    | component-declaration
                    | instance-declaration
                    | workload-declaration
                    | experiment-declaration
                    | function-declaration ;

clock-declaration  = "clock", identifier, "=", frequency-literal, ";" ;
record-declaration = "record", identifier, "{", { field-declaration }, "}" ;
field-declaration  = name, ":", type, ";" ;
enum-declaration   = "enum", identifier, "{", identifier,
                     { ",", identifier }, [ "," ], "}" ;

queue-declaration  = "queue", name, ":", type, "{",
                     "capacity", "=", expression, ";",
                     "discipline", "=", queue-discipline, ";",
                     "overflow", "=", overflow-policy, ";",
                     "}" ;
queue-discipline   = "fifo" | "priority" | "round_robin" ;
overflow-policy    = "block" | "reject" | "drop" ;

resource-declaration = "resource", name, "{",
                       "capacity", "=", expression, ";",
                       "unit", "=", identifier, ";",
                       "}" ;
```

The initial grammar permits exactly one capacity, discipline, and overflow
property per queue; duplicate or missing properties are semantic errors.

## 3. Pipelines, handlers, and components

```ebnf
pipeline-declaration = "pipeline", name, ":", type, "->", type,
                       "{", { pipeline-property | handler }, "}" ;
pipeline-property   = "input", "=", name, ";"
                    | "output", "=", name, ";"
                    | "clock", "=", identifier, ";"
                    | "latency", "=", duration-literal, ";"
                    | "initiation_interval", "=", cycle-literal, ";"
                    | "max_in_flight", "=", expression, ";" ;
handler             = "on_start", "(", identifier, ")", block ;

component-declaration = "component", identifier, "{",
                        { component-member }, "}" ;
component-member     = parameter-declaration
                     | port-declaration
                     | queue-declaration
                     | resource-declaration
                     | pipeline-declaration
                     | instance-declaration
                     | connection-declaration ;
parameter-declaration = "param", name, ":", type, "=", expression, ";" ;
port-declaration      = ("input" | "output"), "port", name,
                        ":", type, ";" ;
connection-declaration = "connect", endpoint, "->", endpoint, ";" ;
endpoint               = name, { ".", identifier } ;

instance-declaration = "instances", name, ":", identifier,
                       "[", expression, "]", "{", { assignment }, "}" ;
```

The names `output` and `completed` are contextual identifiers in
 declaration/reference positions (for example, `queue output: Job`,
`output = output;`, and `output port completed: Job`). In expression position,
`completed(...)` is parsed as the built-in experiment query. The lexer retains
reserved keyword tokens; the parser accepts them as names only in name
positions. The compact form `input port jobs: Job;` and
`output port done: Job;` is the canonical port spelling. Array size and
component parameter expressions must be compile-time constants in 0.1.

## 4. Workloads and experiments

```ebnf
workload-declaration = "workload", name, block ;
experiment-declaration = "experiment", name, "{",
                         { experiment-property }, "}" ;
experiment-property = "workload", "=", identifier, ";"
                    | "mode", "=", ("cycle" | "event"), ";"
                    | "stop_when", "=", ("idle" | "max_cycles"), ";"
                    | "max_cycles", "=", expression, ";"
                    | "max_events", "=", expression, ";"
                    | "seed", "=", expression, ";"
                    | expectation
                    | export-declaration ;
expectation         = "expect", expression, comparison-operator,
                      expression, ";" ;
export-declaration = "export", ("trace" | "stats" | "csv"),
                     "=", string, ";" ;
```

## 5. Statements and functions

```ebnf
block               = "{", { statement }, "}" ;
statement           = let-statement
                    | set-statement
                    | if-statement
                    | repeat-statement
                    | submit-statement
                    | assert-statement
                    | return-statement
                    | expression-statement ;
let-statement       = "let", name, ":", type, "=", expression, ";" ;
set-statement       = "set", lvalue, "=", expression, ";" ;
if-statement        = "if", expression, block,
                      [ "else", (block | if-statement) ] ;
repeat-statement    = "repeat", expression, "as", name, block ;
submit-statement    = "submit", name, ",", identifier,
                      "{", { assignment }, "}", ";" ;
assignment          = name, "=", expression, ";" ;
assert-statement    = "assert", expression, ";" ;
return-statement    = "return", [ expression ], ";" ;
expression-statement = expression, ";" ;
lvalue              = name, { ".", name } ;

function-declaration = "fn", name, "(", [ parameter-list ], ")",
                       "->", type, block ;
parameter-list       = parameter, { ",", parameter } ;
parameter            = name, ":", type ;
```

The `completed` token is a built-in experiment query and may be called as
`completed(pipeline_name)` in an expectation expression. The source-version
header `masl 0.1;` is tokenized as one decimal-number token; semantic analysis
requires exactly two unsigned decimal components and rejects exponents.

`repeat` bounds must be statically bounded by a non-negative integer constant
or a compile-time expression with a proven finite bound. The loop variable is
read-only within the loop body. `return` is required on all non-void function
paths; void functions are deferred from the first executable subset.

## 6. Types and literals

```ebnf
type            = scalar-type | engineering-type | identifier ;
scalar-type     = "bool" | "i8" | "i16" | "i32" | "i64"
                | "u8" | "u16" | "u32" | "u64"
                | "f32" | "f64" | "string" ;
engineering-type = "time" | "frequency" | "cycles" | "bytes"
                 | "bandwidth" | "latency" | "count" | "address" ;

time-literal       = number, time-unit ;
frequency-literal  = number, frequency-unit ;
byte-literal       = number, byte-unit ;
cycle-literal      = expression, "cycles", "(", identifier, ")" ;
duration-literal   = time-literal | cycle-literal ;
bytes-per-cycle    = number, "bytes_per_cycle", "(", identifier, ")" ;
time-unit          = "fs" | "ps" | "ns" | "us" | "ms" | "s" ;
frequency-unit     = "Hz" | "kHz" | "MHz" | "GHz" ;
byte-unit          = "B" | "KiB" | "MiB" | "GiB" ;
```

The lexer recognizes a numeric token followed by a known unit token; semantic
analysis validates its dimensional use and referenced clock.

## 7. Expressions (Pratt parser)

The following table is highest-to-lowest precedence. Operators at a row are
left-associative except unary operators and conditional branching.

| Precedence | Operators / forms |
|---:|---|
| 90 | function call `f(...)`, field access `.`, parenthesized expression |
| 80 | unary `!`, `-`, `+` |
| 70 | `*`, `/`, `%` |
| 60 | `+`, `-` |
| 50 | `<`, `<=`, `>`, `>=` |
| 40 | `==`, `!=` |
| 30 | `&&` |
| 20 | `||` |

```ebnf
expression       = primary, { postfix } ;
primary          = number | string | "true" | "false" | identifier | "completed"
                 | "(", expression, ")" ;
postfix          = call-suffix | field-suffix ;
call-suffix      = "(", [ argument-list ], ")" ;
field-suffix     = ".", identifier ;
argument-list    = expression, { ",", expression } ;
comparison-operator = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
```

The expression EBNF above is intentionally schematic: binary and unary
operators are parsed by Pratt binding powers from the precedence table. The
implemented parser uses numeric binding powers with the same relative order.
Calls and field access bind tighter than unary operators. Assignment is not an
expression. Short-circuit behavior for `&&`/`||` is a runtime requirement and
is not yet implemented by an evaluator. Parser behavior is documented in
`docs/language/AST_AND_PARSER.md`.

## 8. Lexical and syntactic error rules

- Invalid UTF-8 is a lexical error before tokenization.
- Unterminated strings/comments report the opening delimiter location.
- A malformed exponent or numeric separator is a lexical error.
- Unknown unit suffixes are not silently split into a valid number plus an
  identifier if they are adjacent with no whitespace; they receive a targeted
  unknown-unit diagnostic.
- Parser recovery synchronizes at `;` or a declaration-closing `}` while
  guaranteeing forward progress.
- Parser diagnostics do not cascade indefinitely after one malformed token.

## 9. Deferred grammar

Import/module declarations, generic type parameters, optional types, arrays as
first-class data values, dynamic loops, while loops, recursion, pattern
matching, macros, user-defined operator overloading, and native FFI are out of
scope for source version 0.1/initial 1.0 unless formally added by a language
revision.
