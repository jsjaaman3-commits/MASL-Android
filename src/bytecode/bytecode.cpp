#include "masl/bytecode/bytecode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace masl::bytecode {
namespace {

constexpr std::uint32_t kSectionFunctions = 1U;
constexpr std::uint32_t kSectionCode = 2U;
constexpr std::uint32_t kSectionStrings = 3U;
constexpr std::size_t kDirectoryOffset = kHeaderSize;
constexpr std::size_t kDirectoryCount = 3U;
constexpr std::size_t kPayloadOffset = kHeaderSize + kDirectoryEntrySize * kDirectoryCount;

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned int i = 0; i < 4U; ++i)
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned int i = 0; i < 8U; ++i)
    out.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
}
void patch_u32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
  for (unsigned int i = 0; i < 4U; ++i)
    out[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
}
class Reader final {
 public:
  Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  bool u8(std::uint8_t& value) { if (!available(1U)) return false; value = data_[position_++]; return true; }
  bool u16(std::uint16_t& value) {
    if (!available(2U)) return false;
    value = static_cast<std::uint16_t>(data_[position_]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[position_ + 1U]) << 8U);
    position_ += 2U; return true;
  }
  bool u32(std::uint32_t& value) {
    if (!available(4U)) return false;
    value = 0;
    for (unsigned int i = 0; i < 4U; ++i) value |= static_cast<std::uint32_t>(data_[position_ + i]) << (i * 8U);
    position_ += 4U; return true;
  }
  bool u64(std::uint64_t& value) {
    if (!available(8U)) return false;
    value = 0;
    for (unsigned int i = 0; i < 8U; ++i) value |= static_cast<std::uint64_t>(data_[position_ + i]) << (i * 8U);
    position_ += 8U; return true;
  }
  bool string(std::string& value) {
    std::uint32_t size = 0;
    if (!u32(size) || size > 1U * 1024U * 1024U || !available(size)) return false;
    value.assign(reinterpret_cast<const char*>(data_ + position_), size);
    position_ += size; return true;
  }
  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] bool at_end() const noexcept { return position_ == size_; }
 private:
  [[nodiscard]] bool available(std::size_t count) const noexcept {
    return position_ <= size_ && count <= size_ - position_;
  }
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t position_{0};
};

std::uint8_t type_code(const semantic::Type& type) {
  using semantic::TypeKind;
  switch (type.kind) {
    case TypeKind::Bool: return 3U;
    case TypeKind::String: return 4U;
    case TypeKind::FloatingPoint: case TypeKind::FloatingLiteral: return 2U;
    case TypeKind::SignedInteger: case TypeKind::UnsignedInteger:
    case TypeKind::IntegerLiteral: case TypeKind::Count: case TypeKind::Cycles:
    case TypeKind::Bytes: case TypeKind::Time: case TypeKind::Frequency:
    case TypeKind::Latency: case TypeKind::Bandwidth: case TypeKind::Address: return 1U;
    default: return 0U;
  }
}

std::string opcode_name(Opcode op) {
  switch (op) {
    case Opcode::PushI64: return "PUSH_I64"; case Opcode::PushF64: return "PUSH_F64";
    case Opcode::PushBool: return "PUSH_BOOL"; case Opcode::PushString: return "PUSH_STRING";
    case Opcode::LoadLocal: return "LOAD_LOCAL"; case Opcode::StoreLocal: return "STORE_LOCAL";
    case Opcode::Pop: return "POP"; case Opcode::Add: return "ADD";
    case Opcode::Subtract: return "SUB"; case Opcode::Multiply: return "MUL";
    case Opcode::Divide: return "DIV"; case Opcode::Modulo: return "MOD";
    case Opcode::Negate: return "NEG"; case Opcode::LogicalNot: return "NOT";
    case Opcode::Equal: return "EQ"; case Opcode::NotEqual: return "NE";
    case Opcode::Less: return "LT"; case Opcode::LessEqual: return "LE";
    case Opcode::Greater: return "GT"; case Opcode::GreaterEqual: return "GE";
    case Opcode::LogicalAnd: return "AND"; case Opcode::LogicalOr: return "OR";
    case Opcode::Jump: return "JUMP"; case Opcode::JumpIfFalse: return "JUMP_IF_FALSE";
    case Opcode::Call: return "CALL"; case Opcode::Return: return "RETURN";
    case Opcode::Assert: return "ASSERT"; case Opcode::Trap: return "TRAP";
  }
  return "UNKNOWN";
}

bool read_instruction(const std::vector<std::uint8_t>& code, std::size_t index,
                      Opcode& opcode, std::uint64_t& operand) {
  const std::size_t offset = index * kInstructionSize;
  if (offset > code.size() || code.size() - offset < kInstructionSize) return false;
  const auto raw = code[offset];
  if (raw < static_cast<std::uint8_t>(Opcode::PushI64) ||
      raw > static_cast<std::uint8_t>(Opcode::Trap)) return false;
  opcode = static_cast<Opcode>(raw);
  operand = 0;
  for (unsigned int i = 0; i < 8U; ++i)
    operand |= static_cast<std::uint64_t>(code[offset + 1U + i]) << (i * 8U);
  return true;
}

class FunctionCompiler final {
 public:
  FunctionCompiler(const ir::Function& source,
                   const std::unordered_map<std::string, std::uint32_t>& functions,
                   std::vector<std::string>& strings)
      : source_(source), functions_(functions), strings_(strings) {
    scopes_.emplace_back();
    for (const auto& parameter : source.parameters) {
      const auto slot = static_cast<std::uint16_t>(function_.local_types.size());
      scopes_.back()[parameter.name] = slot;
      function_.local_types.push_back(type_code(parameter.type));
    }
    function_.name = source.name;
    function_.parameter_count = static_cast<std::uint16_t>(source.parameters.size());
    function_.return_type = type_code(source.return_type);
    function_.instruction_budget = kDefaultInstructionBudget;
  }

  Result run() {
    if (source_.parameters.size() > kDefaultMaximumLocals) {
      fail("MASL-B6001", "Function exceeds the local-slot limit.");
      return {std::nullopt, std::move(diagnostics_)};
    }
    compile_block(source_.body, false);
    if (!diagnostics_.empty()) return {std::nullopt, std::move(diagnostics_)};
    emit(Opcode::Trap);  // Guard the end of the function against a malformed fall-through path.
    if (function_.code.empty()) {
      fail("MASL-B6002", "Function body produced no executable instructions.");
      return {std::nullopt, std::move(diagnostics_)};
    }
    return {std::move(module_holder_), {}};
  }

  Function take() { return std::move(function_); }
  const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

 private:
  void fail(std::string code, std::string message, lexer::SourceSpan span = {}) {
    diagnostics_.push_back({std::move(code), std::move(message), span.start.byte_offset});
  }
  std::size_t emit(Opcode opcode, std::uint64_t operand = 0U) {
    const auto index = function_.code.size() / kInstructionSize;
    function_.code.push_back(static_cast<std::uint8_t>(opcode));
    for (unsigned int i = 0; i < 8U; ++i)
      function_.code.push_back(static_cast<std::uint8_t>((operand >> (i * 8U)) & 0xffU));
    return index;
  }
  void patch(std::size_t instruction, std::uint64_t target) {
    const std::size_t offset = instruction * kInstructionSize + 1U;
    for (unsigned int i = 0; i < 8U; ++i)
      function_.code[offset + i] = static_cast<std::uint8_t>((target >> (i * 8U)) & 0xffU);
  }
  std::uint32_t intern(const std::string& value) {
    const auto found = std::find(strings_.begin(), strings_.end(), value);
    if (found != strings_.end()) return static_cast<std::uint32_t>(std::distance(strings_.begin(), found));
    strings_.push_back(value);
    return static_cast<std::uint32_t>(strings_.size() - 1U);
  }
  std::optional<std::uint16_t> find_local(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) return found->second;
    }
    return std::nullopt;
  }
  std::uint16_t declare_local(const std::string& name, std::uint8_t type) {
    if (function_.local_types.size() >= kDefaultMaximumLocals) {
      fail("MASL-B6003", "Function local-slot limit exceeded."); return 0;
    }
    const auto slot = static_cast<std::uint16_t>(function_.local_types.size());
    function_.local_types.push_back(type);
    scopes_.back()[name] = slot;
    return slot;
  }
  void compile_expression(const ir::Expression& expression) {
    using K = ir::ExpressionKind;
    switch (expression.kind) {
      case K::Unit:
        fail("MASL-B6004", "Physical-unit expressions are reserved for the simulation backend and cannot be executed by pure-function VM bytecode v1.0.", expression.span);
        return;
      case K::IntegerLiteral: {
        std::int64_t value = 0;
        const auto* first = expression.value.data();
        const auto* last = first + expression.value.size();
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) {
          fail("MASL-B6004", "Integer literal is outside the supported signed 64-bit range.", expression.span); return;
        }
        emit(Opcode::PushI64, std::bit_cast<std::uint64_t>(value)); return;
      }
      case K::FloatingLiteral: {
        char* end = nullptr;
        const double value = std::strtod(expression.value.c_str(), &end);
        if (end == expression.value.c_str() || *end != '\0' || !std::isfinite(value)) {
          fail("MASL-B6005", "Invalid or non-finite floating-point literal.", expression.span); return;
        }
        emit(Opcode::PushF64, std::bit_cast<std::uint64_t>(value)); return;
      }
      case K::BooleanLiteral:
        emit(Opcode::PushBool, expression.value == "true" ? 1U : 0U); return;
      case K::StringLiteral:
        emit(Opcode::PushString, intern(expression.value)); return;
      case K::Name: {
        const auto slot = find_local(expression.value);
        if (!slot.has_value()) { fail("MASL-B6006", "Unknown local variable '" + expression.value + "'.", expression.span); return; }
        emit(Opcode::LoadLocal, *slot); return;
      }
      case K::Unary:
        if (expression.operands.size() != 1U) { fail("MASL-B6007", "Malformed unary expression.", expression.span); return; }
        compile_expression(expression.operands[0]);
        if (expression.operation == lexer::TokenKind::Minus) emit(Opcode::Negate);
        else if (expression.operation == lexer::TokenKind::Bang) emit(Opcode::LogicalNot);
        else if (expression.operation != lexer::TokenKind::Plus) fail("MASL-B6008", "Unsupported unary operator.", expression.span);
        return;
      case K::Binary: {
        if (expression.operands.size() != 2U) { fail("MASL-B6009", "Malformed binary expression.", expression.span); return; }
        compile_expression(expression.operands[0]); compile_expression(expression.operands[1]);
        Opcode opcode{};
        switch (expression.operation) {
          case lexer::TokenKind::Plus: opcode = Opcode::Add; break;
          case lexer::TokenKind::Minus: opcode = Opcode::Subtract; break;
          case lexer::TokenKind::Star: opcode = Opcode::Multiply; break;
          case lexer::TokenKind::Slash: opcode = Opcode::Divide; break;
          case lexer::TokenKind::Percent: opcode = Opcode::Modulo; break;
          case lexer::TokenKind::Equal: opcode = Opcode::Equal; break;
          case lexer::TokenKind::NotEqual: opcode = Opcode::NotEqual; break;
          case lexer::TokenKind::Less: opcode = Opcode::Less; break;
          case lexer::TokenKind::LessEqual: opcode = Opcode::LessEqual; break;
          case lexer::TokenKind::Greater: opcode = Opcode::Greater; break;
          case lexer::TokenKind::GreaterEqual: opcode = Opcode::GreaterEqual; break;
          case lexer::TokenKind::AndAnd: opcode = Opcode::LogicalAnd; break;
          case lexer::TokenKind::OrOr: opcode = Opcode::LogicalOr; break;
          default: fail("MASL-B6010", "Unsupported binary operator.", expression.span); return;
        }
        emit(opcode); return;
      }
      case K::Call: {
        const auto target = functions_.find(expression.value);
        if (target == functions_.end()) { fail("MASL-B6011", "Call target '" + expression.value + "' is not a compiled function.", expression.span); return; }
        for (const auto& operand : expression.operands) compile_expression(operand);
        emit(Opcode::Call, target->second); return;
      }
      case K::Field:
        fail("MASL-B6012", "Record-field expressions are not yet part of the executable VM subset.", expression.span); return;
    }
  }
  void compile_block(const ir::Block& block, bool new_scope) {
    if (new_scope) scopes_.emplace_back();
    for (const auto& statement : block.statements) compile_statement(statement);
    if (new_scope) scopes_.pop_back();
  }
  void compile_statement(const ir::Statement& statement) {
    using S = ir::StatementKind;
    switch (statement.kind) {
      case S::Let: {
        if (!statement.expression.has_value()) { fail("MASL-B6013", "let statement has no value.", statement.span); return; }
        compile_expression(*statement.expression);
        const auto slot = declare_local(statement.name, type_code(statement.declared_semantic_type));
        emit(Opcode::StoreLocal, slot); return;
      }
      case S::Set: {
        if (!statement.expression.has_value()) { fail("MASL-B6014", "set statement has no value.", statement.span); return; }
        const auto slot = find_local(statement.name);
        if (!slot.has_value()) { fail("MASL-B6015", "Assignment to unknown local '" + statement.name + "'.", statement.span); return; }
        compile_expression(*statement.expression); emit(Opcode::StoreLocal, *slot); return;
      }
      case S::If: {
        if (!statement.expression.has_value()) { fail("MASL-B6016", "if statement has no condition.", statement.span); return; }
        compile_expression(*statement.expression);
        const auto false_jump = emit(Opcode::JumpIfFalse);
        compile_block(statement.body, true);
        if (statement.else_body.has_value()) {
          const auto end_jump = emit(Opcode::Jump);
          patch(false_jump, function_.code.size() / kInstructionSize);
          compile_block(*statement.else_body, true);
          patch(end_jump, function_.code.size() / kInstructionSize);
        } else patch(false_jump, function_.code.size() / kInstructionSize);
        return;
      }
      case S::Repeat: {
        if (!statement.expression.has_value()) { fail("MASL-B6017", "repeat statement has no bound.", statement.span); return; }
        compile_expression(*statement.expression);
        const auto bound_slot = declare_local("$repeat_bound_" + std::to_string(temp_counter_++), 1U);
        emit(Opcode::StoreLocal, bound_slot);
        emit(Opcode::PushI64, 0U);
        const auto index_slot = declare_local(statement.name, 1U);
        emit(Opcode::StoreLocal, index_slot);
        const auto loop_start = function_.code.size() / kInstructionSize;
        emit(Opcode::LoadLocal, index_slot); emit(Opcode::LoadLocal, bound_slot); emit(Opcode::Less);
        const auto exit_jump = emit(Opcode::JumpIfFalse);
        compile_block(statement.body, true);
        emit(Opcode::LoadLocal, index_slot); emit(Opcode::PushI64, 1U); emit(Opcode::Add);
        emit(Opcode::StoreLocal, index_slot); emit(Opcode::Jump, loop_start);
        patch(exit_jump, function_.code.size() / kInstructionSize);
        return;
      }
      case S::Assert:
        if (!statement.expression.has_value()) { fail("MASL-B6018", "assert statement has no condition.", statement.span); return; }
        compile_expression(*statement.expression); emit(Opcode::Assert); return;
      case S::Return:
        if (!statement.expression.has_value()) { fail("MASL-B6019", "return statement has no value.", statement.span); return; }
        compile_expression(*statement.expression); emit(Opcode::Return); return;
      case S::Expression:
        if (statement.expression.has_value()) { compile_expression(*statement.expression); emit(Opcode::Pop); }
        return;
      case S::Submit:
        fail("MASL-B6020", "submit is a simulation-runtime operation and cannot execute in a pure VM function.", statement.span); return;
    }
  }

  const ir::Function& source_;
  const std::unordered_map<std::string, std::uint32_t>& functions_;
  std::vector<std::string>& strings_;
  Function function_;
  std::map<std::string, std::uint16_t> unused_scope_;
  std::vector<std::map<std::string, std::uint16_t>> scopes_;
  std::vector<Diagnostic> diagnostics_;
  std::uint64_t temp_counter_{0};
  Module module_holder_;
};

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint32_t crc = 0xffffffffU;
  for (std::size_t i = 0; i < size; ++i) {
    std::uint8_t byte = data[i];
    if (i >= 20U && i < 24U) byte = 0U;
    crc ^= byte;
    for (unsigned int bit = 0; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
  }
  return crc ^ 0xffffffffU;
}

Result compile(const ir::Module& source) {
  std::vector<Diagnostic> diagnostics;
  auto ir_diagnostics = ir::verify(source);
  if (!ir_diagnostics.empty()) {
    for (const auto& item : ir_diagnostics)
      diagnostics.push_back({"MASL-B6000", item.code + ": " + item.message, item.span.start.byte_offset});
    return {std::nullopt, std::move(diagnostics)};
  }
  Module result;
  result.name = source.name;
  if (source.language_major > std::numeric_limits<std::uint16_t>::max() ||
      source.language_minor > std::numeric_limits<std::uint16_t>::max()) {
    diagnostics.push_back({"MASL-B6022", "Source language version exceeds MASB metadata limits.", 0U});
    return {std::nullopt, std::move(diagnostics)};
  }
  result.language_major = static_cast<std::uint16_t>(source.language_major);
  result.language_minor = static_cast<std::uint16_t>(source.language_minor);
  std::unordered_map<std::string, std::uint32_t> function_indices;
  for (const auto& declaration : source.declarations) {
    if (const auto* function = std::get_if<ir::Function>(&declaration)) {
      if (function_indices.contains(function->name)) {
        diagnostics.push_back({"MASL-B6021", "Duplicate function name '" + function->name + "'.", function->span.start.byte_offset});
        continue;
      }
      function_indices.emplace(function->name, static_cast<std::uint32_t>(function_indices.size()));
    } else {
      diagnostics.push_back({"MASL-B6023", "MASB function backend cannot yet serialize hardware/workload declaration variants; refusing to discard declaration data.", 0U});
    }
  }
  if (function_indices.empty())
    diagnostics.push_back({"MASL-B6024", "Source model contains no executable pure function.", 0U});
  if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
  for (const auto& declaration : source.declarations) {
    if (const auto* function = std::get_if<ir::Function>(&declaration)) {
      FunctionCompiler compiler(*function, function_indices, result.strings);
      auto compiled = compiler.run();
      if (!compiled.diagnostics.empty()) {
        diagnostics.insert(diagnostics.end(), compiled.diagnostics.begin(), compiled.diagnostics.end());
        continue;
      }
      result.functions.push_back(compiler.take());
    }
  }
  if (!diagnostics.empty()) return {std::nullopt, std::move(diagnostics)};
  // The verifier derives stack depth in a temporary copy and validates all control-flow joins.
  for (auto& function : result.functions) function.maximum_stack = kDefaultMaximumStack;
  auto verification = verify(result);
  if (!verification.empty()) return {std::nullopt, std::move(verification)};
  return {std::move(result), {}};
}

std::vector<Diagnostic> verify(const Module& module) {
  std::vector<Diagnostic> diagnostics;
  if (module.format_major != kFormatMajor || module.format_minor > kFormatMinor)
    diagnostics.push_back({"MASL-B6100", "Unsupported MASB module version.", 0U});
  if (module.name.empty() || module.name.size() > 4096U)
    diagnostics.push_back({"MASL-B6101", "Module name is empty or too long.", 0U});
  if (module.functions.size() > 65535U || module.strings.size() > 1'000'000U)
    diagnostics.push_back({"MASL-B6102", "Module exceeds the function or string table limit.", 0U});
  std::unordered_map<std::string, std::size_t> names;
  for (std::size_t index = 0; index < module.functions.size(); ++index) {
    const auto& function = module.functions[index];
    if (function.name.empty() || function.name.size() > 4096U || !names.emplace(function.name, index).second)
      diagnostics.push_back({"MASL-B6103", "Function name is empty, too long, or duplicated.", index});
    if (function.parameter_count > function.local_types.size() ||
        function.local_types.size() > kDefaultMaximumLocals ||
        function.maximum_stack == 0U || function.maximum_stack > kDefaultMaximumStack ||
        function.instruction_budget == 0U || function.instruction_budget > 100'000'000U)
      diagnostics.push_back({"MASL-B6104", "Function metadata exceeds a declared VM limit.", index});
    if (function.code.empty() || function.code.size() % kInstructionSize != 0U)
      diagnostics.push_back({"MASL-B6105", "Function code is empty or not instruction-aligned.", index});
    for (const auto type : function.local_types)
      if (type == 0U || type > 4U) diagnostics.push_back({"MASL-B6106", "Unknown local type code.", index});
    if (function.return_type == 0U || function.return_type > 4U)
      diagnostics.push_back({"MASL-B6107", "Unsupported function return type.", index});
  }
  for (const auto& value : module.strings)
    if (value.size() > 1U * 1024U * 1024U)
      diagnostics.push_back({"MASL-B6108", "String constant exceeds 1 MiB.", 0U});
  if (!diagnostics.empty()) return diagnostics;

  std::vector<std::vector<std::size_t>> call_graph(module.functions.size());
  for (std::size_t function_index = 0; function_index < module.functions.size(); ++function_index) {
    const auto& function = module.functions[function_index];
    const std::size_t instruction_count = function.code.size() / kInstructionSize;
    // Decode every instruction, including unreachable instructions, before control-flow analysis.
    for (std::size_t pc = 0; pc < instruction_count; ++pc) {
      Opcode opcode{}; std::uint64_t operand = 0;
      if (!read_instruction(function.code, pc, opcode, operand)) {
        diagnostics.push_back({"MASL-B6109", "Unknown or truncated opcode in instruction stream.", pc});
        continue;
      }
      const bool uses_operand = opcode == Opcode::PushI64 || opcode == Opcode::PushF64 ||
          opcode == Opcode::PushBool || opcode == Opcode::PushString ||
          opcode == Opcode::LoadLocal || opcode == Opcode::StoreLocal ||
          opcode == Opcode::Jump || opcode == Opcode::JumpIfFalse || opcode == Opcode::Call;
      if ((!uses_operand && operand != 0U) ||
          (opcode == Opcode::PushString && operand >= module.strings.size()) ||
          ((opcode == Opcode::LoadLocal || opcode == Opcode::StoreLocal) && operand >= function.local_types.size()) ||
          ((opcode == Opcode::Jump || opcode == Opcode::JumpIfFalse) && operand >= instruction_count) ||
          (opcode == Opcode::Call && operand >= module.functions.size()) ||
          (opcode == Opcode::PushBool && operand > 1U)) {
        diagnostics.push_back({"MASL-B6110", "Instruction " + opcode_name(opcode) + " has a non-zero reserved operand or an operand outside its valid range.", pc});
      }
      if (opcode == Opcode::PushF64 && !std::isfinite(std::bit_cast<double>(operand)))
        diagnostics.push_back({"MASL-B6111", "Bytecode contains a non-finite floating-point constant.", pc});
      if (opcode == Opcode::Call && operand < module.functions.size())
        call_graph[function_index].push_back(static_cast<std::size_t>(operand));
    }
  }
  if (!diagnostics.empty()) return diagnostics;

  // Recursion is intentionally excluded from MASB 1.0; execution depth is not a substitute for this gate.
  std::vector<std::uint8_t> visit_state(module.functions.size(), 0U);
  std::function<void(std::size_t)> visit = [&](std::size_t node) {
    if (visit_state[node] == 2U) return;
    if (visit_state[node] == 1U) {
      diagnostics.push_back({"MASL-B6112", "Recursive function call graph is not permitted in MASB 1.0.", node});
      return;
    }
    visit_state[node] = 1U;
    for (const auto target : call_graph[node]) visit(target);
    visit_state[node] = 2U;
  };
  for (std::size_t i = 0; i < module.functions.size(); ++i) visit(i);
  if (!diagnostics.empty()) return diagnostics;

  const auto is_numeric = [](std::uint8_t type) { return type == 1U || type == 2U; };
  for (std::size_t function_index = 0; function_index < module.functions.size(); ++function_index) {
    const auto& function = module.functions[function_index];
    const std::size_t instruction_count = function.code.size() / kInstructionSize;
    std::vector<std::vector<std::uint8_t>> states(instruction_count);
    std::vector<std::vector<bool>> initialized(instruction_count,
                                               std::vector<bool>(function.local_types.size(), false));
    std::vector<bool> reached(instruction_count, false);
    std::queue<std::size_t> pending;
    if (instruction_count == 0U) continue;
    reached[0] = true; states[0] = {};
    for (std::size_t i = 0; i < function.parameter_count; ++i) initialized[0][i] = true;
    pending.push(0U);
    std::size_t observed_max = 0U;
    bool saw_return = false;
    while (!pending.empty()) {
      const auto pc = pending.front(); pending.pop();
      Opcode opcode{}; std::uint64_t operand = 0;
      if (!read_instruction(function.code, pc, opcode, operand)) continue;
      auto stack = states[pc];
      auto local_initialized = initialized[pc];
      auto report = [&](std::string code, std::string message) {
        diagnostics.push_back({std::move(code), std::move(message), pc});
      };
      auto pop_type = [&]() -> std::optional<std::uint8_t> {
        if (stack.empty()) { report("MASL-B6113", "Operand stack underflow in verified control flow."); return std::nullopt; }
        const auto value = stack.back(); stack.pop_back(); return value;
      };
      bool valid = true;
      switch (opcode) {
        case Opcode::PushI64: stack.push_back(1U); break;
        case Opcode::PushF64: stack.push_back(2U); break;
        case Opcode::PushBool: stack.push_back(3U); break;
        case Opcode::PushString: stack.push_back(4U); break;
        case Opcode::LoadLocal:
          if (!local_initialized[static_cast<std::size_t>(operand)]) {
            report("MASL-B6133", "Local variable may be read before it is initialized."); valid = false;
          }
          stack.push_back(function.local_types[static_cast<std::size_t>(operand)]); break;
        case Opcode::StoreLocal: {
          auto value = pop_type();
          if (value.has_value() && *value != function.local_types[static_cast<std::size_t>(operand)]) {
            report("MASL-B6114", "Stored value type does not match the destination local slot."); valid = false;
          }
          if (value.has_value()) local_initialized[static_cast<std::size_t>(operand)] = true;
          break;
        }
        case Opcode::Pop: (void)pop_type(); break;
        case Opcode::Negate: {
          auto value = pop_type();
          if (value.has_value() && !is_numeric(*value)) { report("MASL-B6115", "NEG requires a numeric stack value."); valid = false; }
          if (value.has_value()) stack.push_back(*value);
          break;
        }
        case Opcode::LogicalNot: {
          auto value = pop_type();
          if (value.has_value() && *value != 3U) { report("MASL-B6116", "NOT requires a boolean stack value."); valid = false; }
          if (value.has_value()) stack.push_back(3U);
          break;
        }
        case Opcode::Add: case Opcode::Subtract: case Opcode::Multiply: case Opcode::Divide: case Opcode::Modulo: {
          auto rhs = pop_type(); auto lhs = pop_type();
          if (lhs.has_value() && rhs.has_value()) {
            if (!is_numeric(*lhs) || !is_numeric(*rhs) ||
                (opcode == Opcode::Modulo && (*lhs != 1U || *rhs != 1U))) {
              report("MASL-B6117", "Arithmetic opcode has incompatible operand types."); valid = false;
            } else stack.push_back((*lhs == 2U || *rhs == 2U) ? 2U : 1U);
          }
          break;
        }
        case Opcode::Equal: case Opcode::NotEqual: case Opcode::Less: case Opcode::LessEqual:
        case Opcode::Greater: case Opcode::GreaterEqual: {
          auto rhs = pop_type(); auto lhs = pop_type();
          if (lhs.has_value() && rhs.has_value()) {
            const bool numeric_pair = is_numeric(*lhs) && is_numeric(*rhs);
            const bool equal_pair = *lhs == *rhs && (*lhs == 3U || *lhs == 4U);
            if (!numeric_pair && !equal_pair) { report("MASL-B6118", "Comparison opcode has incompatible operand types."); valid = false; }
            if (equal_pair && opcode != Opcode::Equal && opcode != Opcode::NotEqual) {
              report("MASL-B6119", "Ordered comparison requires numeric operands."); valid = false;
            }
            stack.push_back(3U);
          }
          break;
        }
        case Opcode::LogicalAnd: case Opcode::LogicalOr: {
          auto rhs = pop_type(); auto lhs = pop_type();
          if (lhs.has_value() && rhs.has_value()) {
            if (*lhs != 3U || *rhs != 3U) { report("MASL-B6120", "Logical opcode requires boolean operands."); valid = false; }
            stack.push_back(3U);
          }
          break;
        }
        case Opcode::Jump: break;
        case Opcode::JumpIfFalse: {
          auto condition = pop_type();
          if (condition.has_value() && *condition != 3U) { report("MASL-B6121", "Conditional branch requires a boolean."); valid = false; }
          break;
        }
        case Opcode::Call: {
          const auto& callee = module.functions[static_cast<std::size_t>(operand)];
          const std::size_t argc = callee.parameter_count;
          if (stack.size() < argc) { report("MASL-B6122", "Call has fewer stack arguments than the callee parameter count."); valid = false; break; }
          const std::size_t base = stack.size() - argc;
          for (std::size_t i = 0; i < argc; ++i) {
            if (stack[base + i] != callee.local_types[i]) {
              report("MASL-B6123", "Call argument type does not match the callee signature."); valid = false;
            }
          }
          stack.resize(base);
          stack.push_back(callee.return_type);
          break;
        }
        case Opcode::Trap:
          report("MASL-B6132", "A reachable fall-through guard indicates a function path without a return.");
          valid = false;
          break;
        case Opcode::Return: {
          auto value = pop_type();
          if (value.has_value() && *value != function.return_type) {
            report("MASL-B6124", "Return value type does not match the function signature."); valid = false;
          }
          if (!stack.empty()) { report("MASL-B6125", "Return leaves extra values on the operand stack."); valid = false; }
          saw_return = true;
          break;
        }
        case Opcode::Assert: {
          auto condition = pop_type();
          if (condition.has_value() && *condition != 3U) { report("MASL-B6126", "ASSERT requires a boolean value."); valid = false; }
          break;
        }
      }
      if (!valid) continue;
      if (stack.size() > kDefaultMaximumStack) {
        report("MASL-B6127", "Operand stack exceeds configured limit."); continue;
      }
      observed_max = std::max(observed_max, stack.size());
      auto enqueue = [&](std::size_t target) {
        if (target >= instruction_count) {
          report("MASL-B6128", "Control flow falls outside the function body."); return;
        }
        if (!reached[target]) {
          reached[target] = true;
          states[target] = stack;
          initialized[target] = local_initialized;
          pending.push(target);
        } else {
          if (states[target] != stack) {
            report("MASL-B6129", "Operand stack types disagree at a control-flow merge.");
          } else {
            bool changed = false;
            for (std::size_t i = 0; i < local_initialized.size(); ++i) {
              const bool merged = initialized[target][i] && local_initialized[i];
              if (merged != initialized[target][i]) { initialized[target][i] = merged; changed = true; }
            }
            if (changed) pending.push(target);
          }
        }
      };
      if (opcode == Opcode::Return || opcode == Opcode::Trap) continue;
      if (opcode == Opcode::Jump || opcode == Opcode::JumpIfFalse) {
        enqueue(static_cast<std::size_t>(operand));
        if (opcode == Opcode::Jump) continue;
      }
      enqueue(pc + 1U);
    }
    if (!saw_return) diagnostics.push_back({"MASL-B6130", "Function has no reachable return instruction.", function_index});
    if (observed_max > function.maximum_stack)
      diagnostics.push_back({"MASL-B6131", "Declared maximum stack is smaller than verified requirement.", function_index});
  }
  return diagnostics;
}

std::vector<std::uint8_t> serialize(const Module& module, std::vector<Diagnostic>& diagnostics) {
  diagnostics = verify(module);
  if (!diagnostics.empty()) return {};

  std::vector<std::uint8_t> function_section;
  put_u32(function_section, static_cast<std::uint32_t>(module.name.size()));
  function_section.insert(function_section.end(), module.name.begin(), module.name.end());
  put_u16(function_section, module.language_major);
  put_u16(function_section, module.language_minor);
  put_u32(function_section, static_cast<std::uint32_t>(module.functions.size()));
  std::vector<std::uint8_t> code_section;
  for (const auto& function : module.functions) {
    put_u32(function_section, static_cast<std::uint32_t>(function.name.size()));
    function_section.insert(function_section.end(), function.name.begin(), function.name.end());
    put_u16(function_section, function.parameter_count);
    put_u16(function_section, static_cast<std::uint16_t>(function.local_types.size()));
    function_section.push_back(function.return_type);
    put_u16(function_section, function.maximum_stack);
    put_u32(function_section, function.instruction_budget);
    put_u32(function_section, static_cast<std::uint32_t>(code_section.size()));
    put_u32(function_section, static_cast<std::uint32_t>(function.code.size()));
    function_section.insert(function_section.end(), function.local_types.begin(), function.local_types.end());
    code_section.insert(code_section.end(), function.code.begin(), function.code.end());
  }
  std::vector<std::uint8_t> string_section;
  put_u32(string_section, static_cast<std::uint32_t>(module.strings.size()));
  for (const auto& value : module.strings) {
    put_u32(string_section, static_cast<std::uint32_t>(value.size()));
    string_section.insert(string_section.end(), value.begin(), value.end());
  }

  const std::size_t total = kPayloadOffset + function_section.size() + code_section.size() + string_section.size();
  if (total > kMaximumFileSize || total > std::numeric_limits<std::uint32_t>::max()) {
    diagnostics.push_back({"MASL-B6121", "Serialized MASB exceeds the 64 MiB file limit.", total}); return {};
  }
  std::vector<std::uint8_t> output;
  output.reserve(total);
  output.insert(output.end(), {'M','A','S','B'});
  put_u16(output, module.format_major); put_u16(output, module.format_minor);
  put_u32(output, static_cast<std::uint32_t>(kHeaderSize));
  put_u64(output, static_cast<std::uint64_t>(total));
  put_u32(output, 0U); put_u32(output, static_cast<std::uint32_t>(kDirectoryCount)); put_u32(output, 0U);
  const std::array<std::pair<std::uint32_t, std::size_t>, 3> entries{{
    {kSectionFunctions, function_section.size()},
    {kSectionCode, code_section.size()},
    {kSectionStrings, string_section.size()}
  }};
  std::size_t offset = kPayloadOffset;
  for (const auto& [type, size] : entries) {
    put_u32(output, type); put_u64(output, static_cast<std::uint64_t>(offset));
    put_u32(output, static_cast<std::uint32_t>(size)); offset += size;
  }
  output.insert(output.end(), function_section.begin(), function_section.end());
  output.insert(output.end(), code_section.begin(), code_section.end());
  output.insert(output.end(), string_section.begin(), string_section.end());
  patch_u32(output, 20U, crc32(output.data(), output.size()));
  return output;
}

Result load(const std::vector<std::uint8_t>& bytes) {
  std::vector<Diagnostic> diagnostics;
  auto fail_result = [&](std::string code, std::string message, std::size_t offset = 0U) {
    diagnostics.push_back({std::move(code), std::move(message), offset});
    return Result{std::nullopt, std::move(diagnostics)};
  };
  if (bytes.size() < kPayloadOffset || bytes.size() > kMaximumFileSize)
    return fail_result("MASL-B6200", "MASB file size is outside the allowed range.");
  if (std::memcmp(bytes.data(), "MASB", 4U) != 0)
    return fail_result("MASL-B6201", "Invalid MASB magic.");
  Reader header(bytes.data() + 4U, 28U);
  std::uint16_t major = 0, minor = 0; std::uint32_t header_size = 0, crc = 0, section_count = 0, flags = 0;
  std::uint64_t total_size = 0;
  if (!header.u16(major) || !header.u16(minor) || !header.u32(header_size) ||
      !header.u64(total_size) || !header.u32(crc) || !header.u32(section_count) || !header.u32(flags))
    return fail_result("MASL-B6202", "Truncated MASB header.");
  if (major != kFormatMajor || minor > kFormatMinor || header_size != kHeaderSize || flags != 0U)
    return fail_result("MASL-B6203", "Unsupported MASB version, header size, or feature flags.");
  if (total_size != bytes.size() || section_count != kDirectoryCount)
    return fail_result("MASL-B6204", "MASB total size or section count is inconsistent.");
  if (crc32(bytes.data(), bytes.size()) != crc)
    return fail_result("MASL-B6205", "MASB CRC-32 checksum mismatch.");

  Reader directory(bytes.data() + kDirectoryOffset, kDirectoryEntrySize * kDirectoryCount);
  struct Section { std::uint32_t type; std::uint64_t offset; std::uint32_t size; };
  std::map<std::uint32_t, Section> sections;
  for (std::size_t i = 0; i < kDirectoryCount; ++i) {
    Section section{};
    if (!directory.u32(section.type) || !directory.u64(section.offset) || !directory.u32(section.size))
      return fail_result("MASL-B6206", "Truncated section directory.");
    if (section.type < kSectionFunctions || section.type > kSectionStrings ||
        !sections.emplace(section.type, section).second)
      return fail_result("MASL-B6207", "Unknown or duplicate MASB section.");
    if (section.offset < kPayloadOffset || section.offset > bytes.size() ||
        section.size > bytes.size() - static_cast<std::size_t>(section.offset))
      return fail_result("MASL-B6208", "MASB section range is outside the file.");
  }
  std::vector<Section> ranges;
  for (const auto& [type, section] : sections) { (void)type; ranges.push_back(section); }
  std::sort(ranges.begin(), ranges.end(), [](const Section& a, const Section& b) { return a.offset < b.offset; });
  std::uint64_t previous_end = kPayloadOffset;
  for (const auto& section : ranges) {
    if (section.offset != previous_end) return fail_result("MASL-B6209", "MASB sections overlap or contain unreferenced gaps.");
    previous_end = section.offset + section.size;
  }
  if (previous_end != bytes.size()) return fail_result("MASL-B6210", "MASB contains unreferenced trailing or missing section bytes.");

  Module module; module.format_major = major; module.format_minor = minor;
  const auto function_section = sections.at(kSectionFunctions);
  Reader functions(bytes.data() + function_section.offset, function_section.size);
  std::uint32_t function_count = 0;
  if (!functions.string(module.name) || !functions.u16(module.language_major) ||
      !functions.u16(module.language_minor) || !functions.u32(function_count) || function_count > 65535U)
    return fail_result("MASL-B6211", "Invalid function table header.", static_cast<std::size_t>(function_section.offset));
  const auto code_section = sections.at(kSectionCode);
  const auto strings_section = sections.at(kSectionStrings);
  std::vector<std::pair<std::uint32_t, std::uint32_t>> code_ranges;
  for (std::uint32_t i = 0; i < function_count; ++i) {
    Function function; std::uint16_t local_count = 0; std::uint32_t code_offset = 0, code_size = 0;
    if (!functions.string(function.name) || !functions.u16(function.parameter_count) ||
        !functions.u16(local_count) || !functions.u8(function.return_type) ||
        !functions.u16(function.maximum_stack) || !functions.u32(function.instruction_budget) ||
        !functions.u32(code_offset) || !functions.u32(code_size))
      return fail_result("MASL-B6212", "Truncated function metadata.", static_cast<std::size_t>(function_section.offset) + functions.position());
    if (local_count > kDefaultMaximumLocals || code_offset > code_section.size ||
        code_size > code_section.size - code_offset || code_size == 0U ||
        code_size % kInstructionSize != 0U)
      return fail_result("MASL-B6213", "Function local or code range is invalid.", i);
    code_ranges.emplace_back(code_offset, code_offset + code_size);
    for (std::uint16_t local = 0; local < local_count; ++local) {
      std::uint8_t type = 0;
      if (!functions.u8(type)) return fail_result("MASL-B6214", "Truncated local type table.", i);
      function.local_types.push_back(type);
    }
    function.code.assign(bytes.begin() + static_cast<std::ptrdiff_t>(code_section.offset + code_offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(code_section.offset + code_offset + code_size));
    module.functions.push_back(std::move(function));
  }
  if (!functions.at_end()) return fail_result("MASL-B6215", "Unexpected bytes at end of function section.");
  std::sort(code_ranges.begin(), code_ranges.end());
  std::uint32_t code_end = 0U;
  for (const auto& range : code_ranges) {
    if (range.first != code_end) return fail_result("MASL-B6219", "Function code ranges overlap or leave unreferenced bytes.", range.first);
    code_end = range.second;
  }
  if (code_end != code_section.size) return fail_result("MASL-B6220", "Function code table does not cover the code section.");
  Reader strings(bytes.data() + strings_section.offset, strings_section.size);
  std::uint32_t string_count = 0;
  if (!strings.u32(string_count) || string_count > 1'000'000U)
    return fail_result("MASL-B6216", "Invalid string-constant count.");
  for (std::uint32_t i = 0; i < string_count; ++i) {
    std::string value;
    if (!strings.string(value)) return fail_result("MASL-B6217", "Invalid or truncated string constant.", strings.position());
    module.strings.push_back(std::move(value));
  }
  if (!strings.at_end()) return fail_result("MASL-B6218", "Unexpected bytes at end of string section.");
  auto verified = verify(module);
  if (!verified.empty()) return {std::nullopt, std::move(verified)};
  return {std::move(module), {}};
}

std::string disassemble(const Module& module) {
  std::ostringstream output;
  output << "MASB " << module.format_major << '.' << module.format_minor << " module " << module.name
         << " (source masl " << module.language_major << '.' << module.language_minor << ")\n";
  for (std::size_t fi = 0; fi < module.functions.size(); ++fi) {
    const auto& function = module.functions[fi];
    output << "function[" << fi << "] " << function.name << " params=" << function.parameter_count
           << " locals=" << function.local_types.size() << " max_stack=" << function.maximum_stack << '\n';
    const auto count = function.code.size() / kInstructionSize;
    for (std::size_t pc = 0; pc < count; ++pc) {
      Opcode opcode{}; std::uint64_t operand = 0;
      if (!read_instruction(function.code, pc, opcode, operand)) { output << "  " << pc << " <invalid>\n"; break; }
      output << "  " << std::setw(4) << pc << "  " << std::left << std::setw(16) << opcode_name(opcode)
             << std::right << operand;
      if (opcode == Opcode::PushString && operand < module.strings.size()) output << "  \"" << module.strings[operand] << '"';
      if (opcode == Opcode::Call && operand < module.functions.size()) output << "  ; " << module.functions[operand].name;
      output << '\n';
    }
  }
  return output.str();
}

}  // namespace masl::bytecode
