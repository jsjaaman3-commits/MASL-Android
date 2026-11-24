# Lexer API — current implementation

Header: `masl/lexer/lexer.hpp`  
Namespace: `masl::lexer`

The lexer owns a copy of source text and produces `LexResult`, containing an
ordered token vector (always terminated by `EndOfFile`) and lexical
diagnostics. Each token stores its kind, exact source lexeme, and a half-open
source span with byte offset plus 1-based line/column. Columns count UTF-8 code
points; byte offsets count source bytes.

Example:

```cpp
#include "masl/lexer/lexer.hpp"

const auto result = masl::lexer::Lexer(source_text).scan();
for (const auto& diagnostic : result.diagnostics) {
  // Format diagnostic.code, diagnostic.message and diagnostic.span.
}
```

The current token set covers the 0.1 declaration/statement keywords,
identifiers, integers/decimal/exponent numbers, strings, engineering units,
comments, delimiters, arithmetic/comparison/logical operators, and `->`.
Recognized unit tokens are `fs`, `ps`, `ns`, `us`, `ms`, `s`, `Hz`, `kHz`,
`MHz`, `GHz`, `B`, `KiB`, `MiB`, `GiB`, `cycles`, and `bytes_per_cycle`.

Lexical diagnostics currently include `MASL-E1001` invalid character/operator,
`MASL-E1002` malformed numeric literal, `MASL-E1003` malformed/unterminated
string, `MASL-E1004` unterminated block comment, `MASL-E1005` unknown adjacent
unit suffix, and `MASL-E1006` invalid UTF-8. File names are supplied by the
future compiler diagnostic layer; the lexer itself is source-buffer based.

The lexer is single-use. Identifier syntax is ASCII-only in this language
revision; UTF-8 is validated across the whole source and accepted inside
strings/comments. Unicode identifiers are deferred.
