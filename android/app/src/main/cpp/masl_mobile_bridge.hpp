#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace masl::mobile {

struct CompileOutput final {
  std::vector<std::uint8_t> bytes;
  std::string message;
};

[[nodiscard]] std::string version();
[[nodiscard]] std::string analyze_source(std::string_view source,
                                         std::string_view action);
[[nodiscard]] std::optional<CompileOutput> compile_source(
    std::string_view source, std::string& error);
[[nodiscard]] std::string simulate_hardware(
    std::string_view engine, std::uint64_t workload_size,
    std::uint64_t thread_count, std::uint64_t seed,
    std::string_view memory_pattern);
[[nodiscard]] std::string run_bytecode(
    const std::vector<std::uint8_t>& bytes, std::string_view function_name,
    const std::vector<std::string>& arguments);

}  // namespace masl::mobile
