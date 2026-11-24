#pragma once

#include "masl/ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace masl::bytecode {

inline constexpr std::uint16_t kFormatMajor = 1;
inline constexpr std::uint16_t kFormatMinor = 0;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kDirectoryEntrySize = 16;
inline constexpr std::size_t kInstructionSize = 9;
inline constexpr std::size_t kMaximumFileSize = 64U * 1024U * 1024U;
inline constexpr std::uint32_t kDefaultInstructionBudget = 1'000'000U;
inline constexpr std::uint16_t kDefaultMaximumStack = 4096U;
inline constexpr std::uint16_t kDefaultMaximumLocals = 4096U;

enum class Opcode : std::uint8_t {
  PushI64 = 1, PushF64, PushBool, PushString,
  LoadLocal, StoreLocal, Pop,
  Add, Subtract, Multiply, Divide, Modulo,
  Negate, LogicalNot,
  Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
  LogicalAnd, LogicalOr,
  Jump, JumpIfFalse, Call, Return, Assert, Trap
};

struct Function final {
  std::string name;
  std::uint16_t parameter_count{0};
  std::uint8_t return_type{0};
  std::vector<std::uint8_t> local_types;
  std::uint16_t maximum_stack{0};
  std::uint32_t instruction_budget{kDefaultInstructionBudget};
  std::vector<std::uint8_t> code;
};

struct Module final {
  std::string name;
  std::uint16_t format_major{kFormatMajor};
  std::uint16_t format_minor{kFormatMinor};
  std::uint16_t language_major{0};
  std::uint16_t language_minor{0};
  std::vector<std::string> strings;
  std::vector<Function> functions;
};

struct Diagnostic final {
  std::string code;
  std::string message;
  std::size_t offset{0};
};

struct Result final {
  std::optional<Module> module;
  std::vector<Diagnostic> diagnostics;
  [[nodiscard]] bool ok() const noexcept {
    return module.has_value() && diagnostics.empty();
  }
};

[[nodiscard]] Result compile(const ir::Module& source);
[[nodiscard]] std::vector<Diagnostic> verify(const Module& module);
[[nodiscard]] std::vector<std::uint8_t> serialize(const Module& module,
                                                  std::vector<Diagnostic>& diagnostics);
[[nodiscard]] Result load(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::string disassemble(const Module& module);
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept;

}  // namespace masl::bytecode
