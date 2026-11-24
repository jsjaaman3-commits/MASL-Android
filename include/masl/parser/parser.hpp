#pragma once

#include "masl/ast/ast.hpp"
#include "masl/lexer/token.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace masl::parser {

using lexer::SourceSpan;

struct ParseDiagnostic final {
  std::string code;
  std::string message;
  SourceSpan span{};
};

struct ParseResult final {
  std::unique_ptr<ast::Program> program;
  std::vector<ParseDiagnostic> diagnostics;

  [[nodiscard]] bool ok() const noexcept {
    return program != nullptr && diagnostics.empty();
  }
};

class Parser final {
 public:
  explicit Parser(std::vector<lexer::Token> tokens);
  [[nodiscard]] ParseResult parse();

 private:
  [[nodiscard]] const lexer::Token& current() const noexcept;
  [[nodiscard]] const lexer::Token& previous() const noexcept;
  [[nodiscard]] bool at_end() const noexcept;
  [[nodiscard]] bool check(lexer::TokenKind kind) const noexcept;
  [[nodiscard]] bool match(lexer::TokenKind kind) noexcept;
  [[nodiscard]] const lexer::Token& advance() noexcept;
  [[nodiscard]] bool expect(lexer::TokenKind kind, std::string message);
  [[nodiscard]] std::string parse_name();
  [[nodiscard]] std::string parse_type_name();
  [[nodiscard]] bool is_name_token() const noexcept;
  [[nodiscard]] bool is_comparison_operator() const noexcept;
  [[nodiscard]] SourceSpan joined_span(SourceSpan start, SourceSpan end) const noexcept;
  void error(std::string code, std::string message, SourceSpan span);
  void synchronize();

  [[nodiscard]] ast::Program parse_program();
  [[nodiscard]] std::optional<ast::ModelMember> parse_model_member();
  [[nodiscard]] ast::ClockDeclaration parse_clock();
  [[nodiscard]] ast::RecordDeclaration parse_record();
  [[nodiscard]] ast::EnumDeclaration parse_enum();
  [[nodiscard]] ast::QueueDeclaration parse_queue();
  [[nodiscard]] ast::ResourceDeclaration parse_resource();
  [[nodiscard]] ast::PipelineDeclaration parse_pipeline();
  [[nodiscard]] ast::ComponentDeclaration parse_component();
  [[nodiscard]] std::optional<ast::ComponentMember> parse_component_member();
  [[nodiscard]] ast::ParameterDeclaration parse_parameter();
  [[nodiscard]] ast::PortDeclaration parse_port(bool is_input);
  [[nodiscard]] ast::ConnectionDeclaration parse_connection();
  [[nodiscard]] ast::InstanceDeclaration parse_instance();
  [[nodiscard]] ast::WorkloadDeclaration parse_workload();
  [[nodiscard]] ast::ExperimentDeclaration parse_experiment();
  [[nodiscard]] ast::Expectation parse_expectation();
  [[nodiscard]] ast::ExportDeclaration parse_export();
  [[nodiscard]] ast::FunctionDeclaration parse_function();
  [[nodiscard]] ast::Block parse_block();
  [[nodiscard]] ast::StatementPtr parse_statement();
  [[nodiscard]] ast::Assignment parse_assignment();
  [[nodiscard]] std::string parse_endpoint();

  [[nodiscard]] ast::ExprPtr parse_expression(int min_precedence = 1);
  [[nodiscard]] ast::ExprPtr parse_prefix();
  [[nodiscard]] ast::ExprPtr parse_unit_or_number();
  [[nodiscard]] ast::ExprPtr make_error_expr(SourceSpan span);
  [[nodiscard]] int precedence(lexer::TokenKind kind) const noexcept;

  std::vector<lexer::Token> tokens_;
  std::size_t current_{0};
  ParseResult result_;
};

}  // namespace masl::parser
