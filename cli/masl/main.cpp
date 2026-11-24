#include "masl/bytecode/bytecode.hpp"
#include "masl/vm/vm.hpp"
#include "masl/version.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
void print_help() {
  std::cout << "Usage: masl <command> [options]\n"
            << "MASL bytecode runtime " << masl::core::version_string() << ".\n\n"
            << "  --help                 Show this help.\n"
            << "  --version              Show runtime version.\n"
            << "  verify <file.masb>     Load and verify a MASB file.\n"
            << "  run <file.masb> <fn> [args...]  Execute a function with typed scalar arguments.\n";
}

bool read_bytecode(const char* path, std::vector<std::uint8_t>& bytes) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > masl::bytecode::kMaximumFileSize) {
    std::cerr << "MASL-E9103: bytecode file is unavailable or exceeds the 64 MiB limit.\n"; return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) { std::cerr << "MASL-E9101: cannot open bytecode file '" << path << "'.\n"; return false; }
  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      std::cerr << "MASL-E9102: failed reading bytecode file.\n";
      bytes.clear();
      return false;
    }
  }
  return true;
}
}

int main(int argc, char** argv) {
  if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) { print_help(); return 0; }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "MASL Runtime " << masl::core::version_string() << "\n"; return 0;
  }
  if ((argc == 3 && std::string_view(argv[1]) == "verify") ||
      (argc >= 4 && std::string_view(argv[1]) == "run")) {
    std::vector<std::uint8_t> bytes;
    if (!read_bytecode(argv[2], bytes)) return 2;
    auto loaded = masl::bytecode::load(bytes);
    if (!loaded.ok()) {
      for (const auto& item : loaded.diagnostics) std::cerr << item.code << ": " << item.message << '\n';
      return 1;
    }
    if (std::string_view(argv[1]) == "verify") {
      std::cout << "MASB verified: " << loaded.module->name << " ("
                << loaded.module->functions.size() << " function(s)).\n";
      return 0;
    }
    const auto function = std::find_if(loaded.module->functions.begin(), loaded.module->functions.end(),
                                       [&](const masl::bytecode::Function& item) { return item.name == argv[3]; });
    if (function == loaded.module->functions.end()) {
      std::cerr << "MASL-V7036: entry function '" << argv[3] << "' does not exist.\n"; return 1;
    }
    const std::size_t supplied = static_cast<std::size_t>(argc - 4);
    if (supplied != function->parameter_count) {
      std::cerr << "MASL-E9104: expected " << function->parameter_count << " argument(s), received " << supplied << ".\n"; return 2;
    }
    std::vector<masl::vm::Value> arguments;
    arguments.reserve(supplied);
    for (std::size_t i = 0; i < supplied; ++i) {
      const std::string_view argument(argv[static_cast<int>(i) + 4]);
      const auto type = function->local_types[i];
      if (type == 1U) {
        std::int64_t value = 0;
        const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size()) {
          std::cerr << "MASL-E9105: argument " << i << " must be a signed 64-bit integer.\n"; return 2;
        }
        arguments.emplace_back(value);
      } else if (type == 2U) {
        double value = 0.0;
        const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || !std::isfinite(value)) {
          std::cerr << "MASL-E9106: argument " << i << " must be a finite floating-point number.\n"; return 2;
        }
        arguments.emplace_back(value);
      } else if (type == 3U) {
        if (argument != "true" && argument != "false") {
          std::cerr << "MASL-E9107: argument " << i << " must be true or false.\n"; return 2;
        }
        arguments.emplace_back(argument == "true");
      } else if (type == 4U) {
        arguments.emplace_back(std::string(argument));
      } else {
        std::cerr << "MASL-E9108: unsupported argument type descriptor.\n"; return 2;
      }
    }
    const masl::vm::VirtualMachine machine;
    const auto result = machine.execute(*loaded.module, argv[3], arguments);
    if (!result.ok) {
      for (const auto& item : result.diagnostics) std::cerr << item.code << ": " << item.message << '\n';
      return 1;
    }
    std::cout << masl::vm::value_to_string(result.value) << "\n"
              << "instructions=" << result.instructions_executed << "\n";
    return 0;
  }
  std::cerr << "masl: invalid command; use --help.\n";
  return 2;
}
