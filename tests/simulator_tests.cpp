#include "masl/sim/simulator.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
int failures = 0;
int assertions = 0;

void check(bool condition, std::string_view message) {
  ++assertions;
  if (condition) {
    std::cout << "[PASS] " << message << '\n';
  } else {
    std::cerr << "[FAIL] " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using masl::sim::Engine;
  using masl::sim::MemoryPattern;
  using masl::sim::SimulatorConfig;

  SimulatorConfig cpu_config;
  cpu_config.cpu.cores = 4U;
  cpu_config.workload.cpu_instructions = 8'000U;
  cpu_config.workload.memory_instruction_percent = 35U;
  cpu_config.workload.floating_point_percent = 25U;
  cpu_config.workload.branch_percent = 12U;
  cpu_config.workload.tensor_percent = 0U;
  cpu_config.workload.shared_memory_percent = 0U;
  cpu_config.workload.working_set_bytes = 256U * 1024U;
  const auto cpu_result = masl::sim::simulate(Engine::Cpu, cpu_config);
  check(cpu_result.ok, "executes a multi-core CPU trace through the ROB model");
  check(cpu_result.instructions == cpu_config.workload.cpu_instructions,
        "retires exactly the requested CPU instruction count");
  check(cpu_result.cycles > 0U && cpu_result.frequency_hz == cpu_config.cpu.frequency_hz,
        "reports non-zero cycles and the configured CPU clock");
  check(cpu_result.memory_instructions > 0U && cpu_result.memory_transactions > 0U,
        "generates data-memory instructions and cache-line transactions");
  check(cpu_result.dram_transactions <= cpu_result.memory_transactions &&
        cpu_result.dram_bytes <= cpu_result.memory_bytes,
        "DRAM traffic is bounded by all requested cache-line traffic");
  check(cpu_result.l1_stats.accesses ==
            cpu_result.memory_transactions - cpu_result.instruction_cache_transactions,
        "L1D accesses are separated from instruction-fetch cache transactions");
  check(cpu_result.l1_instruction_stats.accesses > 0U &&
        cpu_result.instruction_cache_transactions > 0U,
        "models a separate L1 instruction cache and fetch latency");
  check(cpu_result.branch_mispredictions > 0U,
        "models branch misprediction penalties deterministically");
  const auto cpu_repeat = masl::sim::simulate_cpu(cpu_config);
  check(cpu_repeat.ok && cpu_repeat.cycles == cpu_result.cycles &&
        cpu_repeat.dram_transactions == cpu_result.dram_transactions &&
        cpu_repeat.branch_mispredictions == cpu_result.branch_mispredictions,
        "repeats the same CPU simulation bit-for-bit for a fixed seed");
  check(cpu_result.report().find("MODEL BOUNDARIES") != std::string::npos,
        "includes explicit model boundaries in the CPU report");

  SimulatorConfig invalid_cpu = cpu_config;
  invalid_cpu.cpu.l1_instruction.line_bytes = 128U;
  check(!masl::sim::simulate_cpu(invalid_cpu).ok,
        "rejects incompatible CPU instruction/data cache-line sizes");
  invalid_cpu = cpu_config;
  invalid_cpu.cpu.l1_data.capacity_bytes = 1ULL << 40U;
  check(!masl::sim::simulate_cpu(invalid_cpu).ok,
        "rejects excessive cache allocation before creating cache tag arrays");
  invalid_cpu = cpu_config;
  invalid_cpu.cpu.memory.channels = 1'000'000U;
  check(!masl::sim::simulate_cpu(invalid_cpu).ok,
        "rejects excessive DRAM channel counts before allocation");

  SimulatorConfig branch_config;
  branch_config.cpu.cores = 1U;
  branch_config.workload.cpu_instructions = 512U;
  branch_config.workload.memory_instruction_percent = 0U;
  branch_config.workload.floating_point_percent = 0U;
  branch_config.workload.branch_percent = 100U;
  branch_config.workload.tensor_percent = 0U;
  branch_config.workload.shared_memory_percent = 0U;
  branch_config.cpu.branch_mispredict_percent = 100U;
  const auto branch_result = masl::sim::simulate_cpu(branch_config);
  check(branch_result.ok && branch_result.branch_mispredictions > 0U,
        "handles a branch-heavy CPU workload and 100-percent misprediction setting");

  SimulatorConfig gpu_config;
  gpu_config.workload.gpu_threads = 4'096U;
  gpu_config.workload.gpu_instructions_per_thread = 24U;
  gpu_config.workload.working_set_bytes = 512U * 1024U;
  gpu_config.workload.memory_instruction_percent = 35U;
  gpu_config.workload.store_percent_of_memory = 20U;
  gpu_config.workload.floating_point_percent = 25U;
  gpu_config.workload.branch_percent = 5U;
  gpu_config.workload.tensor_percent = 10U;
  gpu_config.workload.shared_memory_percent = 10U;
  gpu_config.workload.divergence_percent = 20U;
  gpu_config.workload.memory_pattern = MemoryPattern::Coalesced;
  const auto gpu_result = masl::sim::simulate_gpu(gpu_config);
  check(gpu_result.ok, "executes a multi-SM GPU SIMT workload");
  check(gpu_result.instructions == 128U * 24U,
        "executes exactly one dynamic instruction stream per launched warp");
  check(gpu_result.cycles > 0U && gpu_result.link_transfer_cycles > 0U &&
        gpu_result.elapsed_nanoseconds() >
            static_cast<double>(gpu_result.cycles) * 1.0e9 /
                static_cast<double>(gpu_result.frequency_hz),
        "reports GPU kernel cycles separately from host/device transfer time");
  check(gpu_result.active_warps_peak > 0U &&
        gpu_result.effective_occupancy_percent_x100 > 0U,
        "calculates resident warp occupancy from SM register and warp limits");
  check(gpu_result.memory_transactions > 0U && gpu_result.memory_instructions > 0U,
        "models global-memory instructions and coalesced transactions");
  check(gpu_result.memory_transactions <=
        gpu_result.memory_instructions * gpu_config.gpu.warp_width,
        "coalescing never creates more transactions than participating lanes");
  SimulatorConfig strided_gpu_config = gpu_config;
  strided_gpu_config.workload.memory_pattern = MemoryPattern::Strided;
  const auto strided_gpu_result = masl::sim::simulate_gpu(strided_gpu_config);
  check(strided_gpu_result.ok &&
        strided_gpu_result.shared_memory_bank_conflicts > 0U,
        "models deterministic shared-memory bank conflicts for strided lane accesses");
  SimulatorConfig wide_bank_gpu_config = gpu_config;
  wide_bank_gpu_config.gpu.shared_memory_bank_width_bytes = 8U;
  wide_bank_gpu_config.workload.memory_pattern = MemoryPattern::Strided;
  const auto wide_bank_gpu_result = masl::sim::simulate_gpu(wide_bank_gpu_config);
  check(wide_bank_gpu_result.ok &&
        wide_bank_gpu_result.shared_memory_bank_conflicts == 0U,
        "bank-conflict calculation follows the configured shared-memory bank width");
  check(gpu_result.active_lane_slots < gpu_result.issued_lane_slots,
        "accounts for lane under-utilization from warp divergence");
  SimulatorConfig no_divergence_gpu_config = gpu_config;
  no_divergence_gpu_config.workload.divergence_percent = 0U;
  const auto no_divergence_gpu_result = masl::sim::simulate_gpu(
      no_divergence_gpu_config);
  check(no_divergence_gpu_result.ok &&
        no_divergence_gpu_result.active_lane_slots ==
            no_divergence_gpu_result.issued_lane_slots,
        "zero divergence preserves all lanes in each issued warp instruction");
  SimulatorConfig full_divergence_gpu_config = gpu_config;
  full_divergence_gpu_config.workload.divergence_percent = 100U;
  const auto full_divergence_gpu_result = masl::sim::simulate_gpu(
      full_divergence_gpu_config);
  check(full_divergence_gpu_result.ok &&
        full_divergence_gpu_result.active_lane_slots == 0U,
        "100-percent synthetic divergence removes active-lane slots");
  const auto gpu_repeat = masl::sim::simulate_gpu(gpu_config);
  check(gpu_repeat.ok && gpu_repeat.cycles == gpu_result.cycles &&
        gpu_repeat.instructions == gpu_result.instructions &&
        gpu_repeat.memory_transactions == gpu_result.memory_transactions,
        "repeats the same GPU simulation deterministically for a fixed seed");

  SimulatorConfig random_gpu_config = gpu_config;
  random_gpu_config.workload.memory_pattern = MemoryPattern::Random;
  const auto random_gpu_result = masl::sim::simulate_gpu(random_gpu_config);
  check(random_gpu_result.ok && random_gpu_result.memory_transactions >=
        gpu_result.memory_transactions,
        "random lane addresses do not coalesce better than contiguous lane addresses in the test workload");

  SimulatorConfig invalid_gpu = gpu_config;
  invalid_gpu.gpu.warp_width = 0U;
  check(!masl::sim::simulate_gpu(invalid_gpu).ok,
        "rejects a zero-width GPU warp instead of dividing by zero");
  invalid_gpu = gpu_config;
  invalid_gpu.workload.gpu_instructions_per_thread = 0U;
  check(!masl::sim::simulate_gpu(invalid_gpu).ok,
        "rejects a zero-length GPU workload before computing instruction budgets");
  invalid_gpu = gpu_config;
  invalid_gpu.gpu.shared_memory_bytes_per_block =
      invalid_gpu.gpu.shared_memory_bytes_per_sm + 1U;
  check(!masl::sim::simulate_gpu(invalid_gpu).ok,
        "rejects impossible per-block shared-memory requirements");
  invalid_gpu = gpu_config;
  invalid_gpu.maximum_cycles = 1U;
  check(!masl::sim::simulate_gpu(invalid_gpu).ok,
        "enforces a hard GPU cycle budget");

  std::cout << "Simulator assertions: " << assertions
            << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
