#include "masl/semantic/analyzer.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace masl::semantic {

bool Type::is_integer() const noexcept {
  return kind == TypeKind::SignedInteger || kind == TypeKind::UnsignedInteger ||
         kind == TypeKind::IntegerLiteral;
}

bool Type::is_numeric() const noexcept {
  return is_integer() || kind == TypeKind::FloatingPoint ||
         kind == TypeKind::FloatingLiteral;
}

std::string Type::to_string() const {
  switch (kind) {
    case TypeKind::Error: return "<error>";
    case TypeKind::Bool: return "bool";
    case TypeKind::SignedInteger: return "i" + std::to_string(bits);
    case TypeKind::UnsignedInteger: return "u" + std::to_string(bits);
    case TypeKind::FloatingPoint: return "f" + std::to_string(bits);
    case TypeKind::String: return "string";
    case TypeKind::Record: return "record " + nominal_name;
    case TypeKind::Enum: return "enum " + nominal_name;
    case TypeKind::Time: return "time";
    case TypeKind::Frequency: return "frequency";
    case TypeKind::Cycles: return "cycles(" + clock_domain + ")";
    case TypeKind::Bytes: return "bytes";
    case TypeKind::Bandwidth: return "bandwidth(" + clock_domain + ")";
    case TypeKind::Latency: return "latency";
    case TypeKind::Count: return "count";
    case TypeKind::Address: return "address";
    case TypeKind::IntegerLiteral: return "integer literal";
    case TypeKind::FloatingLiteral: return "floating-point literal";
  }
  return "<unknown>";
}

namespace {

using ModelMember = ast::ModelMember;
using ComponentMember = ast::ComponentMember;

struct Variable final {
  Type type;
  bool read_only{false};
  std::optional<std::int64_t> minimum;
  std::optional<std::int64_t> maximum;
};

struct RecordInfo final {
  const ast::RecordDeclaration* declaration{nullptr};
  std::unordered_map<std::string, Type> fields;
};

struct FunctionInfo final {
  const ast::FunctionDeclaration* declaration{nullptr};
  std::vector<Type> parameters;
  Type return_type;
};

struct QueueInfo final {
  std::string element_type;
  Type resolved_element_type;
};

struct PipelineInfo final {
  std::string input_type;
  std::string output_type;
};

struct ComponentInfo final {
  const ast::ComponentDeclaration* declaration{nullptr};
  std::unordered_map<std::string, Type> parameters;
};

struct IntegerConstant final {
  std::int64_t value{0};
};

struct IntegerRange final {
  std::int64_t minimum{0};
  std::int64_t maximum{0};
};

[[nodiscard]] std::string remove_underscores(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    if (character != '_') result.push_back(character);
  }
  return result;
}

[[nodiscard]] bool is_integer_spelling(std::string_view value) {
  return value.find_first_of(".eE") == std::string_view::npos;
}

[[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) {
  const std::string cleaned = remove_underscores(text);
  if (cleaned.empty()) return std::nullopt;
  std::uint64_t value = 0;
  const auto parsed = std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != cleaned.data() + cleaned.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<long double> parse_real(std::string_view text) {
  const std::string cleaned = remove_underscores(text);
  if (cleaned.empty()) return std::nullopt;
  char* end = nullptr;
  const long double value = std::strtold(cleaned.c_str(), &end);
  if (end != cleaned.c_str() + cleaned.size() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<IntegerConstant> eval_integer_constant(const ast::Expr& expression) {
  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expression.node)) {
    if (literal->kind != ast::LiteralExpr::Kind::Number ||
        !is_integer_spelling(literal->value)) return std::nullopt;
    const auto value = parse_u64(literal->value);
    if (!value.has_value() || *value > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) return std::nullopt;
    return IntegerConstant{static_cast<std::int64_t>(*value)};
  }
  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expression.node)) {
    const auto operand = eval_integer_constant(*unary->operand);
    if (!operand.has_value()) {
      if (unary->op == lexer::TokenKind::Minus && unary->operand != nullptr) {
        if (const auto* literal = std::get_if<ast::LiteralExpr>(&unary->operand->node)) {
          if (literal->kind == ast::LiteralExpr::Kind::Number &&
              is_integer_spelling(literal->value)) {
            const auto magnitude = parse_u64(literal->value);
            const std::uint64_t minimum_magnitude =
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U;
            if (magnitude.has_value() && *magnitude == minimum_magnitude) {
              return IntegerConstant{std::numeric_limits<std::int64_t>::min()};
            }
          }
        }
      }
      return std::nullopt;
    }
    if (unary->op == lexer::TokenKind::Plus) return operand;
    if (unary->op == lexer::TokenKind::Minus) {
      if (operand->value == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
      return IntegerConstant{-operand->value};
    }
    return std::nullopt;
  }
  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expression.node)) {
    const auto left = eval_integer_constant(*binary->left);
    const auto right = eval_integer_constant(*binary->right);
    if (!left.has_value() || !right.has_value()) return std::nullopt;
    const std::int64_t a = left->value;
    const std::int64_t b = right->value;
    std::int64_t result = 0;
    switch (binary->op) {
      case lexer::TokenKind::Plus:
        if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
            (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) return std::nullopt;
        result = a + b;
        break;
      case lexer::TokenKind::Minus:
        if ((b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
            (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)) return std::nullopt;
        result = a - b;
        break;
      case lexer::TokenKind::Star:
        if (a != 0 && b != 0) {
          if (a == -1 && b == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
          if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
          if (a > 0) {
            if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) return std::nullopt;
            if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) return std::nullopt;
          } else {
            if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) return std::nullopt;
            if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b) return std::nullopt;
          }
        }
        result = a * b;
        break;
      case lexer::TokenKind::Slash:
        if (b == 0 || (a == std::numeric_limits<std::int64_t>::min() && b == -1)) return std::nullopt;
        result = a / b;
        break;
      case lexer::TokenKind::Percent:
        if (b == 0 || (a == std::numeric_limits<std::int64_t>::min() && b == -1)) return std::nullopt;
        result = a % b;
        break;
      default: return std::nullopt;
    }
    return IntegerConstant{result};
  }
  return std::nullopt;
}

[[nodiscard]] bool fits_signed(std::int64_t value, std::uint8_t bits) {
  if (bits >= 64U) return true;
  const std::int64_t maximum = (std::int64_t{1} << (bits - 1U)) - 1;
  const std::int64_t minimum = -(std::int64_t{1} << (bits - 1U));
  return value >= minimum && value <= maximum;
}

[[nodiscard]] bool fits_unsigned(std::int64_t value, std::uint8_t bits) {
  if (value < 0) return false;
  if (bits >= 63U) return true;
  const std::uint64_t maximum = (std::uint64_t{1} << bits) - 1U;
  return static_cast<std::uint64_t>(value) <= maximum;
}

[[nodiscard]] bool is_comparison(lexer::TokenKind op) {
  using lexer::TokenKind;
  return op == TokenKind::Equal || op == TokenKind::NotEqual ||
         op == TokenKind::Less || op == TokenKind::LessEqual ||
         op == TokenKind::Greater || op == TokenKind::GreaterEqual;
}

class SemanticPass final {
 public:
  Result run(const ast::Program& program) {
    result_ = {};
    collect_declarations(program);
    build_record_fields();
    build_queue_signatures();
    build_function_signatures();
    build_component_signatures();
    validate_declarations(program);
    return std::move(result_);
  }

 private:
  enum class BlockMode { Function, Workload, Handler };

  void diagnostic(std::string code, std::string message, lexer::SourceSpan span) {
    result_.diagnostics.push_back(Diagnostic{std::move(code), std::move(message), span});
  }

  void duplicate(std::string_view name, lexer::SourceSpan span, std::string_view category) {
    diagnostic("MASL-E3001", "Duplicate " + std::string(category) + " '" +
                   std::string(name) + "'.",
               span);
  }

  void duplicate_property(std::string_view name, unsigned int count,
                          lexer::SourceSpan span) {
    if (count > 1U) {
      diagnostic("MASL-E3063", "Property '" + std::string(name) +
                     "' is declared more than once.", span);
    }
  }

  [[nodiscard]] Type builtin_type(std::string_view name) const {
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
      const char prefix = name.front();
      const std::string_view width_text = name.substr(1U);
      unsigned int width = 0;
      const auto parsed = std::from_chars(width_text.data(),
                                          width_text.data() + width_text.size(), width);
      if (parsed.ec == std::errc{} && parsed.ptr == width_text.data() + width_text.size() &&
          (width == 8U || width == 16U || width == 32U || width == 64U)) {
        if (prefix == 'i') return Type{TypeKind::SignedInteger, static_cast<std::uint8_t>(width)};
        if (prefix == 'u') return Type{TypeKind::UnsignedInteger, static_cast<std::uint8_t>(width)};
        if (prefix == 'f' && (width == 32U || width == 64U)) {
          return Type{TypeKind::FloatingPoint, static_cast<std::uint8_t>(width)};
        }
      }
    }
    return Type{};
  }

  [[nodiscard]] Type resolve_type(std::string_view name, lexer::SourceSpan span,
                                  bool report_unknown = true) {
    Type type = builtin_type(name);
    if (!type.is_error()) return type;
    if (records_.contains(std::string(name))) {
      Type result;
      result.kind = TypeKind::Record;
      result.nominal_name = std::string(name);
      return result;
    }
    if (enums_.contains(std::string(name))) {
      Type result;
      result.kind = TypeKind::Enum;
      result.nominal_name = std::string(name);
      return result;
    }
    if (report_unknown) {
      diagnostic("MASL-E3002", "Unknown type '" + std::string(name) + "'.", span);
    }
    return Type{};
  }

  void collect_declarations(const ast::Program& program) {
    std::unordered_set<std::string> names;
    for (const ModelMember& member : program.model.members) {
      std::visit([&](const auto& declaration) {
        using T = std::decay_t<decltype(declaration)>;
        if (!names.insert(declaration.name).second) {
          duplicate(declaration.name, declaration.span, "model declaration");
          return;
        }
        if constexpr (std::is_same_v<T, ast::RecordDeclaration>) {
          records_.emplace(declaration.name, RecordInfo{&declaration, {}});
        } else if constexpr (std::is_same_v<T, ast::EnumDeclaration>) {
          enums_.emplace(declaration.name, &declaration);
        } else if constexpr (std::is_same_v<T, ast::ClockDeclaration>) {
          clocks_.insert(declaration.name);
        } else if constexpr (std::is_same_v<T, ast::QueueDeclaration>) {
          queues_.emplace(declaration.name, QueueInfo{declaration.element_type, {}});
        } else if constexpr (std::is_same_v<T, ast::PipelineDeclaration>) {
          pipelines_.emplace(declaration.name,
                             PipelineInfo{declaration.input_type, declaration.output_type});
        } else if constexpr (std::is_same_v<T, ast::ComponentDeclaration>) {
          components_.emplace(declaration.name, ComponentInfo{&declaration, {}});
        } else if constexpr (std::is_same_v<T, ast::FunctionDeclaration>) {
          functions_.emplace(declaration.name, FunctionInfo{&declaration, {}, {}});
        } else if constexpr (std::is_same_v<T, ast::WorkloadDeclaration>) {
          workloads_.insert(declaration.name);
        } else if constexpr (std::is_same_v<T, ast::ExperimentDeclaration>) {
          experiments_.insert(declaration.name);
        }
      }, member);
    }
  }

  void build_record_fields() {
    for (auto& [name, info] : records_) {
      static_cast<void>(name);
      std::unordered_set<std::string> field_names;
      for (const auto& field : info.declaration->fields) {
        if (!field_names.insert(field.name).second) {
          duplicate(field.name, field.span, "record field");
          continue;
        }
        const Type field_type = resolve_type(field.type_name, field.span);
        if (!field_type.is_error()) info.fields.emplace(field.name, field_type);
      }
      if (info.declaration->fields.empty()) {
        diagnostic("MASL-E3003", "A record must declare at least one field.",
                   info.declaration->span);
      }
    }
  }

  void build_queue_signatures() {
    for (auto& [name, info] : queues_) {
      static_cast<void>(name);
      info.resolved_element_type = resolve_type(info.element_type, {}, false);
    }
  }

  void build_component_signatures() {
    for (auto& [name, info] : components_) {
      static_cast<void>(name);
      for (const ComponentMember& member : info.declaration->members) {
        if (const auto* parameter = std::get_if<ast::ParameterDeclaration>(&member)) {
          if (!info.parameters.contains(parameter->name)) {
            info.parameters.emplace(parameter->name,
                                    resolve_type(parameter->type_name, parameter->span, false));
          }
        }
      }
    }
  }

  void build_function_signatures() {
    for (auto& [name, info] : functions_) {
      static_cast<void>(name);
      const auto& declaration = *info.declaration;
      std::unordered_set<std::string> parameter_names;
      for (const auto& parameter : declaration.parameters) {
        if (!parameter_names.insert(parameter.name).second) {
          duplicate(parameter.name, parameter.span, "function parameter");
        }
        info.parameters.push_back(resolve_type(parameter.type_name, parameter.span));
      }
      info.return_type = resolve_type(declaration.return_type, declaration.span);
    }
  }

  [[nodiscard]] std::optional<IntegerRange> integer_range(const ast::Expr& expression) {
    if (const auto exact = eval_integer_constant(expression); exact.has_value()) {
      return IntegerRange{exact->value, exact->value};
    }
    if (const auto* literal = std::get_if<ast::LiteralExpr>(&expression.node)) {
      if (literal->kind != ast::LiteralExpr::Kind::Number ||
          !is_integer_spelling(literal->value)) return std::nullopt;
      const auto value = parse_u64(literal->value);
      if (!value.has_value() || *value > static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max())) return std::nullopt;
      const auto integer = static_cast<std::int64_t>(*value);
      return IntegerRange{integer, integer};
    }
    if (const auto* name = std::get_if<ast::NameExpr>(&expression.node)) {
      Variable* variable = lookup_variable(name->name);
      if (variable != nullptr && variable->minimum.has_value() &&
          variable->maximum.has_value()) {
        return IntegerRange{*variable->minimum, *variable->maximum};
      }
      return std::nullopt;
    }
    if (const auto* unary = std::get_if<ast::UnaryExpr>(&expression.node)) {
      if (unary->operand == nullptr) return std::nullopt;
      const auto range = integer_range(*unary->operand);
      if (!range.has_value()) return std::nullopt;
      if (unary->op == lexer::TokenKind::Plus) return range;
      if (unary->op == lexer::TokenKind::Minus) {
        if (range->minimum == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
        return IntegerRange{-range->maximum, -range->minimum};
      }
      return std::nullopt;
    }
    if (const auto* binary = std::get_if<ast::BinaryExpr>(&expression.node)) {
      if (binary->left == nullptr || binary->right == nullptr) return std::nullopt;
      const auto left = integer_range(*binary->left);
      const auto right = integer_range(*binary->right);
      if (!left.has_value() || !right.has_value()) return std::nullopt;
      auto safe_add = [](std::int64_t a, std::int64_t b) -> std::optional<std::int64_t> {
        if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
            (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) return std::nullopt;
        return a + b;
      };
      auto safe_sub = [](std::int64_t a, std::int64_t b) -> std::optional<std::int64_t> {
        if ((b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
            (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)) return std::nullopt;
        return a - b;
      };
      auto safe_mul = [](std::int64_t a, std::int64_t b) -> std::optional<std::int64_t> {
        if (a == 0 || b == 0) return 0;
        if (a == -1 && b == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
        if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
        if (a > 0) {
          if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) return std::nullopt;
          if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) return std::nullopt;
        } else {
          if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) return std::nullopt;
          if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b) return std::nullopt;
        }
        return a * b;
      };
      if (binary->op == lexer::TokenKind::Plus) {
        const auto minimum = safe_add(left->minimum, right->minimum);
        const auto maximum = safe_add(left->maximum, right->maximum);
        if (minimum && maximum) return IntegerRange{*minimum, *maximum};
      } else if (binary->op == lexer::TokenKind::Minus) {
        const auto minimum = safe_sub(left->minimum, right->maximum);
        const auto maximum = safe_sub(left->maximum, right->minimum);
        if (minimum && maximum) return IntegerRange{*minimum, *maximum};
      } else if (binary->op == lexer::TokenKind::Star) {
        const auto p1 = safe_mul(left->minimum, right->minimum);
        const auto p2 = safe_mul(left->minimum, right->maximum);
        const auto p3 = safe_mul(left->maximum, right->minimum);
        const auto p4 = safe_mul(left->maximum, right->maximum);
        if (p1 && p2 && p3 && p4) {
          return IntegerRange{std::min({*p1, *p2, *p3, *p4}),
                              std::max({*p1, *p2, *p3, *p4})};
        }
      }
      return std::nullopt;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<IntegerConstant> constant(const ast::Expr* expression) const {
    if (expression == nullptr) return std::nullopt;
    return eval_integer_constant(*expression);
  }

  [[nodiscard]] bool positive_integer(const ast::Expr* expression,
                                      std::string_view purpose) {
    if (expression == nullptr) {
      diagnostic("MASL-E3004", "Missing " + std::string(purpose) + ".", {});
      return false;
    }
    const auto value = constant(expression);
    if (!value.has_value() || value->value <= 0) {
      diagnostic("MASL-E3004", std::string(purpose) +
                     " must be a positive compile-time integer constant.", expression->span);
      return false;
    }
    return true;
  }

  [[nodiscard]] bool non_negative_integer(const ast::Expr* expression,
                                          std::string_view purpose) {
    if (expression == nullptr) {
      diagnostic("MASL-E3004", "Missing " + std::string(purpose) + ".", {});
      return false;
    }
    const auto value = constant(expression);
    if (!value.has_value() || value->value < 0) {
      diagnostic("MASL-E3004", std::string(purpose) +
                     " must be a non-negative compile-time integer constant.", expression->span);
      return false;
    }
    return true;
  }

  void validate_declarations(const ast::Program& program) {
    for (const ModelMember& member : program.model.members) {
      std::visit([&](const auto& declaration) { validate_one(declaration); }, member);
    }
  }

  void validate_one(const ast::ClockDeclaration& declaration) {
    if (declaration.frequency == nullptr) {
      diagnostic("MASL-E3005", "Clock frequency is required.", declaration.span);
      return;
    }
    const Type frequency = check_expression(*declaration.frequency);
    if (frequency.kind != TypeKind::Frequency) {
      type_mismatch("Clock frequency", frequency, Type{TypeKind::Frequency},
                    declaration.frequency->span);
    }
    const auto* unit = std::get_if<ast::UnitExpr>(&declaration.frequency->node);
    if (unit == nullptr || !unit_value_positive(unit->number)) {
      diagnostic("MASL-E3005", "Clock frequency must be a positive finite frequency literal.",
                 declaration.frequency->span);
    }
  }

  [[nodiscard]] bool unit_value_positive(std::string_view value) const {
    const auto parsed = parse_real(value);
    return parsed.has_value() && *parsed > 0.0L;
  }

  void validate_one(const ast::RecordDeclaration&) {}

  void validate_one(const ast::EnumDeclaration& declaration) {
    std::unordered_set<std::string> variants;
    for (const auto& variant : declaration.variants) {
      if (!variants.insert(variant).second) {
        duplicate(variant, declaration.span, "enum variant");
      }
    }
    if (declaration.variants.empty()) {
      diagnostic("MASL-E3006", "An enum must contain at least one variant.", declaration.span);
    }
  }

  void validate_one(const ast::QueueDeclaration& declaration) {
    duplicate_property("capacity", declaration.capacity_count, declaration.span);
    duplicate_property("discipline", declaration.discipline_count, declaration.span);
    duplicate_property("overflow", declaration.overflow_count, declaration.span);
    const Type element = resolve_type(declaration.element_type, declaration.span);
    if (declaration.capacity == nullptr) {
      diagnostic("MASL-E3004", "Queue capacity is required.", declaration.span);
    } else {
      const Type capacity_type = check_expression(*declaration.capacity);
      if (!capacity_type.is_integer() && !capacity_type.is_error()) {
        diagnostic("MASL-E3007", "Queue capacity must be an integer.", declaration.capacity->span);
      }
      static_cast<void>(positive_integer(declaration.capacity.get(), "Queue capacity"));
    }
    if (declaration.discipline != "fifo" && declaration.discipline != "priority" &&
        declaration.discipline != "round_robin") {
      diagnostic("MASL-E3008", "Unknown queue discipline '" + declaration.discipline + "'.",
                 declaration.span);
    }
    if (declaration.overflow != "block" && declaration.overflow != "reject" &&
        declaration.overflow != "drop") {
      diagnostic("MASL-E3008", "Unknown queue overflow policy '" + declaration.overflow + "'.",
                 declaration.span);
    }
    auto found = queues_.find(declaration.name);
    if (found != queues_.end()) found->second.resolved_element_type = element;
  }

  void validate_one(const ast::ResourceDeclaration& declaration) {
    duplicate_property("capacity", declaration.capacity_count, declaration.span);
    duplicate_property("unit", declaration.unit_count, declaration.span);
    if (declaration.capacity == nullptr) {
      diagnostic("MASL-E3004", "Resource capacity is required.", declaration.span);
    } else {
      const Type capacity_type = check_expression(*declaration.capacity);
      if (!capacity_type.is_integer() && !capacity_type.is_error()) {
        diagnostic("MASL-E3007", "Resource capacity must be an integer.", declaration.capacity->span);
      }
      static_cast<void>(positive_integer(declaration.capacity.get(), "Resource capacity"));
    }
    if (declaration.unit.empty()) {
      diagnostic("MASL-E3009", "Resource unit must not be empty.", declaration.span);
    }
  }

  void validate_one(const ast::PipelineDeclaration& declaration) {
    duplicate_property("input", declaration.input_count, declaration.span);
    duplicate_property("output", declaration.output_count, declaration.span);
    duplicate_property("clock", declaration.clock_count, declaration.span);
    duplicate_property("latency", declaration.latency_count, declaration.span);
    duplicate_property("initiation_interval", declaration.initiation_interval_count, declaration.span);
    duplicate_property("max_in_flight", declaration.max_in_flight_count, declaration.span);
    duplicate_property("on_start", declaration.on_start_count, declaration.span);
    const Type input_type = resolve_type(declaration.input_type, declaration.span);
    const Type output_type = resolve_type(declaration.output_type, declaration.span);
    if (!clocks_.contains(declaration.clock)) {
      diagnostic("MASL-E3010", "Pipeline references unknown clock '" + declaration.clock + "'.",
                 declaration.span);
    }
    check_pipeline_queue(declaration.input_queue, input_type, true, declaration.span, nullptr);
    check_pipeline_queue(declaration.output_queue, output_type, false, declaration.span, nullptr);
    validate_pipeline_timing(declaration);
    if (declaration.on_start.has_value()) {
      if (!records_.contains(declaration.input_type)) {
        diagnostic("MASL-E3011", "Pipeline on_start handler requires a record input type.",
                   declaration.span);
      } else {
        scopes_.clear();
        scopes_.emplace_back();
        const auto record_type = resolve_type(declaration.input_type, declaration.span, false);
        declare_variable(declaration.handler_parameter, record_type, true, declaration.span);
        check_block(*declaration.on_start, BlockMode::Handler, Type{}, true);
        scopes_.clear();
      }
    }
  }

  void validate_pipeline_timing(const ast::PipelineDeclaration& declaration) {
    const Type expected_cycles{TypeKind::Cycles, 0, {}, declaration.clock};
    const auto check_duration = [&](const ast::Expr* expression, std::string_view field) {
      if (expression == nullptr) {
        diagnostic("MASL-E3012", "Pipeline " + std::string(field) + " is required.",
                   declaration.span);
        return;
      }
      const Type type = check_expression(*expression);
      const bool valid = type.kind == TypeKind::Time || type.kind == TypeKind::Latency ||
                         (type.kind == TypeKind::Cycles && type.clock_domain == declaration.clock);
      if (!valid && !type.is_error()) {
        type_mismatch("Pipeline " + std::string(field), type, expected_cycles, expression->span);
      }
      const auto* unit = std::get_if<ast::UnitExpr>(&expression->node);
      if (unit != nullptr && !unit_value_positive(unit->number)) {
        diagnostic("MASL-E3012", "Pipeline " + std::string(field) + " must be positive.",
                   expression->span);
      }
    };
    check_duration(declaration.latency.get(), "latency");
    check_duration(declaration.initiation_interval.get(), "initiation_interval");
    if (declaration.max_in_flight == nullptr) {
      diagnostic("MASL-E3004", "Pipeline max_in_flight is required.", declaration.span);
    } else {
      const Type type = check_expression(*declaration.max_in_flight);
      if (!type.is_integer() && !type.is_error()) {
        diagnostic("MASL-E3007", "max_in_flight must be an integer.",
                   declaration.max_in_flight->span);
      }
      static_cast<void>(positive_integer(declaration.max_in_flight.get(), "max_in_flight"));
    }
  }

  void check_pipeline_queue(std::string_view queue_name, const Type& expected,
                            bool input, lexer::SourceSpan span,
                            const std::unordered_map<std::string, Type>* local_endpoints) {
    if (queue_name.empty()) {
      diagnostic("MASL-E3013", std::string("Pipeline ") + (input ? "input" : "output") +
                     " endpoint is required.", span);
      return;
    }
    const auto queue = queues_.find(std::string(queue_name));
    if (queue != queues_.end()) {
      if (!same_type(queue->second.resolved_element_type, expected)) {
        type_mismatch("Pipeline queue element", queue->second.resolved_element_type,
                      expected, span);
      }
      return;
    }
    if (local_endpoints != nullptr) {
      const auto endpoint = local_endpoints->find(std::string(queue_name));
      if (endpoint != local_endpoints->end()) {
        if (!same_type(endpoint->second, expected)) {
          type_mismatch("Pipeline port type", endpoint->second, expected, span);
        }
        return;
      }
    }
    diagnostic("MASL-E3014", "Pipeline references unknown " +
                   std::string(input ? "input" : "output") + " queue/port '" +
                   std::string(queue_name) + "'.", span);
  }

  void validate_one(const ast::ComponentDeclaration& declaration) {
    std::unordered_set<std::string> member_names;
    std::unordered_map<std::string, Type> endpoints;
    std::unordered_map<std::string, Type> parameters;
    for (const ComponentMember& member : declaration.members) {
      std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (!std::is_same_v<T, ast::ConnectionDeclaration>) {
          if (!member_names.insert(item.name).second) {
            duplicate(item.name, item.span, "component member");
          }
        }
        if constexpr (std::is_same_v<T, ast::ParameterDeclaration>) {
          parameters.emplace(item.name, resolve_type(item.type_name, item.span));
        } else if constexpr (std::is_same_v<T, ast::PortDeclaration>) {
          endpoints.emplace(item.name, resolve_type(item.type_name, item.span));
        } else if constexpr (std::is_same_v<T, ast::QueueDeclaration>) {
          endpoints.emplace(item.name, resolve_type(item.element_type, item.span));
        }
      }, member);
    }
    for (const ComponentMember& member : declaration.members) {
      std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, ast::ParameterDeclaration>) {
          const auto parameter = parameters.find(item.name);
          const Type type = parameter != parameters.end() ? parameter->second : Type{};
          if (item.default_value != nullptr) {
            static_cast<void>(check_expression(*item.default_value, type));
          }
        } else if constexpr (std::is_same_v<T, ast::PortDeclaration>) {
          static_cast<void>(item);
        } else if constexpr (std::is_same_v<T, ast::QueueDeclaration>) {
          validate_component_queue_or_resource(item);
        } else if constexpr (std::is_same_v<T, ast::ResourceDeclaration>) {
          validate_component_queue_or_resource(item);
        } else if constexpr (std::is_same_v<T, ast::PipelineDeclaration>) {
          duplicate_property("input", item.input_count, item.span);
          duplicate_property("output", item.output_count, item.span);
          duplicate_property("clock", item.clock_count, item.span);
          duplicate_property("latency", item.latency_count, item.span);
          duplicate_property("initiation_interval", item.initiation_interval_count, item.span);
          duplicate_property("max_in_flight", item.max_in_flight_count, item.span);
          duplicate_property("on_start", item.on_start_count, item.span);
          const Type input_type = resolve_type(item.input_type, item.span);
          const Type output_type = resolve_type(item.output_type, item.span);
          check_component_pipeline_endpoint(item.input_queue, input_type, true,
                                            item.span, endpoints, declaration);
          check_component_pipeline_endpoint(item.output_queue, output_type, false,
                                            item.span, endpoints, declaration);
          if (!clocks_.contains(item.clock)) {
            diagnostic("MASL-E3010", "Component pipeline references unknown clock '" +
                           item.clock + "'.", item.span);
          }
          validate_pipeline_timing(item);
          if (item.on_start.has_value()) {
            if (!records_.contains(item.input_type)) {
              diagnostic("MASL-E3011", "Pipeline on_start handler requires a record input type.",
                         item.span);
            } else {
              scopes_.clear();
              scopes_.emplace_back();
              declare_variable(item.handler_parameter,
                               resolve_type(item.input_type, item.span, false), true, item.span);
              check_block(*item.on_start, BlockMode::Handler, Type{}, true);
              scopes_.clear();
            }
          }
        } else if constexpr (std::is_same_v<T, ast::InstanceDeclaration>) {
          validate_instance(item);
        } else if constexpr (std::is_same_v<T, ast::ConnectionDeclaration>) {
          if (item.source.empty() || item.target.empty()) {
            diagnostic("MASL-E3015", "Connection endpoints must not be empty.", item.span);
          }
        }
      }, member);
    }
    auto component = components_.find(declaration.name);
    if (component != components_.end()) component->second.parameters = std::move(parameters);
  }

  void validate_component_queue_or_resource(const ast::QueueDeclaration& declaration) {
    duplicate_property("capacity", declaration.capacity_count, declaration.span);
    duplicate_property("discipline", declaration.discipline_count, declaration.span);
    duplicate_property("overflow", declaration.overflow_count, declaration.span);
    const Type type = resolve_type(declaration.element_type, declaration.span);
    static_cast<void>(type);
    if (declaration.capacity == nullptr) {
      diagnostic("MASL-E3004", "Component queue capacity is required.", declaration.span);
    } else {
      const Type capacity_type = check_expression(*declaration.capacity);
      if (!capacity_type.is_integer() && !capacity_type.is_error()) {
        diagnostic("MASL-E3007", "Component queue capacity must be an integer.", declaration.capacity->span);
      }
      static_cast<void>(positive_integer(declaration.capacity.get(), "Component queue capacity"));
    }
    if (declaration.discipline != "fifo" && declaration.discipline != "priority" &&
        declaration.discipline != "round_robin") {
      diagnostic("MASL-E3008", "Unknown component queue discipline.", declaration.span);
    }
    if (declaration.overflow != "block" && declaration.overflow != "reject" &&
        declaration.overflow != "drop") {
      diagnostic("MASL-E3008", "Unknown component queue overflow policy.", declaration.span);
    }
  }

  void validate_component_queue_or_resource(const ast::ResourceDeclaration& declaration) {
    duplicate_property("capacity", declaration.capacity_count, declaration.span);
    duplicate_property("unit", declaration.unit_count, declaration.span);
    if (declaration.capacity == nullptr) {
      diagnostic("MASL-E3004", "Component resource capacity is required.", declaration.span);
    } else {
      const Type capacity_type = check_expression(*declaration.capacity);
      if (!capacity_type.is_integer() && !capacity_type.is_error()) {
        diagnostic("MASL-E3007", "Component resource capacity must be an integer.", declaration.capacity->span);
      }
      static_cast<void>(positive_integer(declaration.capacity.get(), "Component resource capacity"));
    }
    if (declaration.unit.empty()) {
      diagnostic("MASL-E3009", "Component resource unit must not be empty.", declaration.span);
    }
  }

  void check_component_pipeline_endpoint(std::string_view endpoint, const Type& expected,
                                         bool input, lexer::SourceSpan span,
                                         std::unordered_map<std::string, Type>& endpoints,
                                         const ast::ComponentDeclaration& component) {
    if (!endpoint.empty()) {
      // Local queues and ports are collected in a first pass below; this fallback
      // keeps declaration order from changing the meaning of a component.
      for (const ComponentMember& member : component.members) {
        if (const auto* queue = std::get_if<ast::QueueDeclaration>(&member)) {
          if (queue->name == endpoint) {
            const Type actual = resolve_type(queue->element_type, queue->span, false);
            if (!actual.is_error() && !same_type(actual, expected)) {
              type_mismatch("Component pipeline endpoint", actual, expected, span);
            }
            return;
          }
        }
      }
    }
    const auto found = endpoints.find(std::string(endpoint));
    if (found != endpoints.end()) {
      if (!same_type(found->second, expected)) {
        type_mismatch("Component pipeline endpoint", found->second, expected, span);
      }
      const bool input_port = is_component_input_port(component, endpoint);
      const bool output_port = is_component_output_port(component, endpoint);
      if ((input && output_port && !input_port) || (!input && input_port && !output_port)) {
        diagnostic("MASL-E3016", "Pipeline endpoint uses a port with the wrong direction.", span);
      }
      return;
    }
    diagnostic("MASL-E3014", "Component pipeline references unknown endpoint '" +
                   std::string(endpoint) + "'.", span);
  }

  [[nodiscard]] bool is_component_input_port(const ast::ComponentDeclaration& component,
                                             std::string_view name) const {
    for (const auto& member : component.members) {
      if (const auto* port = std::get_if<ast::PortDeclaration>(&member)) {
        if (port->name == name) return port->is_input;
      }
    }
    return false;
  }

  [[nodiscard]] bool is_component_output_port(const ast::ComponentDeclaration& component,
                                              std::string_view name) const {
    for (const auto& member : component.members) {
      if (const auto* port = std::get_if<ast::PortDeclaration>(&member)) {
        if (port->name == name) return !port->is_input;
      }
    }
    return false;
  }

  void validate_one(const ast::InstanceDeclaration& declaration) {
    validate_instance(declaration);
  }

  void validate_instance(const ast::InstanceDeclaration& declaration) {
    const auto component = components_.find(declaration.component_type);
    if (component == components_.end()) {
      diagnostic("MASL-E3017", "Unknown component type '" + declaration.component_type + "'.",
                 declaration.span);
      return;
    }
    if (declaration.count == nullptr) {
      diagnostic("MASL-E3004", "Component instance count is required.", declaration.span);
    } else {
      const Type count_type = check_expression(*declaration.count);
      if (!count_type.is_integer() && !count_type.is_error()) {
        diagnostic("MASL-E3007", "Component instance count must be an integer.",
                   declaration.count->span);
      }
      static_cast<void>(positive_integer(declaration.count.get(), "Component instance count"));
    }
    std::unordered_set<std::string> assigned;
    for (const auto& parameter : declaration.parameters) {
      const auto expected = component->second.parameters.find(parameter.target);
      if (expected == component->second.parameters.end()) {
        diagnostic("MASL-E3018", "Component '" + declaration.component_type +
                       "' has no parameter '" + parameter.target + "'.", parameter.span);
        continue;
      }
      if (!assigned.insert(parameter.target).second) {
        duplicate(parameter.target, parameter.span, "parameter override");
      }
      if (parameter.value != nullptr) {
        static_cast<void>(check_expression(*parameter.value, expected->second));
      }
    }
    for (const auto& [parameter_name, parameter_type] : component->second.parameters) {
      static_cast<void>(parameter_type);
      bool has_default = false;
      for (const auto& member : component->second.declaration->members) {
        if (const auto* parameter = std::get_if<ast::ParameterDeclaration>(&member)) {
          if (parameter->name == parameter_name) has_default = parameter->default_value != nullptr;
        }
      }
      if (!has_default && !assigned.contains(parameter_name)) {
        diagnostic("MASL-E3019", "Missing required component parameter '" +
                       parameter_name + "'.", declaration.span);
      }
    }
  }

  void validate_one(const ast::WorkloadDeclaration& declaration) {
    scopes_.clear();
    scopes_.emplace_back();
    check_block(declaration.body, BlockMode::Workload, Type{}, false);
    scopes_.clear();
  }

  void validate_one(const ast::ExperimentDeclaration& declaration) {
    duplicate_property("workload", declaration.workload_count, declaration.span);
    duplicate_property("mode", declaration.mode_count, declaration.span);
    duplicate_property("stop_when", declaration.stop_when_count, declaration.span);
    duplicate_property("max_cycles", declaration.max_cycles_count, declaration.span);
    duplicate_property("max_events", declaration.max_events_count, declaration.span);
    duplicate_property("seed", declaration.seed_count, declaration.span);
    if (declaration.workload_count == 0U) {
      diagnostic("MASL-E3020", "Experiment must declare exactly one workload.", declaration.span);
    }
    if (!workloads_.contains(declaration.workload)) {
      diagnostic("MASL-E3020", "Experiment references unknown workload '" +
                     declaration.workload + "'.", declaration.span);
    }
    if (declaration.mode != "cycle" && declaration.mode != "event") {
      diagnostic("MASL-E3021", "Experiment mode must be 'cycle' or 'event'.", declaration.span);
    }
    if (declaration.stop_when != "idle" && declaration.stop_when != "max_cycles") {
      diagnostic("MASL-E3021", "stop_when must be 'idle' or 'max_cycles'.", declaration.span);
    }
    if (declaration.max_cycles != nullptr) {
      static_cast<void>(positive_integer(declaration.max_cycles.get(), "max_cycles"));
      const Type type = check_expression(*declaration.max_cycles);
      if (!type.is_integer() && !type.is_error()) {
        diagnostic("MASL-E3007", "max_cycles must be an integer.", declaration.max_cycles->span);
      }
    }
    if (declaration.max_events != nullptr) {
      static_cast<void>(positive_integer(declaration.max_events.get(), "max_events"));
      const Type type = check_expression(*declaration.max_events);
      if (!type.is_integer() && !type.is_error()) {
        diagnostic("MASL-E3007", "max_events must be an integer.", declaration.max_events->span);
      }
    }
    if (declaration.seed != nullptr) {
      static_cast<void>(non_negative_integer(declaration.seed.get(), "seed"));
      static_cast<void>(check_expression(*declaration.seed));
    }
    if (declaration.stop_when == "max_cycles" && declaration.max_cycles == nullptr) {
      diagnostic("MASL-E3022", "stop_when = max_cycles requires max_cycles.", declaration.span);
    }
    for (const auto& expectation : declaration.expectations) {
      const Type left = check_expression(*expectation.left, std::nullopt, true);
      const Type right = check_expression(*expectation.right);
      if (is_comparison(expectation.op)) {
        if (!comparable(left, right, expectation.op, expectation.left.get(),
                        expectation.right.get())) {
          diagnostic("MASL-E3023", "Experiment expectation compares incompatible types '" +
                         left.to_string() + "' and '" + right.to_string() + "'.",
                     expectation.span);
        }
      }
    }
    for (const auto& export_item : declaration.exports) {
      if (export_item.kind != "trace" && export_item.kind != "stats" &&
          export_item.kind != "csv") {
        diagnostic("MASL-E3024", "Unsupported export kind '" + export_item.kind + "'.",
                   export_item.span);
      }
      if (export_item.path.empty()) {
        diagnostic("MASL-E3024", "Export path must not be empty.", export_item.span);
      }
    }
  }

  void validate_one(const ast::FunctionDeclaration& declaration) {
    const auto found = functions_.find(declaration.name);
    if (found == functions_.end()) return;
    scopes_.clear();
    scopes_.emplace_back();
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
      const Type type = i < found->second.parameters.size()
          ? found->second.parameters[i] : Type{};
      declare_variable(declaration.parameters[i].name, type, true,
                       declaration.parameters[i].span);
    }
    check_block(declaration.body, BlockMode::Function, found->second.return_type, false);
    if (!block_returns(declaration.body)) {
      diagnostic("MASL-E3025", "Function '" + declaration.name +
                     "' does not return a value on every path.", declaration.span);
    }
    scopes_.clear();
  }

  [[nodiscard]] bool block_returns(const ast::Block& block) const {
    for (const auto& statement : block.statements) {
      if (const auto* ret = std::get_if<ast::ReturnStatement>(&statement->node)) {
        static_cast<void>(ret);
        return true;
      }
      if (const auto* branch = std::get_if<ast::IfStatement>(&statement->node)) {
        if (branch->else_block.has_value() && block_returns(branch->then_block) &&
            block_returns(*branch->else_block)) return true;
      }
    }
    return false;
  }

  void declare_variable(const std::string& name, Type type, bool read_only,
                        lexer::SourceSpan span,
                        std::optional<std::int64_t> minimum = std::nullopt,
                        std::optional<std::int64_t> maximum = std::nullopt) {
    if (scopes_.empty()) scopes_.emplace_back();
    auto& current = scopes_.back();
    if (current.contains(name)) {
      duplicate(name, span, "local variable");
      return;
    }
    current.emplace(name, Variable{std::move(type), read_only, minimum, maximum});
  }

  [[nodiscard]] Variable* lookup_variable(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(std::string(name));
      if (found != scope->end()) return &found->second;
    }
    return nullptr;
  }

  void check_block(const ast::Block& block, BlockMode mode, const Type& return_type,
                   bool nested_scope) {
    if (nested_scope) scopes_.emplace_back();
    for (const auto& statement : block.statements) {
      if (statement != nullptr) check_statement(*statement, mode, return_type);
    }
    if (nested_scope) scopes_.pop_back();
  }

  void check_statement(const ast::Statement& statement, BlockMode mode,
                       const Type& return_type) {
    std::visit([&](const auto& item) { check_one_statement(item, statement.span, mode, return_type); },
               statement.node);
  }

  void check_one_statement(const ast::LetStatement& statement, lexer::SourceSpan span,
                           BlockMode, const Type&) {
    const Type expected = resolve_type(statement.type_name, span);
    if (statement.value != nullptr) {
      static_cast<void>(check_expression(*statement.value, expected));
    }
    declare_variable(statement.name, expected, false, span);
  }

  void check_one_statement(const ast::SetStatement& statement, lexer::SourceSpan span,
                           BlockMode mode, const Type&) {
    const std::string& target = statement.assignment.target;
    const std::size_t dot = target.find('.');
    const std::string root = target.substr(0, dot);
    Variable* variable = lookup_variable(root);
    if (variable == nullptr) {
      diagnostic("MASL-E3026", "Assignment target '" + root + "' is not declared.", span);
      if (statement.assignment.value != nullptr) static_cast<void>(check_expression(*statement.assignment.value));
      return;
    }
    Type target_type = variable->type;
    if (dot == std::string::npos && variable->read_only) {
      diagnostic("MASL-E3027", "Cannot assign to read-only variable '" + root + "'.", span);
      if (statement.assignment.value != nullptr) static_cast<void>(check_expression(*statement.assignment.value));
      return;
    }
    if (dot != std::string::npos) {
      if (mode != BlockMode::Handler) {
        diagnostic("MASL-E3028", "Record-field mutation is only allowed in pipeline handlers.", span);
      }
      const std::string field = target.substr(dot + 1U);
      if (target.find('.', dot + 1U) != std::string::npos) {
        diagnostic("MASL-E3028", "Nested record-field mutation is not supported in this language version.", span);
      }
      if (target_type.kind != TypeKind::Record) {
        diagnostic("MASL-E3029", "Assignment target '" + root + "' is not a record value.", span);
        target_type = Type{};
      } else {
        const auto record = records_.find(target_type.nominal_name);
        if (record == records_.end() || !record->second.fields.contains(field)) {
          diagnostic("MASL-E3030", "Record '" + target_type.nominal_name +
                         "' has no field '" + field + "'.", span);
          target_type = Type{};
        } else {
          target_type = record->second.fields.at(field);
        }
      }
    }
    if (statement.assignment.value != nullptr) {
      static_cast<void>(check_expression(*statement.assignment.value, target_type));
    }
  }

  void check_one_statement(const ast::IfStatement& statement, lexer::SourceSpan span,
                           BlockMode mode, const Type& return_type) {
    if (statement.condition != nullptr) {
      static_cast<void>(check_expression(*statement.condition, Type{TypeKind::Bool}));
    }
    check_block(statement.then_block, mode, return_type, true);
    if (statement.else_block.has_value()) check_block(*statement.else_block, mode, return_type, true);
    static_cast<void>(span);
  }

  void check_one_statement(const ast::RepeatStatement& statement, lexer::SourceSpan,
                           BlockMode mode, const Type& return_type) {
    if (statement.count != nullptr) {
      const Type count_type = check_expression(*statement.count);
      if (!count_type.is_integer() && !count_type.is_error()) {
        diagnostic("MASL-E3031", "repeat count must be an integer.", statement.count->span);
      }
      const auto count = constant(statement.count.get());
      if (!count.has_value() || count->value < 0) {
        diagnostic("MASL-E3032", "repeat count must be a non-negative compile-time constant.",
                   statement.count->span);
      }
    }
    scopes_.emplace_back();
    Type index_type{TypeKind::UnsignedInteger, 64};
    const auto bound = constant(statement.count.get());
    const std::optional<std::int64_t> maximum =
        bound.has_value() && bound->value > 0 ? std::optional<std::int64_t>(bound->value - 1)
                                              : std::optional<std::int64_t>(0);
    declare_variable(statement.index_name, index_type, true, statement.count != nullptr
                         ? statement.count->span : lexer::SourceSpan{}, 0, maximum);
    check_block(statement.body, mode, return_type, false);
    scopes_.pop_back();
  }

  void check_one_statement(const ast::SubmitStatement& statement, lexer::SourceSpan span,
                           BlockMode mode, const Type&) {
    if (mode != BlockMode::Workload) {
      diagnostic("MASL-E3033", "submit is only allowed inside a workload.", span);
    }
    const auto queue = queues_.find(statement.queue_name);
    if (queue == queues_.end()) {
      diagnostic("MASL-E3034", "submit references unknown queue '" + statement.queue_name + "'.", span);
    }
    const Type record_type = resolve_type(statement.record_type, span);
    if (record_type.kind != TypeKind::Record && !record_type.is_error()) {
      diagnostic("MASL-E3035", "submit record type must name a record.", span);
    }
    if (queue != queues_.end() && !queue->second.resolved_element_type.is_error() &&
        !record_type.is_error() && !same_type(queue->second.resolved_element_type, record_type)) {
      type_mismatch("Submitted queue element", record_type,
                    queue->second.resolved_element_type, span);
    }
    validate_record_assignments(statement.record_type, statement.fields, span, "submit");
  }

  void check_one_statement(const ast::AssertStatement& statement, lexer::SourceSpan,
                           BlockMode, const Type&) {
    if (statement.condition == nullptr) return;
    static_cast<void>(check_expression(*statement.condition, Type{TypeKind::Bool}));
  }

  void check_one_statement(const ast::ReturnStatement& statement, lexer::SourceSpan span,
                           BlockMode mode, const Type& return_type) {
    if (mode != BlockMode::Function) {
      diagnostic("MASL-E3036", "return is only allowed inside a function.", span);
      if (statement.value != nullptr) static_cast<void>(check_expression(*statement.value));
      return;
    }
    if (statement.value == nullptr) {
      diagnostic("MASL-E3037", "A value-returning function must return an expression.", span);
      return;
    }
    static_cast<void>(check_expression(*statement.value, return_type));
  }

  void check_one_statement(const ast::ExpressionStatement& statement, lexer::SourceSpan,
                           BlockMode, const Type&) {
    if (statement.expression != nullptr) static_cast<void>(check_expression(*statement.expression));
  }

  void validate_record_assignments(std::string_view record_name,
                                   const std::vector<ast::Assignment>& assignments,
                                   lexer::SourceSpan span, std::string_view operation) {
    const auto record = records_.find(std::string(record_name));
    if (record == records_.end()) {
      for (const auto& assignment : assignments) {
        if (assignment.value != nullptr) static_cast<void>(check_expression(*assignment.value));
      }
      return;
    }
    std::unordered_set<std::string> seen;
    for (const auto& assignment : assignments) {
      const auto field = record->second.fields.find(assignment.target);
      if (!seen.insert(assignment.target).second) {
        duplicate(assignment.target, assignment.span, "record initializer field");
      }
      if (assignment.target.find('.') != std::string::npos) {
        diagnostic("MASL-E3038", "Record initializer fields must be direct field names.",
                   assignment.span);
      }
      if (field == record->second.fields.end()) {
        diagnostic("MASL-E3039", "Record '" + std::string(record_name) +
                       "' has no field '" + assignment.target + "'.", assignment.span);
        if (assignment.value != nullptr) static_cast<void>(check_expression(*assignment.value));
        continue;
      }
      if (assignment.value != nullptr) {
        static_cast<void>(check_expression(*assignment.value, field->second));
      }
    }
    for (const auto& [field_name, field_type] : record->second.fields) {
      static_cast<void>(field_type);
      if (!seen.contains(field_name)) {
        diagnostic("MASL-E3040", std::string(operation) + " is missing record field '" +
                       field_name + "'.", span);
      }
    }
  }

  [[nodiscard]] Type check_expression(const ast::Expr& expression,
                                      std::optional<Type> expected = std::nullopt,
                                      bool allow_completed = false) {
    Type type = std::visit([&](const auto& node) { return check_expression_node(node, expression.span,
                                                                                 expected,
                                                                                 allow_completed); },
                           expression.node);
    if (expected.has_value() && !type.is_error() &&
        !is_assignable(type, *expected, &expression)) {
      type_mismatch("Expression", type, *expected, expression.span);
    }
    result_.expression_types.insert_or_assign(&expression, type);
    return type;
  }

  Type check_expression_node(const ast::ErrorExpr&, lexer::SourceSpan,
                             std::optional<Type>, bool) { return Type{}; }

  Type check_expression_node(const ast::LiteralExpr& literal, lexer::SourceSpan span,
                             std::optional<Type> expected, bool) {
    if (literal.kind == ast::LiteralExpr::Kind::Boolean) return Type{TypeKind::Bool};
    if (literal.kind == ast::LiteralExpr::Kind::String) return Type{TypeKind::String};
    if (is_integer_spelling(literal.value)) {
      if (!parse_u64(literal.value).has_value()) {
        diagnostic("MASL-E3041", "Integer literal is outside the supported 64-bit compile-time range.", span);
        return Type{};
      }
      if (expected.has_value() &&
          (expected->kind == TypeKind::SignedInteger || expected->kind == TypeKind::UnsignedInteger)) {
        if (literal_fits(literal.value, *expected)) return *expected;
      }
      return Type{TypeKind::IntegerLiteral};
    }
    const auto real = parse_real(literal.value);
    if (!real.has_value()) {
      diagnostic("MASL-E3041", "Invalid or non-finite numeric literal.", span);
      return Type{};
    }
    if (expected.has_value() && expected->kind == TypeKind::FloatingPoint) {
      const long double maximum = expected->bits == 32U
          ? static_cast<long double>(std::numeric_limits<float>::max())
          : static_cast<long double>(std::numeric_limits<double>::max());
      if (std::fabs(*real) > maximum) {
        diagnostic("MASL-E3064", "Floating-point literal is outside the range of '" +
                       expected->to_string() + "'.", span);
        return Type{};
      }
      return *expected;
    }
    return Type{TypeKind::FloatingLiteral};
  }

  Type check_expression_node(const ast::NameExpr& name, lexer::SourceSpan span,
                             std::optional<Type>, bool) {
    Variable* variable = lookup_variable(name.name);
    if (variable != nullptr) return variable->type;
    diagnostic("MASL-E3042", "Unknown value name '" + name.name + "'.", span);
    return Type{};
  }

  Type check_expression_node(const ast::UnitExpr& unit, lexer::SourceSpan span,
                             std::optional<Type>, bool) {
    const auto value = parse_real(unit.number);
    if (!value.has_value() || *value < 0.0L) {
      diagnostic("MASL-E3043", "Engineering unit literal must be finite and non-negative.", span);
      return Type{};
    }
    Type type;
    if (unit.unit == "fs" || unit.unit == "ps" || unit.unit == "ns" ||
        unit.unit == "us" || unit.unit == "ms" || unit.unit == "s") {
      type.kind = TypeKind::Time;
    } else if (unit.unit == "Hz" || unit.unit == "kHz" || unit.unit == "MHz" ||
               unit.unit == "GHz") {
      type.kind = TypeKind::Frequency;
    } else if (unit.unit == "B" || unit.unit == "KiB" || unit.unit == "MiB" ||
               unit.unit == "GiB") {
      type.kind = TypeKind::Bytes;
    } else if (unit.unit == "cycles") {
      if (!clocks_.contains(unit.clock_domain)) {
        diagnostic("MASL-E3044", "Cycle literal references unknown clock '" +
                       unit.clock_domain + "'.", span);
        return Type{};
      }
      type.kind = TypeKind::Cycles;
      type.clock_domain = unit.clock_domain;
    } else if (unit.unit == "bytes_per_cycle") {
      if (!clocks_.contains(unit.clock_domain)) {
        diagnostic("MASL-E3044", "Bandwidth literal references unknown clock '" +
                       unit.clock_domain + "'.", span);
        return Type{};
      }
      type.kind = TypeKind::Bandwidth;
      type.clock_domain = unit.clock_domain;
    } else {
      diagnostic("MASL-E3045", "Unsupported engineering unit '" + unit.unit + "'.", span);
      return Type{};
    }
    return type;
  }

  Type check_expression_node(const ast::UnaryExpr& unary, lexer::SourceSpan span,
                             std::optional<Type>, bool allow_completed) {
    if (unary.operand == nullptr) return Type{};
    const Type operand = check_expression(*unary.operand, std::nullopt, allow_completed);
    if (unary.op == lexer::TokenKind::Bang) {
      if (operand.kind != TypeKind::Bool && !operand.is_error()) {
        type_mismatch("Unary ! operand", operand, Type{TypeKind::Bool}, span);
        return Type{};
      }
      return Type{TypeKind::Bool};
    }
    if (unary.op == lexer::TokenKind::Minus && operand.kind == TypeKind::UnsignedInteger) {
      diagnostic("MASL-E3046", "Unary minus cannot be applied to an unsigned integer.", span);
      return Type{};
    }
    if (!operand.is_numeric() && !operand.is_error()) {
      diagnostic("MASL-E3046", "Unary sign requires a numeric operand, found '" +
                     operand.to_string() + "'.", span);
      return Type{};
    }
    return operand;
  }

  Type check_expression_node(const ast::BinaryExpr& binary, lexer::SourceSpan span,
                             std::optional<Type>, bool allow_completed) {
    if (binary.left == nullptr || binary.right == nullptr) return Type{};
    const Type left = check_expression(*binary.left, std::nullopt, allow_completed);
    const Type right = check_expression(*binary.right, std::nullopt, allow_completed);
    using lexer::TokenKind;
    if (binary.op == TokenKind::AndAnd || binary.op == TokenKind::OrOr) {
      if (left.kind != TypeKind::Bool && !left.is_error()) {
        type_mismatch("Logical operand", left, Type{TypeKind::Bool}, binary.left->span);
      }
      if (right.kind != TypeKind::Bool && !right.is_error()) {
        type_mismatch("Logical operand", right, Type{TypeKind::Bool}, binary.right->span);
      }
      return Type{TypeKind::Bool};
    }
    if (is_comparison(binary.op)) {
      if (!comparable(left, right, binary.op, binary.left.get(), binary.right.get())) {
        diagnostic("MASL-E3047", "Comparison uses incompatible types '" + left.to_string() +
                       "' and '" + right.to_string() + "'.", span);
      }
      return Type{TypeKind::Bool};
    }
    if (binary.op == TokenKind::Percent) {
      if ((!left.is_integer() && left.kind != TypeKind::IntegerLiteral) ||
          (!right.is_integer() && right.kind != TypeKind::IntegerLiteral)) {
        diagnostic("MASL-E3048", "Remainder operator requires integer operands.", span);
        return Type{};
      }
      return common_numeric_type(left, right, span, binary.left.get(), binary.right.get());
    }
    if (binary.op == TokenKind::Plus || binary.op == TokenKind::Minus) {
      if (same_type(left, right) && is_additive_unit(left)) return left;
      if ((left.is_numeric() || left.kind == TypeKind::IntegerLiteral || left.kind == TypeKind::Count) &&
          (right.is_numeric() || right.kind == TypeKind::IntegerLiteral || right.kind == TypeKind::Count)) {
        return common_numeric_type(left, right, span, binary.left.get(), binary.right.get());
      }
      diagnostic("MASL-E3049", "Addition/subtraction requires compatible numeric or equal-dimension operands.", span);
      return Type{};
    }
    if (binary.op == TokenKind::Star || binary.op == TokenKind::Slash) {
      if ((left.is_numeric() || left.kind == TypeKind::IntegerLiteral) &&
          (right.is_numeric() || right.kind == TypeKind::IntegerLiteral)) {
        return common_numeric_type(left, right, span, binary.left.get(), binary.right.get());
      }
      diagnostic("MASL-E3050", "Multiplication/division currently requires scalar numeric operands.", span);
      return Type{};
    }
    diagnostic("MASL-E3051", "Unsupported binary operator in semantic analysis.", span);
    return Type{};
  }

  Type check_expression_node(const ast::CallExpr& call, lexer::SourceSpan span,
                             std::optional<Type>, bool allow_completed) {
    const auto* callee = call.callee != nullptr
        ? std::get_if<ast::NameExpr>(&call.callee->node) : nullptr;
    if (callee == nullptr) {
      diagnostic("MASL-E3052", "Only direct function calls are supported.", span);
      for (const auto& argument : call.arguments) {
        if (argument != nullptr) static_cast<void>(check_expression(*argument));
      }
      return Type{};
    }
    if (callee->name == "completed") {
      if (!allow_completed) {
        diagnostic("MASL-E3053", "completed(...) is only valid in experiment expectations.", span);
      }
      if (call.arguments.size() != 1U) {
        diagnostic("MASL-E3054", "completed(...) requires exactly one pipeline name.", span);
      } else if (const auto* pipeline_name = std::get_if<ast::NameExpr>(&call.arguments.front()->node)) {
        if (!pipelines_.contains(pipeline_name->name)) {
          diagnostic("MASL-E3055", "completed(...) references unknown pipeline '" +
                         pipeline_name->name + "'.", call.arguments.front()->span);
        }
      } else {
        diagnostic("MASL-E3054", "completed(...) argument must be a pipeline name.",
                   call.arguments.front()->span);
      }
      return Type{TypeKind::Count};
    }
    const auto function = functions_.find(callee->name);
    if (function == functions_.end()) {
      diagnostic("MASL-E3056", "Unknown function '" + callee->name + "'.", span);
      for (const auto& argument : call.arguments) {
        if (argument != nullptr) static_cast<void>(check_expression(*argument));
      }
      return Type{};
    }
    if (call.arguments.size() != function->second.parameters.size()) {
      diagnostic("MASL-E3057", "Function '" + callee->name + "' expects " +
                     std::to_string(function->second.parameters.size()) + " argument(s), received " +
                     std::to_string(call.arguments.size()) + ".", span);
    }
    const std::size_t count = std::min(call.arguments.size(), function->second.parameters.size());
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
      const Type expected = i < count ? function->second.parameters[i] : Type{};
      if (expected.is_error()) {
        static_cast<void>(check_expression(*call.arguments[i]));
      } else {
        static_cast<void>(check_expression(*call.arguments[i], expected));
      }
    }
    return function->second.return_type;
  }

  Type check_expression_node(const ast::FieldExpr& field, lexer::SourceSpan span,
                             std::optional<Type>, bool allow_completed) {
    if (field.object == nullptr) return Type{};
    const Type object = check_expression(*field.object, std::nullopt, allow_completed);
    if (object.kind != TypeKind::Record) {
      if (!object.is_error()) {
        diagnostic("MASL-E3058", "Field access requires a record value, found '" +
                       object.to_string() + "'.", span);
      }
      return Type{};
    }
    const auto record = records_.find(object.nominal_name);
    if (record == records_.end()) return Type{};
    const auto member = record->second.fields.find(field.field);
    if (member == record->second.fields.end()) {
      diagnostic("MASL-E3059", "Record '" + object.nominal_name +
                     "' has no field '" + field.field + "'.", span);
      return Type{};
    }
    return member->second;
  }

  [[nodiscard]] bool literal_fits(std::string_view spelling, const Type& target) const {
    const auto value = parse_u64(spelling);
    if (!value.has_value()) return false;
    if (target.kind == TypeKind::UnsignedInteger) {
      if (target.bits == 64U) return true;
      const std::uint64_t maximum = (std::uint64_t{1} << target.bits) - 1U;
      return *value <= maximum;
    }
    if (target.kind == TypeKind::SignedInteger) {
      const std::uint64_t maximum = target.bits == 64U
          ? static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
          : (std::uint64_t{1} << (target.bits - 1U)) - 1U;
      return *value <= maximum;
    }
    return false;
  }

  [[nodiscard]] bool is_assignable(const Type& from, const Type& to,
                                   const ast::Expr* expression) {
    if (from.is_error() || to.is_error()) return true;
    if (same_type(from, to)) return true;
    if ((from.kind == TypeKind::Cycles && to.kind == TypeKind::Cycles &&
         to.clock_domain.empty()) ||
        (from.kind == TypeKind::Bandwidth && to.kind == TypeKind::Bandwidth &&
         to.clock_domain.empty())) return true;
    if (from.kind == TypeKind::IntegerLiteral && to.kind == TypeKind::Count) {
      if (expression != nullptr) {
        const auto range = integer_range(*expression);
        return range.has_value() && range->minimum >= 0;
      }
      return false;
    }
    if (from.kind == TypeKind::IntegerLiteral &&
        (to.kind == TypeKind::SignedInteger || to.kind == TypeKind::UnsignedInteger)) {
      if (expression != nullptr) {
        const auto value = eval_integer_constant(*expression);
        if (value.has_value()) {
          if (to.kind == TypeKind::SignedInteger) return fits_signed(value->value, to.bits);
          return fits_unsigned(value->value, to.bits);
        }
        if (const auto* literal = std::get_if<ast::LiteralExpr>(&expression->node)) {
          return literal_fits(literal->value, to);
        }
      }
      return false;
    }
    if (from.kind == TypeKind::FloatingLiteral && to.kind == TypeKind::FloatingPoint) {
      if (expression != nullptr) {
        if (const auto* literal = std::get_if<ast::LiteralExpr>(&expression->node)) {
          const auto value = parse_real(literal->value);
          if (!value.has_value()) return false;
          const long double maximum = to.bits == 32U
              ? static_cast<long double>(std::numeric_limits<float>::max())
              : static_cast<long double>(std::numeric_limits<double>::max());
          return std::fabs(*value) <= maximum;
        }
      }
      return false;
    }
    if (expression != nullptr &&
        (to.kind == TypeKind::SignedInteger || to.kind == TypeKind::UnsignedInteger)) {
      const auto range = integer_range(*expression);
      if (range.has_value()) {
        if (to.kind == TypeKind::SignedInteger) {
          return fits_signed(range->minimum, to.bits) && fits_signed(range->maximum, to.bits);
        }
        return fits_unsigned(range->minimum, to.bits) && fits_unsigned(range->maximum, to.bits);
      }
    }
    if (from.kind == TypeKind::SignedInteger && to.kind == TypeKind::SignedInteger) {
      return from.bits <= to.bits;
    }
    if (from.kind == TypeKind::UnsignedInteger && to.kind == TypeKind::UnsignedInteger) {
      return from.bits <= to.bits;
    }
    if (from.kind == TypeKind::FloatingPoint && to.kind == TypeKind::FloatingPoint) {
      return from.bits <= to.bits;
    }
    if ((from.kind == TypeKind::Time || from.kind == TypeKind::Cycles) &&
        to.kind == TypeKind::Latency) return true;
    return false;
  }

  [[nodiscard]] bool same_type(const Type& left, const Type& right) const {
    if (left.kind != right.kind) return false;
    switch (left.kind) {
      case TypeKind::SignedInteger:
      case TypeKind::UnsignedInteger:
      case TypeKind::FloatingPoint: return left.bits == right.bits;
      case TypeKind::Record:
      case TypeKind::Enum: return left.nominal_name == right.nominal_name;
      case TypeKind::Cycles:
      case TypeKind::Bandwidth: return left.clock_domain == right.clock_domain;
      default: return true;
    }
  }

  [[nodiscard]] bool comparable(const Type& left, const Type& right,
                                lexer::TokenKind op, const ast::Expr* left_expr,
                                const ast::Expr* right_expr) {
    if (left.is_error() || right.is_error()) return true;
    if (same_type(left, right)) {
      if (op == lexer::TokenKind::Less || op == lexer::TokenKind::LessEqual ||
          op == lexer::TokenKind::Greater || op == lexer::TokenKind::GreaterEqual) {
        return left.is_numeric() || left.kind == TypeKind::Time ||
               left.kind == TypeKind::Frequency || left.kind == TypeKind::Bytes ||
               left.kind == TypeKind::Cycles || left.kind == TypeKind::Count;
      }
      return true;
    }
    if (is_assignable(left, right, left_expr) || is_assignable(right, left, right_expr)) {
      return left.is_numeric() || right.is_numeric() ||
             left.kind == TypeKind::IntegerLiteral || right.kind == TypeKind::IntegerLiteral ||
             left.kind == TypeKind::FloatingLiteral || right.kind == TypeKind::FloatingLiteral;
    }
    return false;
  }

  [[nodiscard]] bool is_additive_unit(const Type& type) const {
    return type.kind == TypeKind::Time || type.kind == TypeKind::Bytes ||
           type.kind == TypeKind::Cycles || type.kind == TypeKind::Count ||
           type.kind == TypeKind::Bandwidth;
  }

  [[nodiscard]] Type common_numeric_type(const Type& left, const Type& right,
                                         lexer::SourceSpan span,
                                         const ast::Expr* left_expr,
                                         const ast::Expr* right_expr) {
    if (left.kind == TypeKind::Count && right.kind == TypeKind::IntegerLiteral) {
      if (is_assignable(right, left, right_expr)) return left;
      diagnostic("MASL-E3062", "Integer operand is not a valid non-negative count.", span);
      return Type{};
    }
    if (right.kind == TypeKind::Count && left.kind == TypeKind::IntegerLiteral) {
      if (is_assignable(left, right, left_expr)) return right;
      diagnostic("MASL-E3062", "Integer operand is not a valid non-negative count.", span);
      return Type{};
    }
    if (left.kind == TypeKind::IntegerLiteral && right.kind == TypeKind::IntegerLiteral) {
      return Type{TypeKind::IntegerLiteral};
    }
    if (left.kind == TypeKind::FloatingLiteral && right.kind == TypeKind::FloatingLiteral) {
      return Type{TypeKind::FloatingLiteral};
    }
    if (left.kind == TypeKind::IntegerLiteral &&
        (right.kind == TypeKind::SignedInteger || right.kind == TypeKind::UnsignedInteger)) {
      if (is_assignable(left, right, left_expr)) return right;
      diagnostic("MASL-E3062", "Integer literal is outside the range of '" +
                     right.to_string() + "'.", span);
      return Type{};
    }
    if (right.kind == TypeKind::IntegerLiteral &&
        (left.kind == TypeKind::SignedInteger || left.kind == TypeKind::UnsignedInteger)) {
      if (is_assignable(right, left, right_expr)) return left;
      diagnostic("MASL-E3062", "Integer literal is outside the range of '" +
                     left.to_string() + "'.", span);
      return Type{};
    }
    if (left.kind == TypeKind::FloatingLiteral && right.kind == TypeKind::FloatingPoint) {
      if (is_assignable(left, right, left_expr)) return right;
      diagnostic("MASL-E3064", "Floating-point literal is outside the range of '" +
                     right.to_string() + "'.", span);
      return Type{};
    }
    if (right.kind == TypeKind::FloatingLiteral && left.kind == TypeKind::FloatingPoint) {
      if (is_assignable(right, left, right_expr)) return left;
      diagnostic("MASL-E3064", "Floating-point literal is outside the range of '" +
                     left.to_string() + "'.", span);
      return Type{};
    }
    if (left.kind == right.kind &&
        (left.kind == TypeKind::SignedInteger || left.kind == TypeKind::UnsignedInteger ||
         left.kind == TypeKind::FloatingPoint)) {
      Type result = left;
      result.bits = std::max(left.bits, right.bits);
      return result;
    }
    diagnostic("MASL-E3060", "No lossless common numeric type exists for '" +
                   left.to_string() + "' and '" + right.to_string() + "'.", span);
    return Type{};
  }

  void type_mismatch(std::string_view context, const Type& actual,
                     const Type& expected, lexer::SourceSpan span) {
    diagnostic("MASL-E3061", std::string(context) + " has type '" + actual.to_string() +
                   "'; expected '" + expected.to_string() + "'.", span);
  }

  std::unordered_map<std::string, RecordInfo> records_;
  std::unordered_map<std::string, const ast::EnumDeclaration*> enums_;
  std::unordered_set<std::string> clocks_;
  std::unordered_map<std::string, QueueInfo> queues_;
  std::unordered_map<std::string, PipelineInfo> pipelines_;
  std::unordered_map<std::string, ComponentInfo> components_;
  std::unordered_map<std::string, FunctionInfo> functions_;
  std::unordered_set<std::string> workloads_;
  std::unordered_set<std::string> experiments_;
  std::vector<std::unordered_map<std::string, Variable>> scopes_;
  Result result_;
};

}  // namespace

Result Analyzer::analyze(const ast::Program& program) {
  SemanticPass pass;
  return pass.run(program);
}

Result analyze(const ast::Program& program) {
  return Analyzer{}.analyze(program);
}

}  // namespace masl::semantic
