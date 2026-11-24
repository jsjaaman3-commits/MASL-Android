#pragma once

#include "masl/lexer/token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace masl::ast {

using lexer::SourceSpan;

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct ErrorExpr final {};
struct LiteralExpr final {
  enum class Kind { Number, String, Boolean };
  Kind kind{Kind::Number};
  std::string value;
};
struct NameExpr final { std::string name; };
struct UnitExpr final {
  std::string number;
  std::string unit;
  std::string clock_domain;
};
struct UnaryExpr final {
  lexer::TokenKind op;
  ExprPtr operand;
};
struct BinaryExpr final {
  lexer::TokenKind op;
  ExprPtr left;
  ExprPtr right;
};
struct CallExpr final {
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};
struct FieldExpr final {
  ExprPtr object;
  std::string field;
};

struct Expr final {
  SourceSpan span{};
  std::variant<ErrorExpr, LiteralExpr, NameExpr, UnitExpr, UnaryExpr,
               BinaryExpr, CallExpr, FieldExpr> node;
};

struct Assignment final {
  SourceSpan span{};
  std::string target;
  ExprPtr value;
};

struct Statement;
using StatementPtr = std::unique_ptr<Statement>;
struct Block final {
  SourceSpan span{};
  std::vector<StatementPtr> statements;
};

struct LetStatement final {
  std::string name;
  std::string type_name;
  ExprPtr value;
};
struct SetStatement final { Assignment assignment; };
struct IfStatement final {
  ExprPtr condition;
  Block then_block;
  std::optional<Block> else_block;
};
struct RepeatStatement final {
  ExprPtr count;
  std::string index_name;
  Block body;
};
struct SubmitStatement final {
  std::string queue_name;
  std::string record_type;
  std::vector<Assignment> fields;
};
struct AssertStatement final { ExprPtr condition; };
struct ReturnStatement final { ExprPtr value; };
struct ExpressionStatement final { ExprPtr expression; };

struct Statement final {
  SourceSpan span{};
  std::variant<LetStatement, SetStatement, IfStatement, RepeatStatement,
               SubmitStatement, AssertStatement, ReturnStatement,
               ExpressionStatement> node;
};

struct FieldDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string type_name;
};
struct ClockDeclaration final {
  SourceSpan span{};
  std::string name;
  ExprPtr frequency;
};
struct RecordDeclaration final {
  SourceSpan span{};
  std::string name;
  std::vector<FieldDeclaration> fields;
};
struct EnumDeclaration final {
  SourceSpan span{};
  std::string name;
  std::vector<std::string> variants;
};
struct QueueDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string element_type;
  ExprPtr capacity;
  std::string discipline;
  std::string overflow;
  unsigned int capacity_count{0};
  unsigned int discipline_count{0};
  unsigned int overflow_count{0};
};
struct ResourceDeclaration final {
  SourceSpan span{};
  std::string name;
  ExprPtr capacity;
  std::string unit;
  unsigned int capacity_count{0};
  unsigned int unit_count{0};
};
struct PipelineDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string input_type;
  std::string output_type;
  std::string input_queue;
  std::string output_queue;
  std::string clock;
  ExprPtr latency;
  ExprPtr initiation_interval;
  ExprPtr max_in_flight;
  std::string handler_parameter;
  std::optional<Block> on_start;
  unsigned int input_count{0};
  unsigned int output_count{0};
  unsigned int clock_count{0};
  unsigned int latency_count{0};
  unsigned int initiation_interval_count{0};
  unsigned int max_in_flight_count{0};
  unsigned int on_start_count{0};
};
struct ParameterDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string type_name;
  ExprPtr default_value;
};
struct PortDeclaration final {
  SourceSpan span{};
  bool is_input{true};
  std::string name;
  std::string type_name;
};
struct ConnectionDeclaration final {
  SourceSpan span{};
  std::string source;
  std::string target;
};
struct InstanceDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string component_type;
  ExprPtr count;
  std::vector<Assignment> parameters;
};

using ComponentMember = std::variant<ParameterDeclaration, PortDeclaration,
                                     QueueDeclaration, ResourceDeclaration,
                                     PipelineDeclaration, InstanceDeclaration,
                                     ConnectionDeclaration>;
struct ComponentDeclaration final {
  SourceSpan span{};
  std::string name;
  std::vector<ComponentMember> members;
};

struct WorkloadDeclaration final {
  SourceSpan span{};
  std::string name;
  Block body;
};
struct Expectation final {
  SourceSpan span{};
  ExprPtr left;
  lexer::TokenKind op{lexer::TokenKind::Equal};
  ExprPtr right;
};
struct ExportDeclaration final {
  SourceSpan span{};
  std::string kind;
  std::string path;
};
struct ExperimentDeclaration final {
  SourceSpan span{};
  std::string name;
  std::string workload;
  std::string mode;
  std::string stop_when;
  ExprPtr max_cycles;
  ExprPtr max_events;
  ExprPtr seed;
  unsigned int workload_count{0};
  unsigned int mode_count{0};
  unsigned int stop_when_count{0};
  unsigned int max_cycles_count{0};
  unsigned int max_events_count{0};
  unsigned int seed_count{0};
  std::vector<Expectation> expectations;
  std::vector<ExportDeclaration> exports;
};
struct FunctionParameter final {
  SourceSpan span{};
  std::string name;
  std::string type_name;
};
struct FunctionDeclaration final {
  SourceSpan span{};
  std::string name;
  std::vector<FunctionParameter> parameters;
  std::string return_type;
  Block body;
};

using ModelMember = std::variant<ClockDeclaration, RecordDeclaration,
                                 EnumDeclaration, QueueDeclaration,
                                 ResourceDeclaration, PipelineDeclaration,
                                 ComponentDeclaration, InstanceDeclaration,
                                 WorkloadDeclaration, ExperimentDeclaration,
                                 FunctionDeclaration>;
struct Model final {
  SourceSpan span{};
  std::string name;
  std::vector<ModelMember> members;
};
struct Program final {
  unsigned int language_major{0};
  unsigned int language_minor{0};
  Model model;
};

}  // namespace masl::ast
