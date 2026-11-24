#include "masl/bytecode/bytecode.hpp"
#include "masl/ir/ir.hpp"
#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"
#include "masl/vm/vm.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace {
int failures = 0;
int assertions = 0;
void check(bool condition, std::string_view message) {
  ++assertions;
  if (condition) std::cout << "[PASS] " << message << '\n';
  else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
}
constexpr std::string_view source = R"MASL(masl 0.1;
model ExecutableFunctions {
  fn answer() -> i32 { return 6 * 7; }
  fn twice(value: i32) -> i32 { let result: i32 = value * 2; return result; }
  fn choose(value: i32) -> i32 {
    if value > 0 { return twice(value); } else { return 0; }
  }
  fn sum_to() -> u64 {
    let total: u64 = 0;
    repeat 10 as i { set total = total + i; }
    return total;
  }
  fn enabled(value: i32) -> bool { return value >= 0; }
  fn float_divide(value: f64) -> f64 { let result: f64 = 10.0 / value; return result; }
  fn label() -> string { return "MASL"; }
  fn require_positive(value: i32) -> i32 { assert value > 0; return value; }
  fn increment(value: i64) -> i64 { return value + 1; }
  fn divide(value: i64) -> i64 { return 5 / value; }
})MASL";
masl::bytecode::Result build() {
  auto lexed = masl::lexer::Lexer(std::string(source)).scan(); if (!lexed.ok()) return {};
  auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse(); if (!parsed.ok()) return {};
  auto semantic = masl::semantic::analyze(*parsed.program); if (!semantic.ok()) return {};
  auto ir = masl::ir::lower(*parsed.program, semantic); if (!ir.ok()) return {};
  return masl::bytecode::compile(*ir.module);
}
}

int main() {
  auto compiled = build();
  if (!compiled.ok()) for (const auto& item : compiled.diagnostics) std::cerr << item.code << "@" << item.offset << ": " << item.message << '\n';
  check(compiled.ok(), "builds executable function module");
  if (compiled.ok()) {
    masl::vm::VirtualMachine vm;
    auto answer = vm.execute(*compiled.module, "answer");
    check(answer.ok && std::get<std::int64_t>(answer.value) == 42,
          "executes arithmetic and returns 42");
    auto twice = vm.execute(*compiled.module, "twice", {std::int64_t{21}});
    check(twice.ok && std::get<std::int64_t>(twice.value) == 42,
          "loads parameters, stores locals and returns from a function");
    auto branch = vm.execute(*compiled.module, "choose", {std::int64_t{7}});
    check(branch.ok && std::get<std::int64_t>(branch.value) == 14,
          "executes conditional control flow and a direct function call");
    auto otherwise = vm.execute(*compiled.module, "choose", {std::int64_t{-7}});
    check(otherwise.ok && std::get<std::int64_t>(otherwise.value) == 0,
          "executes the alternate branch");
    auto loop = vm.execute(*compiled.module, "sum_to");
    check(loop.ok && std::get<std::int64_t>(loop.value) == 45,
          "executes a bounded repeat loop with mutation");
    auto boolean = vm.execute(*compiled.module, "enabled", {std::int64_t{0}});
    check(boolean.ok && std::get<bool>(boolean.value),
          "executes boolean comparisons and typed boolean return");
    auto bad_arity = vm.execute(*compiled.module, "twice");
    check(!bad_arity.ok && !bad_arity.diagnostics.empty(), "rejects a wrong function argument count");
    auto missing = vm.execute(*compiled.module, "missing");
    check(!missing.ok, "rejects unknown entry function names");
    masl::vm::Limits tiny; tiny.total_instruction_budget = 2U;
    auto budget = masl::vm::VirtualMachine(tiny).execute(*compiled.module, "sum_to");
    check(!budget.ok && !budget.diagnostics.empty(), "traps when the global instruction budget is exhausted");
    auto floating = vm.execute(*compiled.module, "float_divide", {2.0});
    check(floating.ok && std::get<double>(floating.value) == 5.0,
          "executes IEEE-754 floating-point arithmetic");
    auto float_zero = vm.execute(*compiled.module, "float_divide", {0.0});
    check(!float_zero.ok, "traps on floating-point division by zero");
    auto string_result = vm.execute(*compiled.module, "label");
    check(string_result.ok && std::get<std::string>(string_result.value) == "MASL",
          "loads and returns bounded string constants");
    auto assertion_ok = vm.execute(*compiled.module, "require_positive", {std::int64_t{3}});
    auto assertion_bad = vm.execute(*compiled.module, "require_positive", {std::int64_t{0}});
    check(assertion_ok.ok && !assertion_bad.ok, "executes assertions and traps on a false condition");
    auto overflow = vm.execute(*compiled.module, "increment", {std::numeric_limits<std::int64_t>::max()});
    check(!overflow.ok, "traps on signed integer overflow");
    auto division_zero = vm.execute(*compiled.module, "divide", {std::int64_t{0}});
    check(!division_zero.ok, "traps on integer division by zero");
    auto wrong_type = vm.execute(*compiled.module, "float_divide", {std::int64_t{2}});
    check(!wrong_type.ok, "rejects entry arguments with incorrect runtime types");
  }
  std::cout << "VM assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
