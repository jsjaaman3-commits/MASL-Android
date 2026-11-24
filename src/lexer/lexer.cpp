#include "masl/lexer/lexer.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace masl::lexer {
namespace {

constexpr std::array<std::pair<std::string_view, TokenKind>, 58> kKeywords{{
    {"masl", TokenKind::KwMasl},
    {"model", TokenKind::KwModel},
    {"clock", TokenKind::KwClock},
    {"record", TokenKind::KwRecord},
    {"enum", TokenKind::KwEnum},
    {"queue", TokenKind::KwQueue},
    {"capacity", TokenKind::KwCapacity},
    {"discipline", TokenKind::KwDiscipline},
    {"overflow", TokenKind::KwOverflow},
    {"fifo", TokenKind::KwFifo},
    {"priority", TokenKind::KwPriority},
    {"round_robin", TokenKind::KwRoundRobin},
    {"block", TokenKind::KwBlock},
    {"reject", TokenKind::KwReject},
    {"drop", TokenKind::KwDrop},
    {"resource", TokenKind::KwResource},
    {"unit", TokenKind::KwUnit},
    {"slot", TokenKind::KwSlot},
    {"pipeline", TokenKind::KwPipeline},
    {"input", TokenKind::KwInput},
    {"output", TokenKind::KwOutput},
    {"on_start", TokenKind::KwOnStart},
    {"latency", TokenKind::KwLatency},
    {"initiation_interval", TokenKind::KwInitiationInterval},
    {"max_in_flight", TokenKind::KwMaxInFlight},
    {"component", TokenKind::KwComponent},
    {"param", TokenKind::KwParam},
    {"port", TokenKind::KwPort},
    {"connect", TokenKind::KwConnect},
    {"instances", TokenKind::KwInstances},
    {"workload", TokenKind::KwWorkload},
    {"repeat", TokenKind::KwRepeat},
    {"as", TokenKind::KwAs},
    {"submit", TokenKind::KwSubmit},
    {"experiment", TokenKind::KwExperiment},
    {"mode", TokenKind::KwMode},
    {"cycle", TokenKind::KwCycle},
    {"event", TokenKind::KwEvent},
    {"stop_when", TokenKind::KwStopWhen},
    {"idle", TokenKind::KwIdle},
    {"max_cycles", TokenKind::KwMaxCycles},
    {"max_events", TokenKind::KwMaxEvents},
    {"seed", TokenKind::KwSeed},
    {"expect", TokenKind::KwExpect},
    {"completed", TokenKind::KwCompleted},
    {"export", TokenKind::KwExport},
    {"trace", TokenKind::KwTrace},
    {"stats", TokenKind::KwStats},
    {"csv", TokenKind::KwCsv},
    {"fn", TokenKind::KwFn},
    {"let", TokenKind::KwLet},
    {"set", TokenKind::KwSet},
    {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse},
    {"return", TokenKind::KwReturn},
    {"assert", TokenKind::KwAssert},
    {"true", TokenKind::KwTrue},
    {"false", TokenKind::KwFalse},
}};

constexpr std::array<std::string_view, 15> kUnits{{
    "fs", "ps", "ns", "us", "ms", "s", "Hz", "kHz", "MHz", "GHz",
    "B", "KiB", "MiB", "GiB", "cycles"}};

bool is_ascii_alpha(char value) noexcept {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

bool is_digit(char value) noexcept { return value >= '0' && value <= '9'; }

bool is_identifier_start(char value) noexcept {
  return is_ascii_alpha(value) || value == '_';
}

bool is_identifier_continue(char value) noexcept {
  return is_identifier_start(value) || is_digit(value);
}

bool is_unit(std::string_view text) noexcept {
  for (const std::string_view unit : kUnits) {
    if (unit == text) {
      return true;
    }
  }
  return text == "bytes_per_cycle";
}

bool is_valid_utf8(std::string_view text, std::size_t& invalid_offset) noexcept {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7FU) {
      ++index;
      continue;
    }

    std::size_t length = 0;
    std::uint32_t codepoint = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      length = 2;
      codepoint = static_cast<std::uint32_t>(first & 0x1FU);
    } else if (first >= 0xE0U && first <= 0xEFU) {
      length = 3;
      codepoint = static_cast<std::uint32_t>(first & 0x0FU);
    } else if (first >= 0xF0U && first <= 0xF4U) {
      length = 4;
      codepoint = static_cast<std::uint32_t>(first & 0x07U);
    } else {
      invalid_offset = index;
      return false;
    }

    if (index + length > text.size()) {
      invalid_offset = index;
      return false;
    }
    for (std::size_t part = 1; part < length; ++part) {
      const auto next = static_cast<unsigned char>(text[index + part]);
      if ((next & 0xC0U) != 0x80U) {
        invalid_offset = index + part;
        return false;
      }
      codepoint = (codepoint << 6U) | static_cast<std::uint32_t>(next & 0x3FU);
    }

    const bool overlong =
        (length == 2 && codepoint < 0x80U) ||
        (length == 3 && codepoint < 0x800U) ||
        (length == 4 && codepoint < 0x10000U);
    if (overlong || (codepoint >= 0xD800U && codepoint <= 0xDFFFU) ||
        codepoint > 0x10FFFFU) {
      invalid_offset = index;
      return false;
    }
    index += length;
  }
  return true;
}

}  // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

bool Lexer::at_end() const noexcept { return offset_ >= source_.size(); }

char Lexer::peek(std::size_t lookahead) const noexcept {
  const std::size_t index = offset_ + lookahead;
  return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() noexcept {
  if (at_end()) {
    return '\0';
  }
  const char current = source_[offset_++];
  if (current == '\r') {
    if (offset_ < source_.size() && source_[offset_] == '\n') {
      ++offset_;
    }
    ++line_;
    column_ = 1;
  } else if (current == '\n') {
    ++line_;
    column_ = 1;
  } else {
    const auto byte = static_cast<unsigned char>(current);
    if ((byte & 0xC0U) != 0x80U) {
      ++column_;
    }
  }
  return current;
}

bool Lexer::match(char expected) noexcept {
  if (at_end() || peek() != expected) {
    return false;
  }
  static_cast<void>(advance());
  return true;
}

SourcePosition Lexer::position() const noexcept {
  return SourcePosition{offset_, line_, column_};
}

SourceSpan Lexer::span_from(SourcePosition start) const noexcept {
  return SourceSpan{start, position()};
}

void Lexer::add_token(TokenKind kind, SourcePosition start) {
  const std::size_t start_offset = start.byte_offset;
  const std::size_t length = offset_ - start_offset;
  result_.tokens.push_back(
      Token{kind, source_.substr(start_offset, length), span_from(start)});
}

void Lexer::add_diagnostic(std::string code, std::string message,
                           SourcePosition start) {
  result_.diagnostics.push_back(
      LexDiagnostic{std::move(code), std::move(message), span_from(start)});
}

void Lexer::skip_trivia() {
  while (!at_end()) {
    const char value = peek();
    if (value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
        value == '\f' || value == '\v') {
      static_cast<void>(advance());
      continue;
    }
    if (value == '/' && peek(1) == '/') {
      scan_line_comment();
      continue;
    }
    if (value == '/' && peek(1) == '*') {
      scan_block_comment();
      continue;
    }
    break;
  }
}

void Lexer::scan_identifier_or_keyword() {
  const SourcePosition start = position();
  while (is_identifier_continue(peek())) {
    static_cast<void>(advance());
  }
  const std::string_view text(source_.data() + start.byte_offset,
                               offset_ - start.byte_offset);

  for (const auto& [keyword, kind] : kKeywords) {
    if (keyword == text) {
      add_token(kind, start);
      return;
    }
  }
  if (is_unit(text)) {
    add_token(TokenKind::Unit, start);
    return;
  }
  add_token(TokenKind::Identifier, start);
}

void Lexer::scan_number() {
  const SourcePosition start = position();
  bool malformed = false;
  bool previous_was_digit = false;

  while (is_digit(peek()) || peek() == '_') {
    if (peek() == '_') {
      if (!previous_was_digit || !is_digit(peek(1))) {
        malformed = true;
      }
      previous_was_digit = false;
    } else {
      previous_was_digit = true;
    }
    static_cast<void>(advance());
  }

  if (peek() == '.' && is_digit(peek(1))) {
    static_cast<void>(advance());
    previous_was_digit = false;
    while (is_digit(peek()) || peek() == '_') {
      if (peek() == '_') {
        if (!previous_was_digit || !is_digit(peek(1))) {
          malformed = true;
        }
        previous_was_digit = false;
      } else {
        previous_was_digit = true;
      }
      static_cast<void>(advance());
    }
  }

  if (peek() == 'e' || peek() == 'E') {
    static_cast<void>(advance());
    if (peek() == '+' || peek() == '-') {
      static_cast<void>(advance());
    }
    if (!is_digit(peek())) {
      malformed = true;
    } else {
      previous_was_digit = false;
      while (is_digit(peek()) || peek() == '_') {
        if (peek() == '_') {
          if (!previous_was_digit || !is_digit(peek(1))) {
            malformed = true;
          }
          previous_was_digit = false;
        } else {
          previous_was_digit = true;
        }
        static_cast<void>(advance());
      }
    }
  }

  if (malformed) {
    add_diagnostic("MASL-E1002",
                   "Malformed numeric literal or misplaced digit separator.",
                   start);
    return;
  }
  add_token(TokenKind::Number, start);

  if (is_identifier_start(peek())) {
    const SourcePosition suffix_start = position();
    while (is_identifier_continue(peek())) {
      static_cast<void>(advance());
    }
    const std::string_view suffix(source_.data() + suffix_start.byte_offset,
                                  offset_ - suffix_start.byte_offset);
    if (is_unit(suffix)) {
      add_token(TokenKind::Unit, suffix_start);
    } else {
      add_diagnostic("MASL-E1005",
                     "Unknown unit suffix '" + std::string(suffix) +
                         "' immediately follows a numeric literal.",
                     suffix_start);
    }
  }
}

void Lexer::scan_string() {
  const SourcePosition start = position();
  static_cast<void>(advance());
  bool terminated = false;
  bool invalid_escape = false;

  while (!at_end()) {
    const char value = peek();
    if (value == '"') {
      static_cast<void>(advance());
      terminated = true;
      break;
    }
    if (value == '\n' || value == '\r') {
      break;
    }
    if (value == '\\') {
      static_cast<void>(advance());
      if (at_end()) {
        break;
      }
      const char escaped = advance();
      if (escaped != '"' && escaped != '\\' && escaped != 'n' &&
          escaped != 'r' && escaped != 't' && escaped != '0') {
        invalid_escape = true;
      }
      continue;
    }
    static_cast<void>(advance());
  }

  if (!terminated) {
    add_diagnostic("MASL-E1003", "Unterminated string literal.", start);
    return;
  }
  if (invalid_escape) {
    add_diagnostic("MASL-E1003",
                   "String contains an unsupported escape sequence.", start);
    return;
  }
  add_token(TokenKind::String, start);
}

void Lexer::scan_line_comment() {
  static_cast<void>(advance());
  static_cast<void>(advance());
  while (!at_end() && peek() != '\n' && peek() != '\r') {
    static_cast<void>(advance());
  }
}

void Lexer::scan_block_comment() {
  const SourcePosition start = position();
  static_cast<void>(advance());
  static_cast<void>(advance());
  while (!at_end()) {
    if (peek() == '*' && peek(1) == '/') {
      static_cast<void>(advance());
      static_cast<void>(advance());
      return;
    }
    static_cast<void>(advance());
  }
  add_diagnostic("MASL-E1004", "Unterminated block comment.", start);
}

void Lexer::scan_operator_or_punctuation() {
  const SourcePosition start = position();
  const char value = advance();
  switch (value) {
    case '{': add_token(TokenKind::LeftBrace, start); return;
    case '}': add_token(TokenKind::RightBrace, start); return;
    case '(': add_token(TokenKind::LeftParen, start); return;
    case ')': add_token(TokenKind::RightParen, start); return;
    case '[': add_token(TokenKind::LeftBracket, start); return;
    case ']': add_token(TokenKind::RightBracket, start); return;
    case ';': add_token(TokenKind::Semicolon, start); return;
    case ':': add_token(TokenKind::Colon, start); return;
    case ',': add_token(TokenKind::Comma, start); return;
    case '.': add_token(TokenKind::Dot, start); return;
    case '+': add_token(TokenKind::Plus, start); return;
    case '-': add_token(match('>') ? TokenKind::Arrow : TokenKind::Minus, start); return;
    case '*': add_token(TokenKind::Star, start); return;
    case '/': add_token(TokenKind::Slash, start); return;
    case '%': add_token(TokenKind::Percent, start); return;
    case '=': add_token(match('=') ? TokenKind::Equal : TokenKind::Assign, start); return;
    case '!': add_token(match('=') ? TokenKind::NotEqual : TokenKind::Bang, start); return;
    case '<': add_token(match('=') ? TokenKind::LessEqual : TokenKind::Less, start); return;
    case '>': add_token(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater, start); return;
    case '&':
      if (match('&')) {
        add_token(TokenKind::AndAnd, start);
      } else {
        add_diagnostic("MASL-E1001", "Expected '&&'; a single '&' is invalid.", start);
      }
      return;
    case '|':
      if (match('|')) {
        add_token(TokenKind::OrOr, start);
      } else {
        add_diagnostic("MASL-E1001", "Expected '||'; a single '|' is invalid.", start);
      }
      return;
    default:
      add_diagnostic("MASL-E1001",
                     "Unexpected character in MASL source: '" +
                         std::string(1, value) + "'.",
                     start);
      return;
  }
}

void Lexer::synchronize_invalid_utf8() {
  std::size_t invalid_offset = 0;
  if (is_valid_utf8(source_, invalid_offset)) {
    return;
  }

  while (offset_ < invalid_offset) {
    static_cast<void>(advance());
  }
  const SourcePosition start = position();
  const auto byte = static_cast<unsigned char>(peek());
  static_cast<void>(advance());
  add_diagnostic("MASL-E1006",
                 "Source contains an invalid UTF-8 byte sequence (leading byte " +
                     std::to_string(static_cast<unsigned int>(byte)) + ").",
                 start);
}

LexResult Lexer::scan() {
  synchronize_invalid_utf8();
  if (!result_.diagnostics.empty()) {
    result_.tokens.push_back(Token{TokenKind::EndOfFile, "", SourceSpan{position(), position()}});
    return std::move(result_);
  }

  while (!at_end()) {
    skip_trivia();
    if (at_end()) {
      break;
    }

    if (is_identifier_start(peek())) {
      scan_identifier_or_keyword();
    } else if (is_digit(peek())) {
      scan_number();
    } else if (peek() == '"') {
      scan_string();
    } else {
      scan_operator_or_punctuation();
    }
  }

  const SourcePosition end = position();
  result_.tokens.push_back(Token{TokenKind::EndOfFile, "", SourceSpan{end, end}});
  return std::move(result_);
}

}  // namespace masl::lexer
