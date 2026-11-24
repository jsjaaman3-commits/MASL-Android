# PHASE 06 Validation — MASB 1.0

## Implemented scope

- Fixed 32-byte little-endian header, three 16-byte section-directory entries,
  FUNCTIONS/CODE/STRINGS sections, 9-byte instruction encoding.
- CRC-32/IEEE corruption check, 64 MiB file limit, strict section contiguity,
  version/flag/length/range checks.
- Writer, loader, disassembler, source-to-bytecode CLI path, opcode decoding,
  type-aware abstract stack verifier, control-flow merge checks, definite
  local initialization and recursive-call rejection.
- Pure-function backend only. Unsupported hardware/workload declarations are
  rejected with `MASL-B6023`; no data is silently discarded.

## Executed tests

- Dedicated bytecode test executable: 18 assertions, 0 failures.
- End-to-end CLI: compile source to MASB, load/verify MASB, disassemble, and
  execute through Phase 07 runtime.
- Negative coverage: checksum corruption, truncation, invalid magic, directory
  gap, invalid opcode, incompatible local/stack types, invalid control-flow
  merge, recursive call graph, and physical-unit/hardware-scope rejection.
- A CMake integration check verifies that `ArithmeticPipeline.masl` fails with
  `MASL-B6023` and leaves no output artifact.

## Build matrix

| Configuration | Build | CTest |
|---|---:|---:|
| GCC 14.2 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 Release, `-Werror` | PASS | 31/31 |
| Clang 17 Debug, `-Werror` | PASS | 31/31 |
| GCC 14.2 ASan/UBSan, leak detection | PASS | 31/31 |

These are the repository-wide regression counts in which Phase 06 tests are
included, not 31 bytecode-specific unit assertions.

## Explicit non-claims

MASB 1.0 does not yet serialize the complete hardware/experiment object graph,
record layouts or source maps. The Phase 06 exit gate for this revision is the
specified and verified pure-function bytecode slice plus explicit rejection
of unsupported declaration families. Full model serialization remains a
required extension before the complete hardware language path is considered
finished.
