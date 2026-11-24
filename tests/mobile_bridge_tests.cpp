#include "masl_mobile_bridge.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
  }
}

const std::string kProgram = R"MASL(masl 0.1;
model MobileBridgeTest {
    fn answer() -> i32 {
        return 6 * 7;
    }
    fn twice(value: i32) -> i32 {
        return value * 2;
    }
    fn choose(value: i32) -> i32 {
        if value > 0 {
            return twice(value);
        } else {
            return 0;
        }
    }
    fn sum_to() -> u64 {
        let total: u64 = 0;
        repeat 10 as i {
            set total = total + i;
        }
        return total;
    }
    fn enabled(value: i32) -> bool {
        return value >= 0;
    }
}
)MASL";

}  // namespace

int main() {
  using namespace masl::mobile;

  check(version() == "0.10.0", "mobile bridge reports core version");
  const auto lexical = analyze_source(kProgram, "lex");
  check(lexical.find("Lexed ") != std::string::npos,
        "mobile lex action returns token report");
  check(analyze_source(kProgram, "parse").find("Syntax OK: model MobileBridgeTest") !=
            std::string::npos,
        "mobile parse action accepts valid program");
  check(analyze_source(kProgram, "check").find("Semantic check OK") !=
            std::string::npos,
        "mobile semantic action accepts valid program");
  check(analyze_source(kProgram, "ir").find("MobileBridgeTest") !=
            std::string::npos,
        "mobile Typed IR action lowers valid program");

  std::string error;
  auto compiled = compile_source(kProgram, error);
  check(compiled.has_value(), "mobile compile action emits MASB");
  check(error.empty(), "successful mobile compile has no error text");
  if (compiled.has_value()) {
    check(!compiled->bytes.empty(), "mobile compile emits non-empty bytes");
    check(run_bytecode(compiled->bytes, "answer", {}).find("42") == 0,
          "mobile VM runs zero-argument function");
    check(run_bytecode(compiled->bytes, "twice", {"21"}).find("42") == 0,
          "mobile VM parses integer function arguments");
    check(run_bytecode(compiled->bytes, "choose", {"-7"}).find("0") == 0,
          "mobile VM executes branch path");
    check(run_bytecode(compiled->bytes, "sum_to", {}).find("45") == 0,
          "mobile VM executes bounded repeat loop");
    check(run_bytecode(compiled->bytes, "enabled", {"0"}).find("true") == 0,
          "mobile VM returns boolean values");
    check(run_bytecode(compiled->bytes, "twice", {"bad"}).find("MASL-E9303") !=
              std::string::npos,
          "mobile VM rejects malformed typed argument");
    check(run_bytecode(compiled->bytes, "missing", {}).find("MASL-V7036") !=
              std::string::npos,
          "mobile VM rejects unknown entry function");
  }


  const auto cpu_simulation = simulate_hardware("cpu", 2'000U, 0U, 123U,
                                                "coalesced");
  check(cpu_simulation.find("Status: completed") != std::string::npos &&
        cpu_simulation.find("Engine: CPU") != std::string::npos,
        "mobile bridge runs the CPU simulator and formats its report");
  const auto gpu_simulation = simulate_hardware("gpu", 8U, 1'024U, 123U,
                                                "strided");
  check(gpu_simulation.find("Status: completed") != std::string::npos &&
        gpu_simulation.find("Engine: GPU") != std::string::npos,
        "mobile bridge runs the GPU simulator and formats its report");
  check(simulate_hardware("invalid", 1U, 1U, 1U, "coalesced").find(
            "MASL-SIM-E003") != std::string::npos,
        "mobile bridge rejects unknown simulation engines");
  check(simulate_hardware("gpu", 8U, 1'024U, 123U, "bad-pattern").find(
            "MASL-SIM-E001") != std::string::npos,
        "mobile bridge rejects unknown memory access patterns");
  check(simulate_hardware("cpu", 4'000'001U, 0U, 123U, "coalesced").find(
            "MASL-SIM-E004") != std::string::npos,
        "mobile bridge rejects CPU workloads over the on-device safety budget");
  check(simulate_hardware("gpu", 1U, 4'000'001U, 123U, "coalesced").find(
            "MASL-SIM-E005") != std::string::npos,
        "mobile bridge rejects GPU thread counts over the on-device safety budget");

  const std::string invalid = "masl 0.1; model broken { fn x( -> i32 { return 1; } }";
  check(analyze_source(invalid, "check").find("MASL-") != std::string::npos,
        "mobile front end exposes source diagnostics");
  check(!compile_source(invalid, error).has_value(),
        "mobile compiler refuses invalid source");

  if (failures != 0) {
    std::cerr << failures << " mobile bridge test(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All mobile bridge tests passed.\n";
  return EXIT_SUCCESS;
}
