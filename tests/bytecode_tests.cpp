#include "masl/bytecode/bytecode.hpp"
#include "masl/ir/ir.hpp"
#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"

#include <cstdint>
#include <limits>
#include <iostream>
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
})MASL";

masl::bytecode::Result build() {
  auto lexed = masl::lexer::Lexer(std::string(source)).scan();
  if (!lexed.ok()) return {};
  auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
  if (!parsed.ok()) return {};
  auto semantic = masl::semantic::analyze(*parsed.program);
  if (!semantic.ok()) return {};
  auto ir = masl::ir::lower(*parsed.program, semantic);
  if (!ir.ok()) return {};
  return masl::bytecode::compile(*ir.module);
}
}

int main() {
  auto compiled = build();
  if (!compiled.ok()) for (const auto& item : compiled.diagnostics) std::cerr << item.code << "@" << item.offset << ": " << item.message << '\n';
  check(compiled.ok(), "compiles checked Typed IR into MASB module");
  if (compiled.ok()) {
    check(compiled.module->format_major == 1U && compiled.module->functions.size() == 5U,
          "records MASB version and all function entries");
    check(masl::bytecode::verify(*compiled.module).empty(), "verifies compiler-produced code");
    std::vector<masl::bytecode::Diagnostic> diagnostics;
    auto bytes = masl::bytecode::serialize(*compiled.module, diagnostics);
    check(diagnostics.empty() && bytes.size() > masl::bytecode::kHeaderSize,
          "serializes a versioned MASB file");
    auto loaded = masl::bytecode::load(bytes);
    check(loaded.ok(), "loads and verifies serialized bytecode");
    if (loaded.ok()) {
      check(loaded.module->name == "ExecutableFunctions" && loaded.module->functions.size() == 5U,
            "preserves module and function metadata on round trip");
      const auto listing = masl::bytecode::disassemble(*loaded.module);
      check(listing.find("PUSH_I64") != std::string::npos && listing.find("CALL") != std::string::npos,
            "disassembles instruction stream and function references");
    }
    auto corrupt = bytes;
    if (corrupt.size() > 90U) corrupt.back() ^= 0x20U;
    check(!masl::bytecode::load(corrupt).ok(), "rejects checksum-corrupted bytecode");
    auto truncated = bytes;
    truncated.pop_back();
    check(!masl::bytecode::load(truncated).ok(), "rejects truncated MASB files");
    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    check(!masl::bytecode::load(bad_magic).ok(), "rejects invalid MASB magic");
    auto section_gap = bytes;
    const std::size_t code_directory_offset = masl::bytecode::kHeaderSize + masl::bytecode::kDirectoryEntrySize + 4U;
    std::uint64_t code_offset = 0U;
    for (unsigned int i = 0; i < 8U; ++i)
      code_offset |= static_cast<std::uint64_t>(section_gap[code_directory_offset + i]) << (i * 8U);
    ++code_offset;
    for (unsigned int i = 0; i < 8U; ++i)
      section_gap[code_directory_offset + i] = static_cast<std::uint8_t>((code_offset >> (i * 8U)) & 0xffU);
    const auto gap_crc = masl::bytecode::crc32(section_gap.data(), section_gap.size());
    for (unsigned int i = 0; i < 4U; ++i)
      section_gap[20U + i] = static_cast<std::uint8_t>((gap_crc >> (i * 8U)) & 0xffU);
    check(!masl::bytecode::load(section_gap).ok(), "rejects a MASB section directory containing an unreferenced gap");
    check(masl::bytecode::crc32(bytes.data(), bytes.size()) ==
              (static_cast<std::uint32_t>(bytes[20]) |
               (static_cast<std::uint32_t>(bytes[21]) << 8U) |
               (static_cast<std::uint32_t>(bytes[22]) << 16U) |
               (static_cast<std::uint32_t>(bytes[23]) << 24U)),
          "stores a reproducible CRC-32/IEEE checksum");
  }

  auto make_ins = [](std::vector<std::uint8_t>& code, std::uint8_t op, std::uint64_t operand = 0U) {
    code.push_back(op);
    for (unsigned int i = 0; i < 8U; ++i)
      code.push_back(static_cast<std::uint8_t>((operand >> (i * 8U)) & 0xffU));
  };
  {
    masl::bytecode::Module malformed; malformed.name = "Uninitialized";
    masl::bytecode::Function function; function.name = "entry"; function.return_type = 1U;
    function.local_types = {1U}; function.maximum_stack = 4U;
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::LoadLocal), 0U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Return));
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Trap));
    malformed.functions.push_back(function);
    const auto invalid = masl::bytecode::verify(malformed);
    check(!invalid.empty(), "rejects a local read before definite initialization");
  }
  {
    masl::bytecode::Module malformed; malformed.name = "TypeMismatch";
    masl::bytecode::Function function; function.name = "entry"; function.return_type = 1U;
    function.local_types = {1U}; function.maximum_stack = 4U;
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushBool), 1U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::StoreLocal), 0U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushI64), 0U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Return));
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Trap));
    malformed.functions.push_back(function);
    check(!masl::bytecode::verify(malformed).empty(), "rejects a value stored into a mismatched local type");
  }
  {
    masl::bytecode::Module malformed; malformed.name = "Recursive";
    masl::bytecode::Function function; function.name = "entry"; function.return_type = 1U;
    function.maximum_stack = 4U;
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Call), 0U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Return));
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Trap));
    malformed.functions.push_back(function);
    check(!masl::bytecode::verify(malformed).empty(), "rejects recursive call graphs in MASB 1.0");
  }
  {
    masl::bytecode::Module malformed; malformed.name = "InvalidDeadCode";
    masl::bytecode::Function function; function.name = "entry"; function.return_type = 1U;
    function.maximum_stack = 2U;
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushI64), 1U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Return));
    make_ins(function.code, 250U, 0U);
    malformed.functions.push_back(function);
    check(!masl::bytecode::verify(malformed).empty(), "decodes and rejects unknown opcodes even in unreachable code");
  }
  {
    masl::bytecode::Module malformed; malformed.name = "BranchMerge";
    masl::bytecode::Function function; function.name = "entry"; function.return_type = 1U;
    function.maximum_stack = 4U;
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushBool), 1U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::JumpIfFalse), 4U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushI64), 1U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Jump), 5U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::PushBool), 1U);
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Return));
    make_ins(function.code, static_cast<std::uint8_t>(masl::bytecode::Opcode::Trap));
    malformed.functions.push_back(function);
    check(!masl::bytecode::verify(malformed).empty(), "rejects inconsistent stack types at control-flow merges");
  }

  {
    auto lexed = masl::lexer::Lexer(R"MASL(masl 0.1; model UnitBackend { clock core = 1 GHz; fn get_latency() -> cycles { return 3 cycles(core); } })MASL").scan();
    auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
    bool rejected = false;
    if (parsed.ok()) {
      const auto semantic = masl::semantic::analyze(*parsed.program);
      if (semantic.ok()) {
        const auto typed = masl::ir::lower(*parsed.program, semantic);
        if (typed.ok()) rejected = !masl::bytecode::compile(*typed.module).ok();
      }
    }
    check(rejected, "refuses to erase physical units when lowering pure VM bytecode");
  }
  std::cout << "Bytecode assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
