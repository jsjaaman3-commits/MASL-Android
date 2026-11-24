#include "masl/ir/ir.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace masl::ir {
namespace {

using Type = semantic::Type;
using TypeKind = semantic::TypeKind;

Type resolve_type_name(const std::string& name,
                       const std::unordered_set<std::string>& records,
                       const std::unordered_set<std::string>& enums) {
  if (name == "bool") return Type{TypeKind::Bool};
  if (name == "string") return Type{TypeKind::String};
  if (name == "time") return Type{TypeKind::Time};
  if (name == "frequency") return Type{TypeKind::Frequency};
  if (name == "cycles") return Type{TypeKind::Cycles};
  if (name == "bytes") return Type{TypeKind::Bytes};
  if (name == "bandwidth") return Type{TypeKind::Bandwidth};
  if (name == "latency") return Type{TypeKind::Latency};
  if (name == "count") return Type{TypeKind::Count};
  if (name == "address") return Type{TypeKind::Address};
  if (name.size() >= 2U) {
    unsigned int width = 0;
    const auto first = name.data() + 1;
    const auto last = name.data() + name.size();
    const auto parsed = std::from_chars(first, last, width);
    if (parsed.ec == std::errc{} && parsed.ptr == last) {
      if (name.front() == 'i' && (width == 8U || width == 16U || width == 32U || width == 64U))
        return Type{TypeKind::SignedInteger, static_cast<std::uint8_t>(width)};
      if (name.front() == 'u' && (width == 8U || width == 16U || width == 32U || width == 64U))
        return Type{TypeKind::UnsignedInteger, static_cast<std::uint8_t>(width)};
      if (name.front() == 'f' && (width == 32U || width == 64U))
        return Type{TypeKind::FloatingPoint, static_cast<std::uint8_t>(width)};
    }
  }
  if (records.contains(name)) return Type{TypeKind::Record, 0U, name};
  if (enums.contains(name)) return Type{TypeKind::Enum, 0U, name};
  return Type{};
}

class Lowerer final {
 public:
  Lowerer(const ast::Program& program, const semantic::Result& semantic_result)
      : program_(program), semantic_result_(semantic_result) {
    for (const auto& member : program_.model.members) {
      if (const auto* record = std::get_if<ast::RecordDeclaration>(&member))
        records_.insert(record->name);
      if (const auto* enumeration = std::get_if<ast::EnumDeclaration>(&member))
        enums_.insert(enumeration->name);
    }
  }

  BuildResult run() {
    if (!semantic_result_.ok()) {
      diagnostics_.push_back({"MASL-I5001",
                              "Typed IR lowering requires a successful semantic analysis.",
                              program_.model.span});
      return {std::nullopt, std::move(diagnostics_)};
    }

    Module module;
    module.language_major = program_.language_major;
    module.language_minor = program_.language_minor;
    module.name = program_.model.name;
    for (const auto& member : program_.model.members) {
      module.declarations.push_back(lower_declaration(member));
    }
    if (!diagnostics_.empty()) return {std::nullopt, std::move(diagnostics_)};

    auto verification = verify(module);
    if (!verification.empty()) return {std::nullopt, std::move(verification)};
    return {std::move(module), {}};
  }

 private:
  Type resolve(const std::string& name, lexer::SourceSpan span) {
    Type type = resolve_type_name(name, records_, enums_);
    if (type.is_error()) {
      diagnostics_.push_back({"MASL-I5002", "Cannot resolve declared type '" + name + "' while lowering.", span});
    }
    return type;
  }

  Expression lower_expression(const ast::Expr* source) {
    Expression target;
    if (source == nullptr) {
      diagnostics_.push_back({"MASL-I5003", "Encountered a null AST expression.", {}});
      return target;
    }
    target.span = source->span;
    const auto type = semantic_result_.expression_types.find(source);
    if (type == semantic_result_.expression_types.end()) {
      diagnostics_.push_back({"MASL-I5004", "Semantic analysis did not provide a type for an expression.", source->span});
    } else {
      target.type = type->second;
      if (target.type.is_error()) {
        diagnostics_.push_back({"MASL-I5005", "Cannot lower an expression with an error type.", source->span});
      }
    }

    std::visit([&](const auto& node) { lower_expression_node(node, target); }, source->node);
    return target;
  }

  void lower_expression_node(const ast::ErrorExpr&, Expression& target) {
    target.kind = ExpressionKind::IntegerLiteral;
    target.value = "<error>";
  }
  void lower_expression_node(const ast::LiteralExpr& node, Expression& target) {
    target.value = node.value;
    switch (node.kind) {
      case ast::LiteralExpr::Kind::Number:
        target.kind = node.value.find_first_of(".eE") == std::string::npos
                          ? ExpressionKind::IntegerLiteral
                          : ExpressionKind::FloatingLiteral;
        break;
      case ast::LiteralExpr::Kind::String: target.kind = ExpressionKind::StringLiteral; break;
      case ast::LiteralExpr::Kind::Boolean: target.kind = ExpressionKind::BooleanLiteral; break;
    }
  }
  void lower_expression_node(const ast::NameExpr& node, Expression& target) {
    target.kind = ExpressionKind::Name;
    target.value = node.name;
  }
  void lower_expression_node(const ast::UnitExpr& node, Expression& target) {
    target.kind = ExpressionKind::Unit;
    target.value = node.number;
    target.unit = node.unit;
    target.clock_domain = node.clock_domain;
  }
  void lower_expression_node(const ast::UnaryExpr& node, Expression& target) {
    target.kind = ExpressionKind::Unary;
    target.operation = node.op;
    if (node.operand != nullptr) target.operands.push_back(lower_expression(node.operand.get()));
    else diagnostics_.push_back({"MASL-I5006", "Unary expression has no operand.", target.span});
  }
  void lower_expression_node(const ast::BinaryExpr& node, Expression& target) {
    target.kind = ExpressionKind::Binary;
    target.operation = node.op;
    if (node.left != nullptr) target.operands.push_back(lower_expression(node.left.get()));
    else diagnostics_.push_back({"MASL-I5007", "Binary expression has no left operand.", target.span});
    if (node.right != nullptr) target.operands.push_back(lower_expression(node.right.get()));
    else diagnostics_.push_back({"MASL-I5008", "Binary expression has no right operand.", target.span});
  }
  void lower_expression_node(const ast::CallExpr& node, Expression& target) {
    target.kind = ExpressionKind::Call;
    const auto* callee = node.callee == nullptr
        ? nullptr : std::get_if<ast::NameExpr>(&node.callee->node);
    if (callee == nullptr) {
      diagnostics_.push_back({"MASL-I5009", "Only direct named calls can be lowered to Typed IR.", target.span});
      return;
    }
    target.value = callee->name;
    for (const auto& argument : node.arguments) {
      if (argument == nullptr) {
        diagnostics_.push_back({"MASL-I5010", "Call expression contains a null argument.", target.span});
        continue;
      }
      if (semantic_result_.expression_types.contains(argument.get())) {
        target.operands.push_back(lower_expression(argument.get()));
      } else if (const auto* symbol = std::get_if<ast::NameExpr>(&argument->node)) {
        // completed(pipeline) is a typed intrinsic whose argument is a symbol
        // reference, not a first-class runtime value in the current type model.
        target.symbol_arguments.push_back(symbol->name);
      } else {
        diagnostics_.push_back({"MASL-I5010", "Call argument has neither a semantic value type nor a supported symbol-reference form.", argument->span});
      }
    }
  }
  void lower_expression_node(const ast::FieldExpr& node, Expression& target) {
    target.kind = ExpressionKind::Field;
    target.value = node.field;
    if (node.object != nullptr) target.operands.push_back(lower_expression(node.object.get()));
    else diagnostics_.push_back({"MASL-I5010", "Field expression has no object.", target.span});
  }

  Assignment lower_assignment(const ast::Assignment& source) {
    return Assignment{source.span, source.target, lower_expression(source.value.get())};
  }

  Block lower_block(const ast::Block& source) {
    Block target;
    target.span = source.span;
    for (const auto& statement : source.statements) {
      if (statement == nullptr) {
        diagnostics_.push_back({"MASL-I5011", "Encountered a null AST statement.", source.span});
      } else {
        target.statements.push_back(lower_statement(*statement));
      }
    }
    return target;
  }

  Statement lower_statement(const ast::Statement& source) {
    Statement target;
    target.span = source.span;
    std::visit([&](const auto& node) { lower_statement_node(node, target); }, source.node);
    return target;
  }
  void lower_statement_node(const ast::LetStatement& node, Statement& target) {
    target.kind = StatementKind::Let;
    target.name = node.name;
    target.declared_type = node.type_name;
    target.declared_semantic_type = resolve(node.type_name, target.span);
    target.expression = lower_expression(node.value.get());
  }
  void lower_statement_node(const ast::SetStatement& node, Statement& target) {
    target.kind = StatementKind::Set;
    target.name = node.assignment.target;
    target.expression = lower_expression(node.assignment.value.get());
  }
  void lower_statement_node(const ast::IfStatement& node, Statement& target) {
    target.kind = StatementKind::If;
    target.expression = lower_expression(node.condition.get());
    target.body = lower_block(node.then_block);
    if (node.else_block.has_value()) target.else_body = lower_block(*node.else_block);
  }
  void lower_statement_node(const ast::RepeatStatement& node, Statement& target) {
    target.kind = StatementKind::Repeat;
    target.name = node.index_name;
    target.expression = lower_expression(node.count.get());
    target.body = lower_block(node.body);
  }
  void lower_statement_node(const ast::SubmitStatement& node, Statement& target) {
    target.kind = StatementKind::Submit;
    target.name = node.queue_name;
    target.declared_type = node.record_type;
    target.declared_semantic_type = resolve(node.record_type, target.span);
    for (const auto& field : node.fields) target.assignments.push_back(lower_assignment(field));
  }
  void lower_statement_node(const ast::AssertStatement& node, Statement& target) {
    target.kind = StatementKind::Assert;
    target.expression = lower_expression(node.condition.get());
  }
  void lower_statement_node(const ast::ReturnStatement& node, Statement& target) {
    target.kind = StatementKind::Return;
    target.expression = lower_expression(node.value.get());
  }
  void lower_statement_node(const ast::ExpressionStatement& node, Statement& target) {
    target.kind = StatementKind::Expression;
    target.expression = lower_expression(node.expression.get());
  }

  Clock lower_clock(const ast::ClockDeclaration& source) {
    return Clock{source.span, source.name, lower_expression(source.frequency.get())};
  }
  Record lower_record(const ast::RecordDeclaration& source) {
    Record target{source.span, source.name, {}};
    for (const auto& field : source.fields)
      target.fields.push_back(Field{field.span, field.name, resolve(field.type_name, field.span)});
    return target;
  }
  Enum lower_enum(const ast::EnumDeclaration& source) {
    return Enum{source.span, source.name, source.variants};
  }
  Queue lower_queue(const ast::QueueDeclaration& source) {
    return Queue{source.span, source.name, resolve(source.element_type, source.span),
                 lower_expression(source.capacity.get()), source.discipline, source.overflow};
  }
  Resource lower_resource(const ast::ResourceDeclaration& source) {
    return Resource{source.span, source.name, lower_expression(source.capacity.get()), source.unit};
  }
  Pipeline lower_pipeline(const ast::PipelineDeclaration& source) {
    Pipeline target;
    target.span = source.span;
    target.name = source.name;
    target.input_type = resolve(source.input_type, source.span);
    target.output_type = resolve(source.output_type, source.span);
    target.input_queue = source.input_queue;
    target.output_queue = source.output_queue;
    target.clock = source.clock;
    target.latency = lower_expression(source.latency.get());
    target.initiation_interval = lower_expression(source.initiation_interval.get());
    target.max_in_flight = lower_expression(source.max_in_flight.get());
    target.handler_parameter = source.handler_parameter;
    if (source.on_start.has_value()) target.on_start = lower_block(*source.on_start);
    return target;
  }
  Parameter lower_parameter(const ast::ParameterDeclaration& source) {
    Parameter target{source.span, source.name, resolve(source.type_name, source.span), std::nullopt};
    if (source.default_value != nullptr) target.default_value = lower_expression(source.default_value.get());
    return target;
  }
  Port lower_port(const ast::PortDeclaration& source) {
    return Port{source.span, source.is_input, source.name, resolve(source.type_name, source.span)};
  }
  Instance lower_instance(const ast::InstanceDeclaration& source) {
    Instance target{source.span, source.name, source.component_type,
                    lower_expression(source.count.get()), {}};
    for (const auto& parameter : source.parameters) target.parameters.push_back(lower_assignment(parameter));
    return target;
  }

  Component lower_component(const ast::ComponentDeclaration& source) {
    Component target{source.span, source.name, {}};
    for (const auto& member : source.members) {
      target.members.push_back(std::visit([&](const auto& node) -> ComponentMember {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParameterDeclaration>) return lower_parameter(node);
        else if constexpr (std::is_same_v<T, ast::PortDeclaration>) return lower_port(node);
        else if constexpr (std::is_same_v<T, ast::QueueDeclaration>) return lower_queue(node);
        else if constexpr (std::is_same_v<T, ast::ResourceDeclaration>) return lower_resource(node);
        else if constexpr (std::is_same_v<T, ast::PipelineDeclaration>) return lower_pipeline(node);
        else if constexpr (std::is_same_v<T, ast::InstanceDeclaration>) return lower_instance(node);
        else return Connection{node.span, node.source, node.target};
      }, member));
    }
    return target;
  }
  Workload lower_workload(const ast::WorkloadDeclaration& source) {
    return Workload{source.span, source.name, lower_block(source.body)};
  }
  Experiment lower_experiment(const ast::ExperimentDeclaration& source) {
    Experiment target;
    target.span = source.span;
    target.name = source.name;
    target.workload = source.workload;
    target.mode = source.mode;
    target.stop_when = source.stop_when;
    if (source.max_cycles != nullptr) target.max_cycles = lower_expression(source.max_cycles.get());
    if (source.max_events != nullptr) target.max_events = lower_expression(source.max_events.get());
    if (source.seed != nullptr) target.seed = lower_expression(source.seed.get());
    for (const auto& expectation : source.expectations) {
      target.expectations.push_back(Expectation{expectation.span,
          lower_expression(expectation.left.get()), expectation.op,
          lower_expression(expectation.right.get())});
    }
    for (const auto& item : source.exports)
      target.exports.push_back(Export{item.span, item.kind, item.path});
    return target;
  }
  Function lower_function(const ast::FunctionDeclaration& source) {
    Function target;
    target.span = source.span;
    target.name = source.name;
    for (const auto& parameter : source.parameters) {
      target.parameters.push_back(FunctionParameter{parameter.span, parameter.name,
                                                     resolve(parameter.type_name, parameter.span)});
    }
    target.return_type = resolve(source.return_type, source.span);
    target.body = lower_block(source.body);
    return target;
  }

  Declaration lower_declaration(const ast::ModelMember& source) {
    return std::visit([&](const auto& node) -> Declaration {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, ast::ClockDeclaration>) return lower_clock(node);
      else if constexpr (std::is_same_v<T, ast::RecordDeclaration>) return lower_record(node);
      else if constexpr (std::is_same_v<T, ast::EnumDeclaration>) return lower_enum(node);
      else if constexpr (std::is_same_v<T, ast::QueueDeclaration>) return lower_queue(node);
      else if constexpr (std::is_same_v<T, ast::ResourceDeclaration>) return lower_resource(node);
      else if constexpr (std::is_same_v<T, ast::PipelineDeclaration>) return lower_pipeline(node);
      else if constexpr (std::is_same_v<T, ast::ComponentDeclaration>) return lower_component(node);
      else if constexpr (std::is_same_v<T, ast::InstanceDeclaration>) return lower_instance(node);
      else if constexpr (std::is_same_v<T, ast::WorkloadDeclaration>) return lower_workload(node);
      else if constexpr (std::is_same_v<T, ast::ExperimentDeclaration>) return lower_experiment(node);
      else return lower_function(node);
    }, source);
  }

  const ast::Program& program_;
  const semantic::Result& semantic_result_;
  std::unordered_set<std::string> records_;
  std::unordered_set<std::string> enums_;
  std::vector<Diagnostic> diagnostics_;
};

void add_diagnostic(std::vector<Diagnostic>& diagnostics, std::string code,
                    std::string message, lexer::SourceSpan span) {
  diagnostics.push_back({std::move(code), std::move(message), span});
}

void verify_expression(const Expression& expression, std::vector<Diagnostic>& diagnostics) {
  if (expression.type.is_error())
    add_diagnostic(diagnostics, "MASL-I5012", "IR expression has no valid semantic type.", expression.span);
  const auto arity = expression.operands.size();
  switch (expression.kind) {
    case ExpressionKind::Unary:
      if (arity != 1U) add_diagnostic(diagnostics, "MASL-I5013", "Unary IR expression must have exactly one operand.", expression.span);
      break;
    case ExpressionKind::Binary:
      if (arity != 2U) add_diagnostic(diagnostics, "MASL-I5014", "Binary IR expression must have exactly two operands.", expression.span);
      break;
    case ExpressionKind::Field:
      if (arity != 1U || expression.value.empty())
        add_diagnostic(diagnostics, "MASL-I5015", "Field IR expression requires one object and a field name.", expression.span);
      break;
    case ExpressionKind::Call:
      if (expression.value.empty())
        add_diagnostic(diagnostics, "MASL-I5016", "Call IR expression requires a direct callee name.", expression.span);
      break;
    default:
      if (arity != 0U) add_diagnostic(diagnostics, "MASL-I5017", "Literal/name/unit IR expression cannot have operands.", expression.span);
      break;
  }
  if (expression.kind == ExpressionKind::Unit && expression.unit.empty())
    add_diagnostic(diagnostics, "MASL-I5018", "Unit IR expression is missing its unit.", expression.span);
  for (const auto& operand : expression.operands) verify_expression(operand, diagnostics);
}

void verify_assignment(const Assignment& assignment, std::vector<Diagnostic>& diagnostics) {
  if (assignment.target.empty())
    add_diagnostic(diagnostics, "MASL-I5019", "IR assignment target cannot be empty.", assignment.span);
  verify_expression(assignment.value, diagnostics);
}

void verify_block(const Block& block, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : block.statements) {
    if (statement.expression.has_value()) verify_expression(*statement.expression, diagnostics);
    for (const auto& assignment : statement.assignments) verify_assignment(assignment, diagnostics);
    if (statement.kind == StatementKind::Let &&
        (statement.name.empty() || statement.declared_type.empty() || statement.declared_semantic_type.is_error()))
      add_diagnostic(diagnostics, "MASL-I5020", "IR let statement requires a name and resolved declared type.", statement.span);
    if (statement.kind == StatementKind::If) {
      if (!statement.expression.has_value() || statement.expression->type.kind != TypeKind::Bool)
        add_diagnostic(diagnostics, "MASL-I5021", "IR if statement requires a boolean condition.", statement.span);
      verify_block(statement.body, diagnostics);
      if (statement.else_body.has_value()) verify_block(*statement.else_body, diagnostics);
    } else if (statement.kind == StatementKind::Repeat) {
      if (!statement.expression.has_value() || !statement.expression->type.is_integer())
        add_diagnostic(diagnostics, "MASL-I5022", "IR repeat statement requires an integer bound.", statement.span);
      verify_block(statement.body, diagnostics);
    } else if (statement.kind == StatementKind::Assert &&
               (!statement.expression.has_value() || statement.expression->type.kind != TypeKind::Bool)) {
      add_diagnostic(diagnostics, "MASL-I5023", "IR assert statement requires a boolean expression.", statement.span);
    }
    if ((statement.kind == StatementKind::Let || statement.kind == StatementKind::Set ||
         statement.kind == StatementKind::If || statement.kind == StatementKind::Repeat ||
         statement.kind == StatementKind::Assert || statement.kind == StatementKind::Return ||
         statement.kind == StatementKind::Expression) && !statement.expression.has_value())
      add_diagnostic(diagnostics, "MASL-I5024", "IR statement is missing its required expression.", statement.span);
    if (statement.kind == StatementKind::Submit &&
        (statement.name.empty() || statement.declared_type.empty() || statement.declared_semantic_type.kind != TypeKind::Record))
      add_diagnostic(diagnostics, "MASL-I5025", "IR submit statement requires a queue and resolved record type.", statement.span);
  }
}

void verify_pipeline(const Pipeline& pipeline, std::vector<Diagnostic>& diagnostics) {
  if (pipeline.input_type.is_error() || pipeline.output_type.is_error())
    add_diagnostic(diagnostics, "MASL-I5026", "IR pipeline endpoints must have resolved types.", pipeline.span);
  verify_expression(pipeline.latency, diagnostics);
  verify_expression(pipeline.initiation_interval, diagnostics);
  verify_expression(pipeline.max_in_flight, diagnostics);
  if (pipeline.on_start.has_value()) verify_block(*pipeline.on_start, diagnostics);
}

void verify_instance(const Instance& instance, std::vector<Diagnostic>& diagnostics) {
  verify_expression(instance.count, diagnostics);
  for (const auto& parameter : instance.parameters) verify_assignment(parameter, diagnostics);
}

std::string declaration_name(const Declaration& declaration) {
  return std::visit([](const auto& node) { return node.name; }, declaration);
}

}  // namespace

BuildResult lower(const ast::Program& program, const semantic::Result& semantic_result) {
  return Lowerer(program, semantic_result).run();
}

std::vector<Diagnostic> verify(const Module& module) {
  std::vector<Diagnostic> diagnostics;
  if (module.ir_major != 1U)
    add_diagnostic(diagnostics, "MASL-I5027", "Unsupported typed IR major version.", {});
  if (module.name.empty())
    add_diagnostic(diagnostics, "MASL-I5028", "IR module name cannot be empty.", {});

  std::unordered_set<std::string> names;
  for (const auto& declaration : module.declarations) {
    const std::string name = declaration_name(declaration);
    if (name.empty()) add_diagnostic(diagnostics, "MASL-I5029", "IR declaration name cannot be empty.", {});
    else if (!names.insert(name).second)
      add_diagnostic(diagnostics, "MASL-I5030", "Duplicate IR declaration '" + name + "'.", {});

    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<T, Clock>) verify_expression(node.frequency, diagnostics);
      else if constexpr (std::is_same_v<T, Record>) {
        for (const auto& field : node.fields) {
          if (field.type.is_error()) add_diagnostic(diagnostics, "MASL-I5031", "IR record field has unresolved type.", field.span);
        }
      } else if constexpr (std::is_same_v<T, Queue>) {
        if (node.element_type.is_error()) add_diagnostic(diagnostics, "MASL-I5032", "IR queue element type is unresolved.", node.span);
        verify_expression(node.capacity, diagnostics);
      } else if constexpr (std::is_same_v<T, Resource>) verify_expression(node.capacity, diagnostics);
      else if constexpr (std::is_same_v<T, Pipeline>) verify_pipeline(node, diagnostics);
      else if constexpr (std::is_same_v<T, Component>) {
        for (const auto& member : node.members) {
          std::visit([&](const auto& item) {
            using M = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<M, Parameter> || std::is_same_v<M, Port>) {
              if (item.type.is_error()) add_diagnostic(diagnostics, "MASL-I5033", "IR component member has unresolved type.", item.span);
              if constexpr (std::is_same_v<M, Parameter>) {
                if (item.default_value.has_value()) verify_expression(*item.default_value, diagnostics);
              }
            } else if constexpr (std::is_same_v<M, Queue>) {
              if (item.element_type.is_error()) add_diagnostic(diagnostics, "MASL-I5032", "IR queue element type is unresolved.", item.span);
              verify_expression(item.capacity, diagnostics);
            } else if constexpr (std::is_same_v<M, Resource>) verify_expression(item.capacity, diagnostics);
            else if constexpr (std::is_same_v<M, Pipeline>) verify_pipeline(item, diagnostics);
            else if constexpr (std::is_same_v<M, Instance>) verify_instance(item, diagnostics);
          }, member);
        }
      } else if constexpr (std::is_same_v<T, Instance>) verify_instance(node, diagnostics);
      else if constexpr (std::is_same_v<T, Workload>) verify_block(node.body, diagnostics);
      else if constexpr (std::is_same_v<T, Experiment>) {
        if (node.max_cycles.has_value()) verify_expression(*node.max_cycles, diagnostics);
        if (node.max_events.has_value()) verify_expression(*node.max_events, diagnostics);
        if (node.seed.has_value()) verify_expression(*node.seed, diagnostics);
        for (const auto& expectation : node.expectations) {
          verify_expression(expectation.left, diagnostics);
          verify_expression(expectation.right, diagnostics);
        }
      } else if constexpr (std::is_same_v<T, Function>) {
        if (node.return_type.is_error()) add_diagnostic(diagnostics, "MASL-I5034", "IR function return type is unresolved.", node.span);
        for (const auto& parameter : node.parameters) {
          if (parameter.type.is_error()) add_diagnostic(diagnostics, "MASL-I5035", "IR function parameter type is unresolved.", parameter.span);
        }
        verify_block(node.body, diagnostics);
      }
    }, declaration);
  }
  return diagnostics;
}

namespace {
void indent(std::ostringstream& output, std::size_t depth) {
  for (std::size_t i = 0; i < depth; ++i) output << "  ";
}
void dump_expression(const Expression& expression, std::ostringstream& output) {
  output << expression.type.to_string() << ' ';
  switch (expression.kind) {
    case ExpressionKind::IntegerLiteral: output << "int "; break;
    case ExpressionKind::FloatingLiteral: output << "float "; break;
    case ExpressionKind::BooleanLiteral: output << "bool "; break;
    case ExpressionKind::StringLiteral: output << "string "; break;
    case ExpressionKind::Name: output << "name "; break;
    case ExpressionKind::Unit: output << "unit "; break;
    case ExpressionKind::Unary: output << "unary " << lexer::token_kind_name(expression.operation) << ' '; break;
    case ExpressionKind::Binary: output << "binary " << lexer::token_kind_name(expression.operation) << ' '; break;
    case ExpressionKind::Call: output << "call "; break;
    case ExpressionKind::Field: output << "field "; break;
  }
  if (!expression.value.empty()) output << '`' << expression.value << '`';
  if (!expression.unit.empty()) output << " [" << expression.unit << (expression.clock_domain.empty() ? "" : "@" + expression.clock_domain) << ']';
  if (!expression.symbol_arguments.empty()) {
    output << " symbols=";
    for (std::size_t index = 0; index < expression.symbol_arguments.size(); ++index) {
      if (index != 0U) output << ',';
      output << expression.symbol_arguments[index];
    }
  }
  if (!expression.operands.empty()) {
    output << " (";
    for (std::size_t index = 0; index < expression.operands.size(); ++index) {
      if (index != 0U) output << ", ";
      output << expression.operands[index].type.to_string();
    }
    output << ')';
  }
}
void dump_block(const Block& block, std::ostringstream& output, std::size_t depth) {
  for (const auto& statement : block.statements) {
    indent(output, depth);
    switch (statement.kind) {
      case StatementKind::Let: output << "let " << statement.name << " : " << statement.declared_type; break;
      case StatementKind::Set: output << "set " << statement.name; break;
      case StatementKind::If: output << "if"; break;
      case StatementKind::Repeat: output << "repeat " << statement.name; break;
      case StatementKind::Submit: output << "submit " << statement.name << " : " << statement.declared_type; break;
      case StatementKind::Assert: output << "assert"; break;
      case StatementKind::Return: output << "return"; break;
      case StatementKind::Expression: output << "expression"; break;
    }
    if (statement.expression.has_value()) {
      output << " = ";
      dump_expression(*statement.expression, output);
    }
    output << '\n';
    if (statement.kind == StatementKind::If || statement.kind == StatementKind::Repeat) {
      dump_block(statement.body, output, depth + 1U);
      if (statement.else_body.has_value()) dump_block(*statement.else_body, output, depth + 1U);
    }
  }
}
}  // namespace

std::string dump(const Module& module) {
  std::ostringstream output;
  output << "MASL Typed IR " << module.ir_major << '.' << module.ir_minor
         << " | language " << module.language_major << '.' << module.language_minor
         << " | model " << module.name << '\n';
  for (const auto& declaration : module.declarations) {
    std::visit([&](const auto& node) {
      using T = std::decay_t<decltype(node)>;
      output << "  ";
      if constexpr (std::is_same_v<T, Clock>) {
        output << "clock " << node.name << " : "; dump_expression(node.frequency, output);
      } else if constexpr (std::is_same_v<T, Record>) {
        output << "record " << node.name << " {";
        for (const auto& field : node.fields) output << ' ' << field.name << ':' << field.type.to_string() << ';';
        output << " }";
      } else if constexpr (std::is_same_v<T, Enum>) {
        output << "enum " << node.name << " {";
        for (const auto& variant : node.variants) output << ' ' << variant;
        output << " }";
      } else if constexpr (std::is_same_v<T, Queue>) {
        output << "queue " << node.name << " : " << node.element_type.to_string() << " capacity=";
        dump_expression(node.capacity, output);
      } else if constexpr (std::is_same_v<T, Resource>) {
        output << "resource " << node.name << " capacity="; dump_expression(node.capacity, output);
        output << " unit=" << node.unit;
      } else if constexpr (std::is_same_v<T, Pipeline>) {
        output << "pipeline " << node.name << " : " << node.input_type.to_string() << " -> " << node.output_type.to_string()
               << " clock=" << node.clock << " latency="; dump_expression(node.latency, output);
      } else if constexpr (std::is_same_v<T, Component>) {
        output << "component " << node.name << " members=" << node.members.size();
      } else if constexpr (std::is_same_v<T, Instance>) {
        output << "instances " << node.name << " : " << node.component_type << " count="; dump_expression(node.count, output);
      } else if constexpr (std::is_same_v<T, Workload>) {
        output << "workload " << node.name << '\n'; dump_block(node.body, output, 2U); return;
      } else if constexpr (std::is_same_v<T, Experiment>) {
        output << "experiment " << node.name << " workload=" << node.workload << " mode=" << node.mode;
        output << " stop_when=" << node.stop_when << '\n';
        for (const auto& expectation : node.expectations) {
          output << "    expect ";
          dump_expression(expectation.left, output);
          output << ' ' << lexer::token_kind_name(expectation.operation) << ' ';
          dump_expression(expectation.right, output);
          output << '\n';
        }
        for (const auto& export_item : node.exports)
          output << "    export " << export_item.kind << " = `" << export_item.path << "`\n";
        return;
      } else if constexpr (std::is_same_v<T, Function>) {
        output << "fn " << node.name << " -> " << node.return_type.to_string() << " params=" << node.parameters.size()
               << '\n'; dump_block(node.body, output, 2U); return;
      }
      output << '\n';
    }, declaration);
  }
  return output.str();
}

}  // namespace masl::ir
