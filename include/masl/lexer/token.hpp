#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace masl::lexer {

enum class TokenKind {
  EndOfFile,
  Identifier,
  Number,
  String,
  Unit,

  KwMasl,
  KwModel,
  KwClock,
  KwRecord,
  KwEnum,
  KwQueue,
  KwCapacity,
  KwDiscipline,
  KwOverflow,
  KwFifo,
  KwPriority,
  KwRoundRobin,
  KwBlock,
  KwReject,
  KwDrop,
  KwResource,
  KwUnit,
  KwSlot,
  KwPipeline,
  KwInput,
  KwOutput,
  KwOnStart,
  KwLatency,
  KwInitiationInterval,
  KwMaxInFlight,
  KwComponent,
  KwParam,
  KwPort,
  KwConnect,
  KwInstances,
  KwWorkload,
  KwRepeat,
  KwAs,
  KwSubmit,
  KwExperiment,
  KwMode,
  KwCycle,
  KwEvent,
  KwStopWhen,
  KwIdle,
  KwMaxCycles,
  KwMaxEvents,
  KwSeed,
  KwExpect,
  KwCompleted,
  KwExport,
  KwTrace,
  KwStats,
  KwCsv,
  KwFn,
  KwLet,
  KwSet,
  KwIf,
  KwElse,
  KwReturn,
  KwAssert,
  KwTrue,
  KwFalse,

  LeftBrace,
  RightBrace,
  LeftParen,
  RightParen,
  LeftBracket,
  RightBracket,
  Semicolon,
  Colon,
  Comma,
  Dot,
  Arrow,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Assign,
  Equal,
  Bang,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AndAnd,
  OrOr
};

struct SourcePosition final {
  std::size_t byte_offset{0};
  std::size_t line{1};
  std::size_t column{1};
};

struct SourceSpan final {
  SourcePosition start{};
  SourcePosition end{};
};

struct Token final {
  TokenKind kind{TokenKind::EndOfFile};
  std::string lexeme;
  SourceSpan span{};
};

[[nodiscard]] std::string_view token_kind_name(TokenKind kind) noexcept;

}  // namespace masl::lexer
