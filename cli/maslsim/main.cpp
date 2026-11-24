#include "masl/sim/simulator.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

bool parse_u64(std::string_view text, std::uint64_t& value) {
  if (text.empty()) return false;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

void usage() {
  std::cout << "MASL Architecture Simulator 0.10.0\n"
               "Usage:\n"
               "  maslsim --version\n"
               "  maslsim cpu [dynamic-instructions] [seed]\n"
               "  maslsim gpu [instructions-per-thread] [threads] [seed] [coalesced|strided|random]\n"
               "Examples:\n"
               "  maslsim cpu 50000 20261123\n"
               "  maslsim gpu 64 32768 20261123 strided\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "MASL Architecture Simulator 0.10.0\n";
    return 0;
  }
  if (argc < 2 || std::string_view(argv[1]) == "--help" ||
      std::string_view(argv[1]) == "-h") {
    usage();
    return argc < 2 ? 2 : 0;
  }

  masl::sim::SimulatorConfig config;
  const std::string_view engine_name(argv[1]);
  masl::sim::Engine engine = masl::sim::Engine::Cpu;
  if (engine_name == "cpu") {
    if (argc > 4) {
      std::cerr << "too many CPU arguments\n";
      return 2;
    }
    engine = masl::sim::Engine::Cpu;
    if (argc >= 3 && !parse_u64(argv[2], config.workload.cpu_instructions)) {
      std::cerr << "invalid CPU instruction count\n";
      return 2;
    }
    if (argc >= 4 && !parse_u64(argv[3], config.workload.seed)) {
      std::cerr << "invalid seed\n";
      return 2;
    }
  } else if (engine_name == "gpu") {
    if (argc > 6) {
      std::cerr << "too many GPU arguments\n";
      return 2;
    }
    engine = masl::sim::Engine::Gpu;
    std::uint64_t instructions = config.workload.gpu_instructions_per_thread;
    if (argc >= 3 && !parse_u64(argv[2], instructions)) {
      std::cerr << "invalid GPU instructions-per-thread value\n";
      return 2;
    }
    if (instructions == 0U || instructions > 100'000U) {
      std::cerr << "GPU instructions per thread must be in 1..100000\n";
      return 2;
    }
    config.workload.gpu_instructions_per_thread = static_cast<std::uint32_t>(instructions);
    if (argc >= 4 && !parse_u64(argv[3], config.workload.gpu_threads)) {
      std::cerr << "invalid GPU thread count\n";
      return 2;
    }
    if (argc >= 5 && !parse_u64(argv[4], config.workload.seed)) {
      std::cerr << "invalid seed\n";
      return 2;
    }
    if (argc >= 6) {
      const std::string_view pattern(argv[5]);
      if (pattern == "coalesced") {
        config.workload.memory_pattern = masl::sim::MemoryPattern::Coalesced;
      } else if (pattern == "strided") {
        config.workload.memory_pattern = masl::sim::MemoryPattern::Strided;
      } else if (pattern == "random") {
        config.workload.memory_pattern = masl::sim::MemoryPattern::Random;
      } else {
        std::cerr << "memory pattern must be coalesced, strided or random\n";
        return 2;
      }
    }
  } else {
    usage();
    return 2;
  }

  const auto result = masl::sim::simulate(engine, config);
  std::cout << result.report();
  return result.ok ? 0 : 1;
}
