#include "masl/vm/vm.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <sstream>
#include <type_traits>

namespace masl::vm {
namespace {

using bytecode::Opcode;
constexpr std::size_t kInstructionSize = bytecode::kInstructionSize;

bool decode(const std::vector<std::uint8_t>& code, std::size_t pc,
            Opcode& opcode, std::uint64_t& operand) {
  const std::size_t offset = pc * kInstructionSize;
  if (offset > code.size() || code.size() - offset < kInstructionSize) return false;
  const auto raw = code[offset];
  if (raw < static_cast<std::uint8_t>(Opcode::PushI64) ||
      raw > static_cast<std::uint8_t>(Opcode::Trap)) return false;
  opcode = static_cast<Opcode>(raw); operand = 0;
  for (unsigned int i = 0; i < 8U; ++i)
    operand |= static_cast<std::uint64_t>(code[offset + 1U + i]) << (i * 8U);
  return true;
}

bool matches_type(std::uint8_t type, const Value& value) {
  switch (type) {
    case 1U: return std::holds_alternative<std::int64_t>(value);
    case 2U: return std::holds_alternative<double>(value);
    case 3U: return std::holds_alternative<bool>(value);
    case 4U: return std::holds_alternative<std::string>(value);
    default: return false;
  }
}

bool checked_add_i64(std::int64_t a, std::int64_t b, std::int64_t& out) {
  if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
      (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) return false;
  out = a + b; return true;
}
bool checked_sub_i64(std::int64_t a, std::int64_t b, std::int64_t& out) {
  if ((b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
      (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)) return false;
  out = a - b; return true;
}
bool checked_mul_i64(std::int64_t a, std::int64_t b, std::int64_t& out) {
  if (a == 0 || b == 0) { out = 0; return true; }
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  const auto minimum = std::numeric_limits<std::int64_t>::min();
  if ((a == -1 && b == minimum) || (b == -1 && a == minimum)) return false;
  if (a > 0) {
    if ((b > 0 && a > maximum / b) || (b < 0 && b < minimum / a)) return false;
  } else {
    if ((b > 0 && a < minimum / b) || (b < 0 && a < maximum / b)) return false;
  }
  out = a * b; return true;
}

bool truthy(const Value& value, bool& output) {
  if (const auto* boolean = std::get_if<bool>(&value)) { output = *boolean; return true; }
  return false;
}

bool numeric(const Value& value, long double& output, bool& floating) {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) { output = *integer; floating = false; return true; }
  if (const auto* real = std::get_if<double>(&value)) { output = static_cast<long double>(*real); floating = true; return true; }
  return false;
}

bool compare_values(const Value& lhs, const Value& rhs, Opcode op, bool& result) {
  long double left = 0, right = 0; bool left_float = false, right_float = false;
  if (numeric(lhs, left, left_float) && numeric(rhs, right, right_float)) {
    switch (op) {
      case Opcode::Equal: result = left == right; return true;
      case Opcode::NotEqual: result = left != right; return true;
      case Opcode::Less: result = left < right; return true;
      case Opcode::LessEqual: result = left <= right; return true;
      case Opcode::Greater: result = left > right; return true;
      case Opcode::GreaterEqual: result = left >= right; return true;
      default: return false;
    }
  }
  if (lhs.index() != rhs.index()) return false;
  if (const auto* a = std::get_if<bool>(&lhs)) {
    const bool b = std::get<bool>(rhs);
    if (op == Opcode::Equal) result = *a == b;
    else if (op == Opcode::NotEqual) result = *a != b;
    else return false;
    return true;
  }
  if (const auto* a = std::get_if<std::string>(&lhs)) {
    const auto& b = std::get<std::string>(rhs);
    if (op == Opcode::Equal) result = *a == b;
    else if (op == Opcode::NotEqual) result = *a != b;
    else return false;
    return true;
  }
  return false;
}

Value default_value(std::uint8_t type) {
  switch (type) {
    case 2U: return 0.0;
    case 3U: return false;
    case 4U: return std::string{};
    default: return std::int64_t{0};
  }
}

class Executor final {
 public:
  Executor(const bytecode::Module& module, const Limits& limits)
      : module_(module), limits_(limits) {}

  Result run(std::size_t function_index, const std::vector<Value>& arguments) {
    auto value = invoke(function_index, arguments, 0U);
    if (!diagnostics_.empty() || !value.has_value())
      return {false, std::int64_t{0}, std::move(diagnostics_), executed_};
    return {true, std::move(*value), {}, executed_};
  }

 private:
  void trap(std::string code, std::string message) {
    if (diagnostics_.empty()) diagnostics_.push_back({std::move(code), std::move(message)});
  }

  std::optional<Value> invoke(std::size_t function_index,
                              const std::vector<Value>& arguments,
                              std::uint32_t depth) {
    if (function_index >= module_.functions.size()) {
      trap("MASL-V7001", "Function index is outside the module."); return std::nullopt;
    }
    if (depth >= limits_.maximum_call_depth) {
      trap("MASL-V7002", "Maximum VM call depth exceeded."); return std::nullopt;
    }
    const auto& function = module_.functions[function_index];
    if (arguments.size() != function.parameter_count) {
      trap("MASL-V7003", "Function argument count mismatch for '" + function.name + "'."); return std::nullopt;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      if (!matches_type(function.local_types[i], arguments[i])) {
        trap("MASL-V7038", "Argument type mismatch at parameter " + std::to_string(i) + " of '" + function.name + "'.");
        return std::nullopt;
      }
    }
    std::vector<Value> locals;
    locals.reserve(function.local_types.size());
    for (const auto type : function.local_types) locals.push_back(default_value(type));
    for (std::size_t i = 0; i < arguments.size(); ++i) locals[i] = arguments[i];
    std::vector<Value> stack;
    stack.reserve(std::min<std::size_t>(function.maximum_stack, limits_.maximum_stack_values));
    const std::size_t instruction_count = function.code.size() / kInstructionSize;
    std::size_t pc = 0;
    std::uint64_t local_executed = 0;

    auto pop = [&]() -> std::optional<Value> {
      if (stack.empty()) { trap("MASL-V7004", "Operand stack underflow."); return std::nullopt; }
      Value value = std::move(stack.back()); stack.pop_back(); return value;
    };
    auto push = [&](Value value) -> bool {
      if (stack.size() >= limits_.maximum_stack_values || stack.size() >= function.maximum_stack) {
        trap("MASL-V7005", "Operand stack limit exceeded."); return false;
      }
      if (const auto* text = std::get_if<std::string>(&value); text != nullptr && text->size() > limits_.maximum_string_bytes) {
        trap("MASL-V7006", "String value exceeds configured VM limit."); return false;
      }
      stack.push_back(std::move(value)); return true;
    };

    while (pc < instruction_count && diagnostics_.empty()) {
      if (++local_executed > function.instruction_budget || ++executed_ > limits_.total_instruction_budget) {
        trap("MASL-V7007", "VM instruction budget exhausted."); break;
      }
      Opcode opcode{}; std::uint64_t operand = 0;
      if (!decode(function.code, pc, opcode, operand)) { trap("MASL-V7008", "Invalid opcode encountered after verification."); break; }
      ++pc;
      switch (opcode) {
        case Opcode::PushI64:
          if (!push(std::bit_cast<std::int64_t>(operand))) return std::nullopt;
          break;
        case Opcode::PushF64: {
          const double value = std::bit_cast<double>(operand);
          if (!std::isfinite(value)) { trap("MASL-V7009", "Non-finite floating-point constant."); break; }
          if (!push(value)) return std::nullopt;
          break;
        }
        case Opcode::PushBool:
          if (operand > 1U) { trap("MASL-V7010", "Boolean constant must be zero or one."); break; }
          if (!push(operand != 0U)) return std::nullopt;
          break;
        case Opcode::PushString:
          if (operand >= module_.strings.size()) { trap("MASL-V7011", "String constant index out of range."); break; }
          if (!push(module_.strings[static_cast<std::size_t>(operand)])) return std::nullopt;
          break;
        case Opcode::LoadLocal:
          if (operand >= locals.size()) { trap("MASL-V7012", "Local index out of range."); break; }
          if (!push(locals[static_cast<std::size_t>(operand)])) return std::nullopt;
          break;
        case Opcode::StoreLocal: {
          if (operand >= locals.size()) { trap("MASL-V7013", "Local index out of range."); break; }
          auto value = pop(); if (!value.has_value()) break;
          if (!matches_type(function.local_types[static_cast<std::size_t>(operand)], *value)) {
            trap("MASL-V7039", "Runtime value type does not match destination local slot."); break;
          }
          locals[static_cast<std::size_t>(operand)] = std::move(*value); break;
        }
        case Opcode::Pop: (void)pop(); break;
        case Opcode::Negate: {
          auto value = pop(); if (!value.has_value()) break;
          if (auto* integer = std::get_if<std::int64_t>(&*value)) {
            if (*integer == std::numeric_limits<std::int64_t>::min()) { trap("MASL-V7014", "Signed integer overflow in unary negation."); break; }
            *integer = -*integer; (void)push(*integer);
          } else if (auto* real = std::get_if<double>(&*value)) (void)push(-*real);
          else trap("MASL-V7015", "Unary negation requires a numeric value.");
          break;
        }
        case Opcode::LogicalNot: {
          auto value = pop(); if (!value.has_value()) break;
          bool result = false;
          if (!truthy(*value, result)) { trap("MASL-V7016", "Logical NOT requires a boolean value."); break; }
          (void)push(!result); break;
        }
        case Opcode::Add: case Opcode::Subtract: case Opcode::Multiply: case Opcode::Divide: case Opcode::Modulo: {
          auto rhs = pop(); auto lhs = pop();
          if (!rhs.has_value() || !lhs.has_value()) break;
          long double a = 0, b = 0; bool af = false, bf = false;
          if (!numeric(*lhs, a, af) || !numeric(*rhs, b, bf)) { trap("MASL-V7017", "Arithmetic operation requires numeric operands."); break; }
          if (opcode == Opcode::Modulo) {
            const auto* ai = std::get_if<std::int64_t>(&*lhs); const auto* bi = std::get_if<std::int64_t>(&*rhs);
            if (ai == nullptr || bi == nullptr) { trap("MASL-V7018", "Modulo requires integer operands."); break; }
            if (*bi == 0) { trap("MASL-V7019", "Modulo by zero."); break; }
            if (*ai == std::numeric_limits<std::int64_t>::min() && *bi == -1) { trap("MASL-V7020", "Signed integer overflow in modulo."); break; }
            (void)push(*ai % *bi); break;
          }
          if (!af && !bf) {
            const auto x = std::get<std::int64_t>(*lhs); const auto y = std::get<std::int64_t>(*rhs);
            std::int64_t out = 0; bool overflow = false;
            if (opcode == Opcode::Divide) {
              if (y == 0) { trap("MASL-V7021", "Division by zero."); break; }
              if (x == std::numeric_limits<std::int64_t>::min() && y == -1) { trap("MASL-V7022", "Signed integer overflow in division."); break; }
              out = x / y;
            } else if (opcode == Opcode::Add) overflow = !checked_add_i64(x, y, out);
            else if (opcode == Opcode::Subtract) overflow = !checked_sub_i64(x, y, out);
            else overflow = !checked_mul_i64(x, y, out);
            if (overflow) { trap("MASL-V7023", "Signed integer overflow."); break; }
            (void)push(out);
          } else {
            if (opcode == Opcode::Divide && b == 0) { trap("MASL-V7024", "Floating-point division by zero."); break; }
            long double out = opcode == Opcode::Add ? a + b : opcode == Opcode::Subtract ? a - b : opcode == Opcode::Multiply ? a * b : a / b;
            const double value = static_cast<double>(out);
            if (!std::isfinite(value)) { trap("MASL-V7025", "Non-finite floating-point arithmetic result."); break; }
            (void)push(value);
          }
          break;
        }
        case Opcode::Equal: case Opcode::NotEqual: case Opcode::Less: case Opcode::LessEqual: case Opcode::Greater: case Opcode::GreaterEqual: {
          auto rhs = pop(); auto lhs = pop(); if (!rhs.has_value() || !lhs.has_value()) break;
          bool result = false;
          if (!compare_values(*lhs, *rhs, opcode, result)) { trap("MASL-V7026", "Values cannot be compared with this operator."); break; }
          (void)push(result); break;
        }
        case Opcode::LogicalAnd: case Opcode::LogicalOr: {
          auto rhs = pop(); auto lhs = pop(); if (!rhs.has_value() || !lhs.has_value()) break;
          bool a = false, b = false;
          if (!truthy(*lhs, a) || !truthy(*rhs, b)) { trap("MASL-V7027", "Logical operation requires boolean operands."); break; }
          (void)push(opcode == Opcode::LogicalAnd ? (a && b) : (a || b)); break;
        }
        case Opcode::Jump:
          if (operand >= instruction_count) { trap("MASL-V7028", "Jump target out of range."); break; }
          pc = static_cast<std::size_t>(operand); break;
        case Opcode::JumpIfFalse: {
          auto value = pop(); if (!value.has_value()) break;
          bool condition = false;
          if (!truthy(*value, condition)) { trap("MASL-V7029", "Conditional branch requires a boolean."); break; }
          if (!condition) {
            if (operand >= instruction_count) { trap("MASL-V7030", "Conditional target out of range."); break; }
            pc = static_cast<std::size_t>(operand);
          }
          break;
        }
        case Opcode::Call: {
          if (operand >= module_.functions.size()) { trap("MASL-V7031", "Call target out of range."); break; }
          const auto& callee = module_.functions[static_cast<std::size_t>(operand)];
          if (stack.size() < callee.parameter_count) { trap("MASL-V7032", "Not enough arguments on the VM stack."); break; }
          std::vector<Value> args(callee.parameter_count);
          for (std::size_t i = args.size(); i > 0U; --i) { args[i - 1U] = std::move(stack.back()); stack.pop_back(); }
          auto result = invoke(static_cast<std::size_t>(operand), args, depth + 1U);
          if (result.has_value()) (void)push(std::move(*result));
          break;
        }
        case Opcode::Return: {
          auto value = pop();
          if (!value.has_value()) return std::nullopt;
          if (!matches_type(function.return_type, *value)) {
            trap("MASL-V7040", "Runtime return value does not match the function return type.");
            return std::nullopt;
          }
          return value;
        }
        case Opcode::Trap:
          trap("MASL-V7037", "Reached a compiler-emitted invalid fall-through guard.");
          break;
        case Opcode::Assert: {
          auto value = pop(); if (!value.has_value()) break;
          bool condition = false;
          if (!truthy(*value, condition)) { trap("MASL-V7033", "assert requires a boolean condition."); break; }
          if (!condition) trap("MASL-V7034", "MASL assertion failed.");
          break;
        }
      }
    }
    if (diagnostics_.empty()) trap("MASL-V7035", "Function terminated without a return value.");
    return std::nullopt;
  }

  const bytecode::Module& module_;
  const Limits& limits_;
  std::vector<Diagnostic> diagnostics_;
  std::uint64_t executed_{0};
};

}  // namespace

Result VirtualMachine::execute(const bytecode::Module& module,
                               const std::string& function_name,
                               const std::vector<Value>& arguments) const {
  auto diagnostics = bytecode::verify(module);
  if (!diagnostics.empty()) {
    std::vector<Diagnostic> errors;
    for (const auto& item : diagnostics) errors.push_back({"MASL-V7000", item.code + ": " + item.message});
    return {false, std::int64_t{0}, std::move(errors), 0U};
  }
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    if (module.functions[index].name == function_name) return Executor(module, limits_).run(index, arguments);
  }
  return {false, std::int64_t{0}, {{"MASL-V7036", "Entry function '" + function_name + "' does not exist."}}, 0U};
}

std::string value_to_string(const Value& value) {
  return std::visit([](const auto& item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::string>) return item;
    else if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
    else { std::ostringstream output; output << item; return output.str(); }
  }, value);
}

}  // namespace masl::vm
