#pragma once

#include "masl/ast/ast.hpp"
#include "masl/lexer/token.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace masl::semantic {

enum class TypeKind {
  Error,
  Bool,
  SignedInteger,
  UnsignedInteger,
  FloatingPoint,
  String,
  Record,
  Enum,
  Time,
  Frequency,
  Cycles,
  Bytes,
  Bandwidth,
  Latency,
  Count,
  Address,
  IntegerLiteral,
  FloatingLiteral
};

struct Type final {
  TypeKind kind{TypeKind::Error};
  std::uint8_t bits{0};
  std::string nominal_name;
  std::string clock_domain;

  Type() = default;
  Type(TypeKind type_kind, std::uint8_t width = 0,
       std::string name = {}, std::string clock = {})
      : kind(type_kind), bits(width), nominal_name(std::move(name)),
        clock_domain(std::move(clock)) {}

  [[nodiscard]] bool is_error() const noexcept { return kind == TypeKind::Error; }
  [[nodiscard]] bool is_integer() const noexcept;
  [[nodiscard]] bool is_numeric() const noexcept;
  [[nodiscard]] std::string to_string() const;

  friend bool operator==(const Type&, const Type&) = default;
};

struct Diagnostic final {
  std::string code;
  std::string message;
  lexer::SourceSpan span{};
};

struct Result final {
  std::vector<Diagnostic> diagnostics;
  std::unordered_map<const ast::Expr*, Type> expression_types;

  [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

class Analyzer final {
 public:
  [[nodiscard]] Result analyze(const ast::Program& program);
};

[[nodiscard]] Result analyze(const ast::Program& program);

}  // namespace masl::semantic
