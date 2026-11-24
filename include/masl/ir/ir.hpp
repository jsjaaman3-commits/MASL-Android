#pragma once

#include "masl/ast/ast.hpp"
#include "masl/semantic/analyzer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace masl::ir {

enum class ExpressionKind {
  IntegerLiteral,
  FloatingLiteral,
  BooleanLiteral,
  StringLiteral,
  Name,
  Unit,
  Unary,
  Binary,
  Call,
  Field
};

struct Expression final {
  ExpressionKind kind{ExpressionKind::IntegerLiteral};
  semantic::Type type{};
  lexer::SourceSpan span{};
  lexer::TokenKind operation{lexer::TokenKind::EndOfFile};
  std::string value;
  std::string unit;
  std::string clock_domain;
  // Direct calls store the callee name in value; pipeline handles accepted by
  // completed(...) are retained as symbol arguments instead of fake values.
  std::vector<std::string> symbol_arguments;
  std::vector<Expression> operands;
};

struct Assignment final {
  lexer::SourceSpan span{};
  std::string target;
  Expression value;
};

struct Statement;
struct Block final {
  lexer::SourceSpan span{};
  std::vector<Statement> statements;
};

enum class StatementKind { Let, Set, If, Repeat, Submit, Assert, Return, Expression };

struct Statement final {
  StatementKind kind{StatementKind::Expression};
  lexer::SourceSpan span{};
  std::string name;
  std::string declared_type;
  semantic::Type declared_semantic_type{};
  std::string auxiliary;
  std::optional<Expression> expression;
  std::vector<Assignment> assignments;
  Block body;
  std::optional<Block> else_body;
};

struct Field final {
  lexer::SourceSpan span{};
  std::string name;
  semantic::Type type{};
};

struct Clock final {
  lexer::SourceSpan span{};
  std::string name;
  Expression frequency;
};

struct Record final {
  lexer::SourceSpan span{};
  std::string name;
  std::vector<Field> fields;
};

struct Enum final {
  lexer::SourceSpan span{};
  std::string name;
  std::vector<std::string> variants;
};

struct Queue final {
  lexer::SourceSpan span{};
  std::string name;
  semantic::Type element_type{};
  Expression capacity;
  std::string discipline;
  std::string overflow;
};

struct Resource final {
  lexer::SourceSpan span{};
  std::string name;
  Expression capacity;
  std::string unit;
};

struct Pipeline final {
  lexer::SourceSpan span{};
  std::string name;
  semantic::Type input_type{};
  semantic::Type output_type{};
  std::string input_queue;
  std::string output_queue;
  std::string clock;
  Expression latency;
  Expression initiation_interval;
  Expression max_in_flight;
  std::string handler_parameter;
  std::optional<Block> on_start;
};

struct Parameter final {
  lexer::SourceSpan span{};
  std::string name;
  semantic::Type type{};
  std::optional<Expression> default_value;
};

struct Port final {
  lexer::SourceSpan span{};
  bool is_input{true};
  std::string name;
  semantic::Type type{};
};

struct Connection final {
  lexer::SourceSpan span{};
  std::string source;
  std::string target;
};

struct Instance final {
  lexer::SourceSpan span{};
  std::string name;
  std::string component_type;
  Expression count;
  std::vector<Assignment> parameters;
};

using ComponentMember = std::variant<Parameter, Port, Queue, Resource, Pipeline,
                                     Instance, Connection>;

struct Component final {
  lexer::SourceSpan span{};
  std::string name;
  std::vector<ComponentMember> members;
};

struct Workload final {
  lexer::SourceSpan span{};
  std::string name;
  Block body;
};

struct Expectation final {
  lexer::SourceSpan span{};
  Expression left;
  lexer::TokenKind operation{lexer::TokenKind::Equal};
  Expression right;
};

struct Export final {
  lexer::SourceSpan span{};
  std::string kind;
  std::string path;
};

struct Experiment final {
  lexer::SourceSpan span{};
  std::string name;
  std::string workload;
  std::string mode;
  std::string stop_when;
  std::optional<Expression> max_cycles;
  std::optional<Expression> max_events;
  std::optional<Expression> seed;
  std::vector<Expectation> expectations;
  std::vector<Export> exports;
};

struct FunctionParameter final {
  lexer::SourceSpan span{};
  std::string name;
  semantic::Type type{};
};

struct Function final {
  lexer::SourceSpan span{};
  std::string name;
  std::vector<FunctionParameter> parameters;
  semantic::Type return_type{};
  Block body;
};

using Declaration = std::variant<Clock, Record, Enum, Queue, Resource, Pipeline,
                                 Component, Instance, Workload, Experiment, Function>;

struct Module final {
  std::uint32_t ir_major{1};
  std::uint32_t ir_minor{0};
  unsigned int language_major{0};
  unsigned int language_minor{0};
  std::string name;
  std::vector<Declaration> declarations;
};

struct Diagnostic final {
  std::string code;
  std::string message;
  lexer::SourceSpan span{};
};

struct BuildResult final {
  std::optional<Module> module;
  std::vector<Diagnostic> diagnostics;
  [[nodiscard]] bool ok() const noexcept {
    return module.has_value() && diagnostics.empty();
  }
};

// Lowering accepts only a semantically valid AST and retains inferred types
// from the semantic pass. It never silently invents an expression type.
[[nodiscard]] BuildResult lower(const ast::Program& program,
                                const semantic::Result& semantic_result);

// Structural verification is intentionally separate from semantic analysis:
// it checks IR invariants before a later bytecode backend consumes the module.
[[nodiscard]] std::vector<Diagnostic> verify(const Module& module);
[[nodiscard]] std::string dump(const Module& module);

}  // namespace masl::ir
