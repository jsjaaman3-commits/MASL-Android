#include "masl/lexer/lexer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
  }
}

bool has_kind(const masl::lexer::LexResult& result,
              masl::lexer::TokenKind kind) {
  for (const auto& token : result.tokens) {
    if (token.kind == kind) {
      return true;
    }
  }
  return false;
}

bool has_diagnostic(const masl::lexer::LexResult& result,
                    std::string_view code) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using masl::lexer::Lexer;
  using masl::lexer::TokenKind;

  {
    const auto result = Lexer("masl 0.1; model M { queue output: Job {} }").scan();
    check(result.ok(), "lexes language header and model skeleton");
    check(result.tokens.size() > 2U &&
              result.tokens[1].kind == TokenKind::Number &&
              result.tokens[1].lexeme == "0.1",
          "tokenizes source language version as one decimal token");
    check(has_kind(result, TokenKind::KwMasl), "recognizes masl keyword");
    check(has_kind(result, TokenKind::KwQueue), "recognizes queue keyword");
    check(has_kind(result, TokenKind::KwOutput), "recognizes output keyword");
    check(result.tokens.back().kind == TokenKind::EndOfFile,
          "always appends EOF token");
  }

  {
    const auto result = Lexer("2.4 GHz 4 cycles(core) 64 bytes_per_cycle(core) 128 KiB").scan();
    check(result.ok(), "lexes engineering unit literals");
    std::size_t unit_count = 0;
    for (const auto& token : result.tokens) {
      if (token.kind == TokenKind::Unit) {
        ++unit_count;
      }
    }
    check(unit_count == 4U, "recognizes frequency, cycles, bandwidth and byte units");
  }

  {
    const auto result = Lexer("// line comment\r\n/* block\ncomment */ fn add() -> i32 { return 1 + 2 * 3; }").scan();
    check(result.ok(), "skips line and block comments");
    check(has_kind(result, TokenKind::KwFn), "lexes function keyword after comments");
    check(has_kind(result, TokenKind::Arrow), "lexes arrow operator");
    check(has_kind(result, TokenKind::Star), "lexes multiplication operator");
  }

  {
    const auto result = Lexer("latency = 3 cycles(core);").scan();
    check(result.ok() && has_kind(result, TokenKind::KwLatency),
          "recognizes latency as a reserved pipeline property");
  }

  {
    const auto result = Lexer("\"gpu trace: فارسی\\nline\\\"quoted\\\\path\"").scan();
    check(result.ok(), "accepts UTF-8 string and supported escapes");
    check(result.tokens.front().kind == TokenKind::String,
          "emits string token");
  }

  {
    const auto result = Lexer("first\r\nsecond").scan();
    check(result.tokens.size() == 3U, "tokenizes identifiers plus EOF");
    check(result.tokens[1].span.start.line == 2U &&
              result.tokens[1].span.start.column == 1U,
          "tracks CRLF source positions");
  }

  {
    const auto result = Lexer("1__2 3e+ 4widgets").scan();
    check(!result.ok(), "rejects malformed numbers and unknown unit suffixes");
    check(has_diagnostic(result, "MASL-E1002"), "reports malformed number code");
    check(has_diagnostic(result, "MASL-E1005"), "reports unknown unit code");
  }

  {
    const auto result = Lexer("\"unterminated").scan();
    check(has_diagnostic(result, "MASL-E1003"), "reports unterminated string");
  }

  {
    const auto result = Lexer("/* unterminated").scan();
    check(has_diagnostic(result, "MASL-E1004"), "reports unterminated block comment");
  }

  {
    const auto result = Lexer("value & other | flag").scan();
    check(has_diagnostic(result, "MASL-E1001"), "rejects single ampersand and pipe");
  }

  {
    std::string invalid_utf8("name ");
    invalid_utf8.push_back(static_cast<char>(0xFF));
    const auto result = Lexer(invalid_utf8).scan();
    check(has_diagnostic(result, "MASL-E1006"), "rejects malformed UTF-8 source");
  }

  {
    const auto result = Lexer("a == b != c <= d >= e && f || !g -> h").scan();
    check(result.ok(), "lexes compound operators");
    check(has_kind(result, TokenKind::Equal) &&
              has_kind(result, TokenKind::NotEqual) &&
              has_kind(result, TokenKind::LessEqual) &&
              has_kind(result, TokenKind::GreaterEqual) &&
              has_kind(result, TokenKind::AndAnd) &&
              has_kind(result, TokenKind::OrOr),
          "recognizes comparison and logical operators");
  }

  if (failures != 0) {
    std::cerr << failures << " lexer test(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All lexer tests passed.\n";
  return EXIT_SUCCESS;
}
