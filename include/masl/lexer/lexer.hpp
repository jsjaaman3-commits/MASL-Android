#pragma once

#include "masl/lexer/token.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace masl::lexer {

struct LexDiagnostic final {
  std::string code;
  std::string message;
  SourceSpan span{};
};

struct LexResult final {
  std::vector<Token> tokens;
  std::vector<LexDiagnostic> diagnostics;

  [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class Lexer final {
 public:
  explicit Lexer(std::string_view source);

  [[nodiscard]] LexResult scan();

 private:
  [[nodiscard]] bool at_end() const noexcept;
  [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
  [[nodiscard]] char advance() noexcept;
  [[nodiscard]] bool match(char expected) noexcept;
  [[nodiscard]] SourcePosition position() const noexcept;
  [[nodiscard]] SourceSpan span_from(SourcePosition start) const noexcept;
  void skip_trivia();
  void scan_identifier_or_keyword();
  void scan_number();
  void scan_string();
  void scan_operator_or_punctuation();
  void scan_line_comment();
  void scan_block_comment();
  void add_token(TokenKind kind, SourcePosition start);
  void add_diagnostic(std::string code, std::string message,
                      SourcePosition start);
  void synchronize_invalid_utf8();

  std::string source_;
  std::size_t offset_{0};
  std::size_t line_{1};
  std::size_t column_{1};
  LexResult result_;
};

}  // namespace masl::lexer
