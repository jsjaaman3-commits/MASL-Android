#include "masl_mobile_bridge.hpp"

#include "masl/bytecode/bytecode.hpp"
#include "masl/ir/ir.hpp"
#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"
#include "masl/sim/simulator.hpp"
#include "masl/version.hpp"
#include "masl/vm/vm.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace masl::mobile {
namespace {

template <typename T>
void append_diagnostics(std::ostringstream& output,
                        const std::vector<T>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    output << diagnostic.code << ':' << diagnostic.span.start.line << ':'
           << diagnostic.span.start.column << ": " << diagnostic.message
           << '\n';
  }
}

struct FrontEnd final {
  lexer::LexResult lexed;
  parser::ParseResult parsed;
  semantic::Result checked;
};

std::optional<FrontEnd> front_end(std::string_view source,
                                  std::string& error) {
  FrontEnd result;
  result.lexed = lexer::Lexer(source).scan();
  if (!result.lexed.ok()) {
    std::ostringstream output;
    append_diagnostics(output, result.lexed.diagnostics);
    error = output.str();
    return std::nullopt;
  }

  result.parsed = parser::Parser(std::move(result.lexed.tokens)).parse();
  if (!result.parsed.ok()) {
    std::ostringstream output;
    append_diagnostics(output, result.parsed.diagnostics);
    error = output.str();
    return std::nullopt;
  }

  result.checked = semantic::analyze(*result.parsed.program);
  if (!result.checked.ok()) {
    std::ostringstream output;
    append_diagnostics(output, result.checked.diagnostics);
    error = output.str();
    return std::nullopt;
  }
  return result;
}

bool parse_argument(std::string_view text, std::uint8_t type,
                    vm::Value& value, std::string& error,
                    std::size_t index) {
  if (type == 1U) {
    std::int64_t parsed_value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
      error = "Argument " + std::to_string(index) +
              " must be a signed 64-bit integer.";
      return false;
    }
    value = parsed_value;
    return true;
  }
  if (type == 2U) {
    double parsed_value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(parsed_value)) {
      error = "Argument " + std::to_string(index) +
              " must be a finite floating-point number.";
      return false;
    }
    value = parsed_value;
    return true;
  }
  if (type == 3U) {
    if (text != "true" && text != "false") {
      error = "Argument " + std::to_string(index) +
              " must be true or false.";
      return false;
    }
    value = (text == "true");
    return true;
  }
  if (type == 4U) {
    value = std::string(text);
    return true;
  }
  error = "Unsupported argument type descriptor.";
  return false;
}

}  // namespace

std::string version() {
  return std::string(core::version_string());
}

std::string analyze_source(std::string_view source, std::string_view action) {
  if (action == "lex") {
    const auto lexed = lexer::Lexer(source).scan();
    if (!lexed.ok()) {
      std::ostringstream output;
      append_diagnostics(output, lexed.diagnostics);
      return output.str();
    }
    std::ostringstream output;
    for (const auto& token : lexed.tokens) {
      if (token.kind == lexer::TokenKind::EndOfFile) {
        continue;
      }
      output << token.span.start.line << ':' << token.span.start.column << "  "
             << lexer::token_kind_name(token.kind) << "  `" << token.lexeme
             << "`\n";
    }
    output << "Lexed " << lexed.tokens.size() - 1U
           << " token(s) successfully.";
    return output.str();
  }

  std::string error;
  auto stages = front_end(source, error);
  if (!stages.has_value()) {
    return error;
  }

  if (action == "parse") {
    return "Syntax OK: model " + stages->parsed.program->model.name + " (" +
           std::to_string(stages->parsed.program->model.members.size()) +
           " top-level declaration(s)).";
  }
  if (action == "check") {
    return "Semantic check OK: model " +
           stages->parsed.program->model.name + " (" +
           std::to_string(stages->parsed.program->model.members.size()) +
           " top-level declaration(s)).";
  }
  if (action == "ir") {
    auto lowered = ir::lower(*stages->parsed.program, stages->checked);
    if (!lowered.ok()) {
      std::ostringstream output;
      for (const auto& diagnostic : lowered.diagnostics) {
        output << diagnostic.code << ": " << diagnostic.message << '\n';
      }
      return output.str();
    }
    return ir::dump(*lowered.module);
  }
  return "MASL-E9301: unknown Studio action.";
}

std::optional<CompileOutput> compile_source(std::string_view source,
                                            std::string& error) {
  auto stages = front_end(source, error);
  if (!stages.has_value()) {
    return std::nullopt;
  }

  auto lowered = ir::lower(*stages->parsed.program, stages->checked);
  if (!lowered.ok()) {
    std::ostringstream output;
    for (const auto& diagnostic : lowered.diagnostics) {
      output << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    error = output.str();
    return std::nullopt;
  }

  auto compiled = bytecode::compile(*lowered.module);
  if (!compiled.ok()) {
    std::ostringstream output;
    for (const auto& diagnostic : compiled.diagnostics) {
      output << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    error = output.str();
    return std::nullopt;
  }

  std::vector<bytecode::Diagnostic> diagnostics;
  auto bytes = bytecode::serialize(*compiled.module, diagnostics);
  if (!diagnostics.empty()) {
    std::ostringstream output;
    for (const auto& diagnostic : diagnostics) {
      output << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    error = output.str();
    return std::nullopt;
  }

  CompileOutput result;
  result.message = "MASB compiled: " + std::to_string(bytes.size()) +
                   " bytes, " +
                   std::to_string(compiled.module->functions.size()) +
                   " function(s).";
  result.bytes = std::move(bytes);
  return result;
}

std::string simulate_hardware(std::string_view engine,
                              std::uint64_t workload_size,
                              std::uint64_t thread_count,
                              std::uint64_t seed,
                              std::string_view memory_pattern) {
  sim::SimulatorConfig config;
  config.maximum_dynamic_instructions = 4'000'000U;
  config.workload.seed = seed;
  if (memory_pattern == "coalesced") {
    config.workload.memory_pattern = sim::MemoryPattern::Coalesced;
  } else if (memory_pattern == "strided") {
    config.workload.memory_pattern = sim::MemoryPattern::Strided;
  } else if (memory_pattern == "random") {
    config.workload.memory_pattern = sim::MemoryPattern::Random;
  } else {
    return "MASL-SIM-E001: memory pattern must be coalesced, strided or random.";
  }

  sim::Engine selected_engine = sim::Engine::Cpu;
  if (engine == "cpu") {
    if (workload_size > config.maximum_dynamic_instructions) {
      return "MASL-SIM-E004: Android CPU workload exceeds the 4000000 instruction safety budget.";
    }
    config.workload.cpu_instructions = workload_size;
  } else if (engine == "gpu") {
    if (thread_count > 4'000'000U) {
      return "MASL-SIM-E005: Android GPU thread count exceeds the 4000000 thread safety budget.";
    }
    if (workload_size > 100'000U) {
      return "MASL-SIM-E002: GPU instructions per thread exceed the 100000 limit.";
    }
    config.workload.gpu_instructions_per_thread =
        static_cast<std::uint32_t>(workload_size);
    config.workload.gpu_threads = thread_count;
    selected_engine = sim::Engine::Gpu;
  } else {
    return "MASL-SIM-E003: engine must be cpu or gpu.";
  }
  const auto result = sim::simulate(selected_engine, config);
  return result.report();
}

std::string run_bytecode(const std::vector<std::uint8_t>& bytes,
                         std::string_view function_name,
                         const std::vector<std::string>& arguments) {
  if (bytes.empty() || bytes.size() > bytecode::kMaximumFileSize) {
    return "MASL-E9302: bytecode is empty or exceeds the 64 MiB limit.";
  }

  auto loaded = bytecode::load(bytes);
  if (!loaded.ok()) {
    std::ostringstream output;
    for (const auto& diagnostic : loaded.diagnostics) {
      output << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    return output.str();
  }

  const auto function = std::find_if(
      loaded.module->functions.begin(), loaded.module->functions.end(),
      [function_name](const bytecode::Function& item) {
        return item.name == function_name;
      });
  if (function == loaded.module->functions.end()) {
    return "MASL-V7036: entry function '" + std::string(function_name) +
           "' does not exist.";
  }
  if (arguments.size() != function->parameter_count) {
    return "MASL-E9104: expected " +
           std::to_string(function->parameter_count) + " argument(s), received " +
           std::to_string(arguments.size()) + ".";
  }

  std::vector<vm::Value> values;
  values.reserve(arguments.size());
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    vm::Value value{std::int64_t{0}};
    std::string error;
    if (!parse_argument(arguments[index], function->local_types[index], value,
                        error, index)) {
      return "MASL-E9303: " + error;
    }
    values.push_back(std::move(value));
  }

  const vm::VirtualMachine machine;
  const auto result = machine.execute(*loaded.module, std::string(function_name),
                                      values);
  if (!result.ok) {
    std::ostringstream output;
    for (const auto& diagnostic : result.diagnostics) {
      output << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    return output.str();
  }
  return vm::value_to_string(result.value) + "\ninstructions=" +
         std::to_string(result.instructions_executed);
}

}  // namespace masl::mobile
