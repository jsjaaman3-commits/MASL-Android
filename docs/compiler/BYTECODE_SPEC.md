# MASB Bytecode Format — 1.0

Status: implemented for the executable pure-function subset of MASL. The binary
container, loader, structural verifier, control-flow/type verifier, disassembler,
and CLI path are implemented. This document is normative for the bytes emitted
by this repository revision; it is not a promise that the current backend can
serialize every MASL declaration.

## 1. Binary invariants

- Magic: four bytes `MASB`.
- Integer byte order: little-endian.
- Format version: major `1`, minor `0` (loader accepts minor versions no newer
  than the implementation's supported minor version).
- Header size: 32 bytes.
- Directory: exactly three entries, each 16 bytes.
- Instruction encoding: exactly 9 bytes (`u8 opcode` followed by `u64 operand`).
- Maximum file size: 64 MiB.
- CRC: CRC-32/IEEE (polynomial `0xEDB88320`, reflected); checksum bytes at
  header offsets 20..23 are treated as zero while calculating the checksum.
- Reserved feature flags must be zero.

The CRC is corruption detection, not authentication. Untrusted MASB must still
pass the complete loader and verifier before execution.

## 2. Header layout

| Offset | Width | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `MASB` |
| 4 | 2 | format_major | `1` |
| 6 | 2 | format_minor | `0` |
| 8 | 4 | header_size | `32` |
| 12 | 8 | total_size | Exact file length in bytes |
| 20 | 4 | crc32 | CRC-32/IEEE over the full file with this field zeroed |
| 24 | 4 | section_count | Exactly `3` |
| 28 | 4 | feature_flags | `0` in format 1.0 |

## 3. Section directory

The directory begins at offset 32. Each entry is:

| Relative offset | Width | Field |
|---:|---:|---|
| 0 | 4 | section_type |
| 4 | 8 | absolute_offset |
| 12 | 4 | byte_length |

Required section IDs are `1 = FUNCTIONS`, `2 = CODE`, and `3 = STRINGS`.
All three sections must exist exactly once, begin after the directory, be
contiguous in ascending physical order, contain no overlap or gaps, and cover
the remainder of the file. Unknown, duplicate, out-of-range, truncated, or
unreferenced sections are rejected.

## 4. Section payloads

All variable-length strings are encoded as `u32 byte_length` followed by that
many raw UTF-8 bytes. The current loader enforces a 1 MiB maximum per string.

### FUNCTIONS section

1. module-name string;
2. source language major (`u16`);
3. source language minor (`u16`);
4. function count (`u32`, maximum 65,535);
5. one record per function, in function-index order:
   - function-name string;
   - parameter count (`u16`);
   - local-slot count (`u16`);
   - return type code (`u8`);
   - verified maximum stack (`u16`);
   - per-function instruction budget (`u32`);
   - code offset relative to CODE (`u32`);
   - code byte length (`u32`);
   - one type-code byte per local slot.

Type codes: `1 = signed 64-bit operational integer`, `2 = IEEE-754 binary64`,
`3 = boolean`, `4 = string`. Parameter slots occupy the first
`parameter_count` local positions. The current lowering maps MASL integer/count/
cycle/byte/time/frequency/latency/bandwidth/address scalar types to operational
code 1 and floating scalar types to code 2. Exact source-width and signedness
semantics are not preserved yet; see Known Limitations.

### CODE section

Function code ranges are contiguous, instruction-aligned, non-overlapping, and
must cover the CODE section exactly. Each instruction is:

- byte 0: opcode number;
- bytes 1..8: unsigned 64-bit little-endian operand bits.

The current opcode assignments are:

| Value | Mnemonic | Operand meaning |
|---:|---|---|
| 1 | PUSH_I64 | Signed integer represented by the operand's two's-complement bits |
| 2 | PUSH_F64 | IEEE-754 binary64 bits |
| 3 | PUSH_BOOL | `0` or `1` |
| 4 | PUSH_STRING | STRINGS table index |
| 5 | LOAD_LOCAL | Local-slot index |
| 6 | STORE_LOCAL | Local-slot index |
| 7 | POP | Reserved operand must be zero |
| 8 | ADD | Reserved operand must be zero |
| 9 | SUB | Reserved operand must be zero |
| 10 | MUL | Reserved operand must be zero |
| 11 | DIV | Reserved operand must be zero |
| 12 | MOD | Reserved operand must be zero |
| 13 | NEG | Reserved operand must be zero |
| 14 | NOT | Reserved operand must be zero |
| 15 | EQ | Reserved operand must be zero |
| 16 | NE | Reserved operand must be zero |
| 17 | LT | Reserved operand must be zero |
| 18 | LE | Reserved operand must be zero |
| 19 | GT | Reserved operand must be zero |
| 20 | GE | Reserved operand must be zero |
| 21 | AND | Reserved operand must be zero |
| 22 | OR | Reserved operand must be zero |
| 23 | JUMP | Absolute instruction index within the current function |
| 24 | JUMP_IF_FALSE | Absolute instruction index; consumes a boolean |
| 25 | CALL | Module function index; arguments are taken from the stack |
| 26 | RETURN | Consumes the function's result |
| 27 | ASSERT | Consumes a boolean; false traps |
| 28 | TRAP | Unconditional invalid-fall-through guard |

All currently reserved operands must be zero. Branch targets refer to
instruction indexes, not byte offsets. Calls are direct and module-local.

### STRINGS section

1. string count (`u32`, maximum 1,000,000);
2. each string as `u32 byte_length` plus raw bytes.

## 5. Compiler lowering boundary

`maslc compile <source.masl> -o <output.masb>` runs lexing, parsing, semantic
analysis, AST-to-IR lowering, IR verification, bytecode generation, MASB
verification, serialization, and output writing.

Format 1.0 currently compiles pure functions and their supported expressions
and statements: integer/floating/boolean/string literals, local names, supported
unary and binary operators, direct function calls, `let`, `set`, `if/else`,
bounded `repeat`, `assert`, `return`, and expression statements. It rejects
unsupported constructs with stable `MASL-B6xxx` diagnostics. It refuses to
serialize a module containing hardware/workload declarations (`MASL-B6023`)
instead of silently dropping them. It rejects a module with no executable
function (`MASL-B6024`). `submit`, field access, physical-unit expressions,
indirect calls, and unsupported function forms are not silently approximated.

## 6. Verification contract

Before serialization and after loading, the verifier checks:

- supported format/module/function metadata and declared resource bounds;
- unique non-empty function names and valid type descriptors;
- instruction alignment, full opcode decoding, and reserved operand rules;
- finite floating-point constants and in-range string/local/function indexes;
- valid branch targets and complete function code ranges;
- direct call graph validity and recursion rejection in MASB 1.0;
- operand stack underflow/height and abstract type propagation;
- local-slot store type and definite initialization at every read;
- parameter/call argument compatibility and return-value compatibility;
- stack agreement at control-flow joins;
- reachable return paths, maximum stack declaration, and fall-through traps.

The loader validates size, magic, version, header, feature flags, total length,
CRC, directory ranges, overlaps/gaps, section structure, and then invokes the
bytecode verifier. The VM independently invokes the verifier before execution.

## 7. CLI

```sh
maslc compile examples/language_basics/executable_functions.masl -o app.masb
masl verify app.masb
maslc disasm app.masb
masl run app.masb answer
masl run app.masb twice 21
```

The current command-line runner accepts scalar integer, floating-point,
boolean, or string entry arguments according to the function signature.
