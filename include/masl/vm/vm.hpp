#pragma once

#include "masl/bytecode/bytecode.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace masl::vm {

using Value = std::variant<std::int64_t, double, bool, std::string>;

struct Limits final {
  std::uint64_t total_instruction_budget{10'000'000U};
  std::uint32_t maximum_call_depth{128U};
  std::size_t maximum_stack_values{4096U};
  std::size_t maximum_string_bytes{1U * 1024U * 1024U};
};

struct Diagnostic final {
  std::string code;
  std::string message;
};

struct Result final {
  bool ok{false};
  Value value{std::int64_t{0}};
  std::vector<Diagnostic> diagnostics;
  std::uint64_t instructions_executed{0};
};

class VirtualMachine final {
 public:
  explicit VirtualMachine(Limits limits = {}) : limits_(limits) {}
  [[nodiscard]] Result execute(const bytecode::Module& module,
                               const std::string& function_name,
                               const std::vector<Value>& arguments = {}) const;
 private:
  Limits limits_;
};

[[nodiscard]] std::string value_to_string(const Value& value);

}  // namespace masl::vm
