#include "masl/sim/simulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <limits>
#include <list>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace masl::sim {
namespace {

constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint32_t kMaximumCores = 128U;
constexpr std::uint32_t kMaximumSms = 256U;
constexpr std::uint32_t kMaximumWarpWidth = 128U;
constexpr std::uint64_t kMaximumWorkingSet = 1ULL << 40U;
constexpr std::uint64_t kMaximumCacheCapacity = 1ULL << 30U;
constexpr std::uint64_t kMaximumTotalCacheSets = 1'000'000U;

bool is_power_of_two(std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

std::uint64_t ceil_div(std::uint64_t numerator, std::uint64_t denominator) noexcept {
  return numerator / denominator + (numerator % denominator == 0U ? 0U : 1U);
}

bool validate_cache(const CacheConfig& cache, std::string& error,
                    const std::string& name) {
  if (cache.capacity_bytes == 0U || cache.capacity_bytes > kMaximumCacheCapacity ||
      cache.line_bytes == 0U || cache.line_bytes > 4096U ||
      cache.associativity == 0U || cache.associativity > 64U ||
      cache.hit_latency_cycles == 0U || !is_power_of_two(cache.line_bytes)) {
    error = name + " cache has invalid or excessive capacity, line size, associativity or latency";
    return false;
  }
  const std::uint64_t set_bytes =
      static_cast<std::uint64_t>(cache.line_bytes) * cache.associativity;
  if (cache.capacity_bytes % set_bytes != 0U ||
      cache.capacity_bytes / set_bytes == 0U ||
      cache.capacity_bytes / set_bytes > 65'536U) {
    error = name + " cache has an invalid set count or exceeds the 65536-set budget";
    return false;
  }
  return true;
}

std::uint64_t cache_set_count(const CacheConfig& cache) noexcept {
  return cache.capacity_bytes /
      (static_cast<std::uint64_t>(cache.line_bytes) * cache.associativity);
}

bool validate_cache_hierarchy(std::uint64_t private_cache_count,
                               std::uint64_t private_cache_sets,
                               const CacheConfig& l2,
                               const CacheConfig& llc,
                               std::string& error,
                               const std::string& name) {
  const std::uint64_t shared_sets = cache_set_count(l2) + cache_set_count(llc);
  if (private_cache_count >
      (kMaximumTotalCacheSets - shared_sets) / private_cache_sets) {
    error = name + " cache hierarchy exceeds the total cache-set memory budget";
    return false;
  }
  return true;
}

bool validate_memory(const MemoryConfig& memory, std::string& error) {
  if (memory.channels == 0U || memory.channels > 256U ||
      memory.bytes_per_cycle_per_channel == 0U ||
      memory.latency_cycles == 0U || memory.latency_cycles > 1'000'000U ||
      memory.line_bytes == 0U || memory.line_bytes > 4096U ||
      !is_power_of_two(memory.line_bytes)) {
    error = "memory controller has invalid channel, bandwidth, latency or line-size settings";
    return false;
  }
  return true;
}

bool validate_workload(const Workload& workload, std::uint64_t maximum_dynamic,
                       std::string& error) {
  if (workload.working_set_bytes == 0U || workload.working_set_bytes > kMaximumWorkingSet) {
    error = "working_set_bytes must be between 1 byte and 1 TiB";
    return false;
  }
  const std::array<std::uint32_t, 8U> percentages{
      workload.memory_instruction_percent, workload.store_percent_of_memory,
      workload.floating_point_percent, workload.branch_percent,
      workload.tensor_percent, workload.shared_memory_percent,
      workload.dependency_percent, workload.divergence_percent};
  if (std::any_of(percentages.begin(), percentages.end(),
                  [](std::uint32_t value) { return value > 100U; })) {
    error = "workload percentages must be in the range 0..100";
    return false;
  }
  if (workload.memory_instruction_percent + workload.floating_point_percent +
          workload.branch_percent + workload.tensor_percent +
          workload.shared_memory_percent >
      100U) {
    error = "instruction mix percentages exceed 100 percent";
    return false;
  }
  if (workload.store_percent_of_memory + workload.dependency_percent > 200U) {
    error = "store and dependency percentages are outside their supported range";
    return false;
  }
  if (workload.cpu_instructions > maximum_dynamic ||
      workload.cpu_instructions == 0U) {
    error = "CPU instruction count is zero or exceeds the configured dynamic-instruction budget";
    return false;
  }
  if (workload.gpu_threads == 0U || workload.gpu_instructions_per_thread == 0U) {
    error = "GPU thread count and instructions per thread must both be non-zero";
    return false;
  }
  return true;
}

bool validate_cpu(const SimulatorConfig& config, std::string& error) {
  const CpuConfig& cpu = config.cpu;
  if (cpu.cores == 0U || cpu.cores > kMaximumCores || cpu.frequency_hz == 0U ||
      cpu.fetch_width == 0U || cpu.fetch_width > 64U ||
      cpu.issue_width == 0U || cpu.issue_width > 64U ||
      cpu.retire_width == 0U || cpu.retire_width > 64U ||
      cpu.reorder_buffer_entries == 0U || cpu.reorder_buffer_entries > 4096U ||
      cpu.integer_units == 0U || cpu.integer_units > 256U ||
      cpu.floating_point_units == 0U || cpu.floating_point_units > 256U ||
      cpu.load_store_units == 0U || cpu.load_store_units > 256U || cpu.integer_latency == 0U ||
      cpu.floating_point_latency == 0U || cpu.branch_mispredict_percent > 100U ||
      config.maximum_cycles == 0U || config.maximum_dynamic_instructions == 0U) {
    error = "CPU configuration contains a zero or out-of-range core, clock, width, ROB, unit or limit value";
    return false;
  }
  if (!validate_cache(cpu.l1_instruction, error, "CPU L1I") ||
      !validate_cache(cpu.l1_data, error, "CPU L1D") ||
      !validate_cache(cpu.l2, error, "CPU L2") ||
      !validate_cache(cpu.last_level, error, "CPU LLC") ||
      !validate_memory(cpu.memory, error)) {
    return false;
  }
  if (cpu.l1_instruction.line_bytes != cpu.l1_data.line_bytes ||
      cpu.l1_data.line_bytes != cpu.l2.line_bytes ||
      cpu.l2.line_bytes != cpu.last_level.line_bytes ||
      cpu.last_level.line_bytes != cpu.memory.line_bytes) {
    error = "CPU cache and memory line sizes must match in this inclusive hierarchy model";
    return false;
  }
  if (!validate_cache_hierarchy(cpu.cores,
          cache_set_count(cpu.l1_instruction) + cache_set_count(cpu.l1_data),
          cpu.l2, cpu.last_level, error, "CPU")) {
    return false;
  }
  return validate_workload(config.workload, config.maximum_dynamic_instructions,
                           error);
}

bool validate_gpu(const SimulatorConfig& config, std::string& error) {
  if (!validate_workload(config.workload, config.maximum_dynamic_instructions, error)) {
    return false;
  }
  const GpuConfig& gpu = config.gpu;
  const std::uint64_t warps = ceil_div(config.workload.gpu_threads,
                                       gpu.warp_width == 0U ? 1U : gpu.warp_width);
  if (gpu.streaming_multiprocessors == 0U ||
      gpu.streaming_multiprocessors > kMaximumSms || gpu.frequency_hz == 0U ||
      gpu.warp_width == 0U || gpu.warp_width > kMaximumWarpWidth ||
      gpu.maximum_warps_per_sm == 0U || gpu.maximum_warps_per_sm > 256U ||
      gpu.issue_width_per_sm == 0U || gpu.issue_width_per_sm > 64U ||
      gpu.register_file_bytes_per_sm == 0U ||
      gpu.register_file_bytes_per_sm > 16U * 1024U * 1024U ||
      gpu.registers_per_thread == 0U || gpu.registers_per_thread > 255U ||
      gpu.shared_memory_banks == 0U || gpu.shared_memory_banks > 256U ||
      gpu.shared_memory_bank_width_bytes == 0U ||
      gpu.shared_memory_bank_width_bytes > 128U ||
      gpu.shared_memory_bytes_per_sm > 16U * 1024U * 1024U ||
      gpu.shared_memory_bytes_per_block > 16U * 1024U * 1024U ||
      gpu.integer_latency == 0U || gpu.floating_point_latency == 0U ||
      gpu.tensor_latency == 0U || gpu.branch_latency == 0U ||
      gpu.link_bandwidth_bytes_per_cycle == 0U || config.maximum_cycles == 0U ||
      config.maximum_dynamic_instructions == 0U) {
    error = "GPU configuration contains an invalid SM, clock, warp, issue, resource, unit or limit value";
    return false;
  }
  if (!validate_cache(gpu.l1_data, error, "GPU L1D") ||
      !validate_cache(gpu.l2, error, "GPU L2") ||
      !validate_cache(gpu.last_level, error, "GPU LLC") ||
      !validate_memory(gpu.memory, error)) {
    return false;
  }
  if (gpu.l1_data.line_bytes != gpu.l2.line_bytes ||
      gpu.l2.line_bytes != gpu.last_level.line_bytes ||
      gpu.last_level.line_bytes != gpu.memory.line_bytes) {
    error = "GPU cache and memory line sizes must match in this inclusive hierarchy model";
    return false;
  }
  if (!validate_cache_hierarchy(gpu.streaming_multiprocessors,
          cache_set_count(gpu.l1_data), gpu.l2, gpu.last_level, error, "GPU")) {
    return false;
  }
  if (gpu.shared_memory_bytes_per_block > gpu.shared_memory_bytes_per_sm) {
    error = "per-block shared-memory demand exceeds shared memory available in one SM";
    return false;
  }
  if (warps > 1'000'000U ||
      warps > config.maximum_dynamic_instructions /
                  config.workload.gpu_instructions_per_thread) {
    error = "GPU warp-instruction count exceeds the configured dynamic-instruction budget";
    return false;
  }
  return true;
}

class Cache final {
 public:
  explicit Cache(CacheConfig config) : config_(config) {
    const std::uint64_t sets64 = config_.capacity_bytes /
        (static_cast<std::uint64_t>(config_.line_bytes) * config_.associativity);
    sets_.resize(static_cast<std::size_t>(sets64));
  }

  bool probe(std::uint64_t address) {
    ++stats_.accesses;
    const std::uint64_t line = address / config_.line_bytes;
    const std::size_t set_index = static_cast<std::size_t>(line % sets_.size());
    const std::uint64_t tag = line / sets_.size();
    auto& set = sets_[set_index];
    for (auto& entry : set) {
      if (entry.tag == tag) {
        ++stats_.hits;
        entry.last_used = ++clock_;
        return true;
      }
    }
    ++stats_.misses;
    return false;
  }

  void insert(std::uint64_t address) {
    const std::uint64_t line = address / config_.line_bytes;
    const std::size_t set_index = static_cast<std::size_t>(line % sets_.size());
    const std::uint64_t tag = line / sets_.size();
    auto& set = sets_[set_index];
    for (auto& entry : set) {
      if (entry.tag == tag) {
        entry.last_used = ++clock_;
        return;
      }
    }
    if (set.size() >= config_.associativity) {
      const auto oldest = std::min_element(set.begin(), set.end(),
          [](const Line& lhs, const Line& rhs) {
            return lhs.last_used < rhs.last_used;
          });
      set.erase(oldest);
    }
    set.push_back(Line{tag, ++clock_});
  }

  [[nodiscard]] const CacheStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::uint32_t latency() const noexcept {
    return config_.hit_latency_cycles;
  }
  [[nodiscard]] std::uint32_t line_bytes() const noexcept {
    return config_.line_bytes;
  }

 private:
  struct Line final {
    std::uint64_t tag;
    std::uint64_t last_used;
  };
  CacheConfig config_;
  std::vector<std::vector<Line>> sets_;
  CacheStats stats_{};
  std::uint64_t clock_{0U};
};

class MemoryController final {
 public:
  explicit MemoryController(MemoryConfig config)
      : config_(config), channel_available_(config.channels, 0U) {}

  std::uint64_t access(std::uint64_t arrival_cycle, std::uint64_t address,
                       std::uint32_t bytes) {
    const std::uint64_t line = address / config_.line_bytes;
    const std::size_t channel = static_cast<std::size_t>(line % config_.channels);
    const std::uint64_t transfer_cycles = std::max<std::uint64_t>(
        1U, ceil_div(bytes, config_.bytes_per_cycle_per_channel));
    const std::uint64_t start = std::max(arrival_cycle, channel_available_[channel]);
    const std::uint64_t next = saturating_add(start, transfer_cycles);
    channel_available_[channel] = next;
    ++transactions_;
    bytes_ += bytes;
    return saturating_add(next, config_.latency_cycles);
  }

  [[nodiscard]] std::uint64_t transactions() const noexcept { return transactions_; }
  [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }

 private:
  static std::uint64_t saturating_add(std::uint64_t lhs,
                                      std::uint64_t rhs) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
  }
  MemoryConfig config_;
  std::vector<std::uint64_t> channel_available_;
  std::uint64_t transactions_{0U};
  std::uint64_t bytes_{0U};
};

class MemoryHierarchy final {
 public:
  MemoryHierarchy(std::uint32_t private_cache_count, CacheConfig l1_config,
                  CacheConfig l2_config, CacheConfig llc_config,
                  MemoryConfig memory_config,
                  std::optional<CacheConfig> l1_instruction_config = std::nullopt)
      : l2_(l2_config), llc_(llc_config), memory_(memory_config) {
    l1_.reserve(private_cache_count);
    if (l1_instruction_config.has_value()) {
      l1_instruction_.reserve(private_cache_count);
    }
    for (std::uint32_t index = 0U; index < private_cache_count; ++index) {
      l1_.emplace_back(l1_config);
      if (l1_instruction_config.has_value()) {
        l1_instruction_.emplace_back(*l1_instruction_config);
      }
    }
  }

  std::uint64_t access(std::uint32_t private_cache, std::uint64_t cycle,
                       std::uint64_t address) {
    ++data_transactions_;
    return access_from(l1_, private_cache, cycle, address);
  }

  std::uint64_t access_instruction(std::uint32_t private_cache,
                                   std::uint64_t cycle,
                                   std::uint64_t address) {
    if (l1_instruction_.empty()) {
      ++data_transactions_;
      return access_from(l1_, private_cache, cycle, address);
    }
    ++instruction_transactions_;
    return access_from(l1_instruction_, private_cache, cycle, address);
  }

  [[nodiscard]] CacheStats l1_stats() const noexcept {
    return aggregate(l1_);
  }
  [[nodiscard]] CacheStats l1_instruction_stats() const noexcept {
    return aggregate(l1_instruction_);
  }
  [[nodiscard]] const CacheStats& l2_stats() const noexcept { return l2_.stats(); }
  [[nodiscard]] const CacheStats& last_level_stats() const noexcept {
    return llc_.stats();
  }
  [[nodiscard]] std::uint64_t transactions() const noexcept {
    return data_transactions_ + instruction_transactions_;
  }
  [[nodiscard]] std::uint64_t instruction_transactions() const noexcept {
    return instruction_transactions_;
  }
  [[nodiscard]] std::uint64_t memory_transactions() const noexcept {
    return memory_.transactions();
  }
  [[nodiscard]] std::uint64_t memory_bytes() const noexcept {
    return transactions() * static_cast<std::uint64_t>(l1_.front().line_bytes());
  }
  [[nodiscard]] std::uint64_t dram_bytes() const noexcept { return memory_.bytes(); }

 private:
  static std::uint64_t add_cycles(std::uint64_t lhs,
                                  std::uint64_t rhs) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
  }

  static CacheStats aggregate(const std::vector<Cache>& caches) noexcept {
    CacheStats result{};
    for (const auto& cache : caches) {
      const auto& stats = cache.stats();
      result.accesses += stats.accesses;
      result.hits += stats.hits;
      result.misses += stats.misses;
    }
    return result;
  }

  std::uint64_t access_from(std::vector<Cache>& first_level,
                            std::uint32_t private_cache,
                            std::uint64_t cycle, std::uint64_t address) {
    if (private_cache >= first_level.size()) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t line = address - (address % first_level.front().line_bytes());
    std::uint64_t ready = cycle;
    Cache& first = first_level[private_cache];
    if (first.probe(line)) {
      return add_cycles(ready, first.latency());
    }
    ready = add_cycles(ready, first.latency());
    if (l2_.probe(line)) {
      first.insert(line);
      return add_cycles(ready, l2_.latency());
    }
    ready = add_cycles(ready, l2_.latency());
    if (llc_.probe(line)) {
      l2_.insert(line);
      first.insert(line);
      return add_cycles(ready, llc_.latency());
    }
    ready = add_cycles(ready, llc_.latency());
    const std::uint64_t completion = memory_.access(
        ready, line, first_level.front().line_bytes());
    llc_.insert(line);
    l2_.insert(line);
    first.insert(line);
    return completion;
  }

  std::vector<Cache> l1_;
  std::vector<Cache> l1_instruction_;
  Cache l2_;
  Cache llc_;
  MemoryController memory_;
  std::uint64_t data_transactions_{0U};
  std::uint64_t instruction_transactions_{0U};
};

enum class CpuOp : std::uint8_t { Integer, FloatingPoint, Load, Store, Branch };

struct CpuInstruction final {
  std::uint64_t sequence{0U};
  CpuOp op{CpuOp::Integer};
  std::array<std::int64_t, 2U> dependencies{-1, -1};
  std::uint64_t address{0U};
  bool mispredict{false};
  bool issued{false};
  std::uint64_t completion_cycle{kNever};
};

struct CpuCoreState final {
  explicit CpuCoreState(std::uint64_t instruction_count, std::uint64_t seed,
                       const CpuConfig& config)
      : target(instruction_count), random(seed),
        integer_units(config.integer_units, 0U),
        fp_units(config.floating_point_units, 0U),
        memory_units(config.load_store_units, 0U), branch_units(1U, 0U) {
    last_writer.fill(-1);
  }

  std::uint64_t target{0U};
  std::uint64_t generated{0U};
  std::mt19937_64 random;
  std::array<std::int64_t, 64U> last_writer{};
  std::vector<std::uint64_t> producer_ready;
  std::vector<CpuOp> producer_op;
  std::deque<CpuInstruction> rob;
  std::vector<std::uint64_t> integer_units;
  std::vector<std::uint64_t> fp_units;
  std::vector<std::uint64_t> memory_units;
  std::vector<std::uint64_t> branch_units;
  std::uint64_t fetch_blocked_until{0U};
  std::uint64_t fetch_ready_cycle{0U};
  std::uint64_t retired{0U};
  std::uint64_t mispredicts{0U};
  std::uint64_t dependency_stalls{0U};
  std::uint64_t memory_dependency_stalls{0U};
  std::uint64_t frontend_stalls{0U};
  std::uint64_t issue_count{0U};

  [[nodiscard]] bool done() const noexcept {
    return generated >= target && rob.empty();
  }
};

CpuOp choose_cpu_op(std::mt19937_64& random, const Workload& workload) {
  const std::uint32_t choice = static_cast<std::uint32_t>(random() % 100U);
  if (choice < workload.memory_instruction_percent) {
    return random() % 100U < workload.store_percent_of_memory
        ? CpuOp::Store : CpuOp::Load;
  }
  if (choice < workload.memory_instruction_percent + workload.floating_point_percent) {
    return CpuOp::FloatingPoint;
  }
  if (choice < workload.memory_instruction_percent + workload.floating_point_percent +
                    workload.branch_percent) {
    return CpuOp::Branch;
  }
  return CpuOp::Integer;
}

void fetch_cpu(CpuCoreState& core, const CpuConfig& config,
               const Workload& workload, MemoryHierarchy& memory,
               std::uint32_t core_id, std::uint64_t cycle) {
  if (core.generated >= core.target) return;
  if (core.fetch_blocked_until > cycle || core.fetch_ready_cycle > cycle) {
    ++core.frontend_stalls;
    return;
  }
  if (core.fetch_ready_cycle != 0U) {
    core.fetch_ready_cycle = 0U;
  } else {
    const std::uint64_t available = std::min<std::uint64_t>(
        config.fetch_width,
        std::min<std::uint64_t>(core.target - core.generated,
            config.reorder_buffer_entries - core.rob.size()));
    std::uint64_t ready = cycle;
    std::uint64_t previous_line = kNever;
    for (std::uint64_t offset = 0U; offset < available; ++offset) {
      const std::uint64_t instruction_address = (core.generated + offset) * 4U;
      const std::uint64_t line = instruction_address -
          instruction_address % config.l1_instruction.line_bytes;
      if (line == previous_line) continue;
      ready = std::max(ready, memory.access_instruction(core_id, cycle, line));
      previous_line = line;
    }
    core.fetch_ready_cycle = ready;
    if (ready > cycle) {
      ++core.frontend_stalls;
      return;
    }
  }
  std::uint32_t fetched = 0U;
  while (fetched < config.fetch_width && core.generated < core.target &&
         core.rob.size() < config.reorder_buffer_entries) {
    CpuInstruction instruction;
    instruction.sequence = core.generated;
    instruction.op = choose_cpu_op(core.random, workload);
    for (std::size_t source = 0U; source < instruction.dependencies.size(); ++source) {
      if (core.random() % 100U < workload.dependency_percent) {
        const std::size_t reg = static_cast<std::size_t>(core.random() % core.last_writer.size());
        instruction.dependencies[source] = core.last_writer[reg];
      }
    }
    const std::size_t destination = static_cast<std::size_t>(core.random() % core.last_writer.size());
    const std::uint64_t sequence = core.generated;
    core.last_writer[destination] = static_cast<std::int64_t>(sequence);
    const std::uint64_t line_count = std::max<std::uint64_t>(
        1U, workload.working_set_bytes / 64U);
    instruction.address = (core.random() % line_count) * 64U;
    instruction.mispredict = instruction.op == CpuOp::Branch &&
        core.random() % 100U < config.branch_mispredict_percent;
    core.producer_ready.push_back(kNever);
    core.producer_op.push_back(instruction.op);
    core.rob.push_back(instruction);
    ++core.generated;
    ++fetched;
  }
}

bool dependencies_ready(const CpuCoreState& core,
                        const CpuInstruction& instruction,
                        std::uint64_t cycle, bool& memory_dependency) {
  memory_dependency = false;
  for (const std::int64_t producer : instruction.dependencies) {
    if (producer < 0) continue;
    const auto index = static_cast<std::size_t>(producer);
    if (index >= core.producer_ready.size() || core.producer_ready[index] == kNever ||
        core.producer_ready[index] > cycle) {
      if (index < core.producer_op.size()) {
        const CpuOp op = core.producer_op[index];
        memory_dependency = memory_dependency || op == CpuOp::Load || op == CpuOp::Store;
      }
      return false;
    }
  }
  return true;
}

std::vector<std::uint64_t>* select_fu(CpuCoreState& core, CpuOp op) {
  switch (op) {
    case CpuOp::Integer: return &core.integer_units;
    case CpuOp::FloatingPoint: return &core.fp_units;
    case CpuOp::Load:
    case CpuOp::Store: return &core.memory_units;
    case CpuOp::Branch: return &core.branch_units;
  }
  return nullptr;
}

void issue_cpu(CpuCoreState& core, const CpuConfig& config,
               MemoryHierarchy& memory, std::uint32_t core_id,
               std::uint64_t cycle, SimulationResult& result) {
  std::uint32_t issued = 0U;
  bool saw_dependency = false;
  bool saw_memory_dependency = false;
  for (auto& instruction : core.rob) {
    if (issued >= config.issue_width) break;
    if (instruction.issued) continue;
    bool memory_dependency = false;
    if (!dependencies_ready(core, instruction, cycle, memory_dependency)) {
      saw_dependency = true;
      saw_memory_dependency = saw_memory_dependency || memory_dependency;
      continue;
    }
    auto* units = select_fu(core, instruction.op);
    if (units == nullptr) continue;
    auto unit = std::find_if(units->begin(), units->end(),
        [cycle](std::uint64_t available) { return available <= cycle; });
    if (unit == units->end()) continue;
    *unit = cycle + 1U;
    std::uint64_t latency = config.integer_latency;
    switch (instruction.op) {
      case CpuOp::Integer: latency = config.integer_latency; break;
      case CpuOp::FloatingPoint: latency = config.floating_point_latency; break;
      case CpuOp::Load:
      case CpuOp::Store:
        ++result.memory_instructions;
        latency = memory.access(core_id, cycle, instruction.address) - cycle;
        if (latency == 0U) latency = 1U;
        break;
      case CpuOp::Branch:
        latency = 1U;
        if (instruction.mispredict) {
          ++core.mispredicts;
          core.fetch_blocked_until = std::max(core.fetch_blocked_until,
                                               cycle + config.branch_mispredict_penalty);
        }
        break;
    }
    if (latency > std::numeric_limits<std::uint64_t>::max() - cycle) {
      instruction.completion_cycle = kNever;
    } else {
      instruction.completion_cycle = cycle + latency;
    }
    instruction.issued = true;
    const std::size_t sequence = static_cast<std::size_t>(instruction.sequence);
    if (sequence < core.producer_ready.size()) {
      core.producer_ready[sequence] = instruction.completion_cycle;
    }
    ++issued;
    ++core.issue_count;
  }
  if (issued == 0U && !core.rob.empty()) {
    if (saw_dependency) {
      ++core.dependency_stalls;
      if (saw_memory_dependency) ++core.memory_dependency_stalls;
    }
  }
}

struct GpuWarp final {
  std::uint64_t id{0U};
  std::uint32_t remaining{0U};
  std::uint32_t pc{0U};
  std::uint64_t ready_cycle{0U};
};

struct GpuSm final {
  std::deque<GpuWarp> waiting;
  std::vector<GpuWarp> active;
  std::size_t round_robin{0U};
};

enum class GpuOp : std::uint8_t { Integer, FloatingPoint, Load, Store, Branch, Tensor, Shared };

GpuOp choose_gpu_op(std::mt19937_64& random, const Workload& workload) {
  const std::uint32_t choice = static_cast<std::uint32_t>(random() % 100U);
  std::uint32_t boundary = workload.memory_instruction_percent;
  if (choice < boundary) {
    return random() % 100U < workload.store_percent_of_memory
        ? GpuOp::Store : GpuOp::Load;
  }
  boundary += workload.floating_point_percent;
  if (choice < boundary) return GpuOp::FloatingPoint;
  boundary += workload.branch_percent;
  if (choice < boundary) return GpuOp::Branch;
  boundary += workload.tensor_percent;
  if (choice < boundary) return GpuOp::Tensor;
  boundary += workload.shared_memory_percent;
  if (choice < boundary) return GpuOp::Shared;
  return GpuOp::Integer;
}

std::uint32_t shared_bank_conflicts(const GpuConfig& gpu,
                                    std::mt19937_64& random,
                                    MemoryPattern pattern) {
  std::vector<std::uint32_t> bank_hits(gpu.shared_memory_banks, 0U);
  for (std::uint32_t lane = 0U; lane < gpu.warp_width; ++lane) {
    std::uint64_t address_bytes = 0U;
    if (pattern == MemoryPattern::Coalesced) {
      address_bytes = static_cast<std::uint64_t>(lane) * 4U;
    } else if (pattern == MemoryPattern::Strided) {
      address_bytes = static_cast<std::uint64_t>(lane) * 8U;
    } else {
      address_bytes = (random() % gpu.shared_memory_banks) *
          gpu.shared_memory_bank_width_bytes;
    }
    const std::uint32_t bank = static_cast<std::uint32_t>(
        (address_bytes / gpu.shared_memory_bank_width_bytes) %
        gpu.shared_memory_banks);
    ++bank_hits[bank];
  }
  if (bank_hits.empty()) return 0U;
  return *std::max_element(bank_hits.begin(), bank_hits.end());
}

std::vector<std::uint64_t> gpu_global_addresses(const GpuConfig& gpu,
                                                const Workload& workload,
                                                const GpuWarp& warp,
                                                std::mt19937_64& random) {
  const std::uint64_t line_bytes = gpu.memory.line_bytes;
  const std::uint64_t lines = std::max<std::uint64_t>(
      1U, workload.working_set_bytes / line_bytes);
  std::vector<std::uint64_t> addresses;
  addresses.reserve(gpu.warp_width);
  for (std::uint32_t lane = 0U; lane < gpu.warp_width; ++lane) {
    std::uint64_t line = 0U;
    if (workload.memory_pattern == MemoryPattern::Coalesced) {
      const std::uint64_t lane_bytes = static_cast<std::uint64_t>(lane) * 4U;
      const std::uint64_t base = (warp.id * 131U + warp.pc * 17U) % lines;
      line = (base + lane_bytes / line_bytes) % lines;
    } else if (workload.memory_pattern == MemoryPattern::Strided) {
      const std::uint64_t base = (warp.id * 131U + warp.pc * 17U) % lines;
      line = (base + static_cast<std::uint64_t>(lane) * 128U / line_bytes) % lines;
    } else {
      line = random() % lines;
    }
    const std::uint64_t address = line * line_bytes;
    if (std::find(addresses.begin(), addresses.end(), address) == addresses.end()) {
      addresses.push_back(address);
    }
  }
  return addresses;
}

std::uint64_t calculate_link_cycles(const GpuConfig& gpu,
                                    const Workload& workload) {
  std::uint64_t cycles = 0U;
  if (workload.host_to_device_bytes > 0U) {
    cycles += gpu.link_latency_cycles +
        ceil_div(workload.host_to_device_bytes, gpu.link_bandwidth_bytes_per_cycle);
  }
  if (workload.device_to_host_bytes > 0U) {
    cycles += gpu.link_latency_cycles +
        ceil_div(workload.device_to_host_bytes, gpu.link_bandwidth_bytes_per_cycle);
  }
  return cycles;
}

std::string percentage(double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value << '%';
  return output.str();
}

}  // namespace

double SimulationResult::elapsed_nanoseconds() const noexcept {
  if (frequency_hz == 0U) return 0.0;
  const std::uint64_t total_cycles = cycles + link_transfer_cycles;
  return static_cast<double>(total_cycles) * 1'000'000'000.0 /
         static_cast<double>(frequency_hz);
}

double SimulationResult::ipc() const noexcept {
  return cycles == 0U ? 0.0 : static_cast<double>(instructions) /
      static_cast<double>(cycles);
}

double SimulationResult::cache_hit_rate(const CacheStats& stats) const noexcept {
  return stats.accesses == 0U ? 0.0 :
      static_cast<double>(stats.hits) * 100.0 / static_cast<double>(stats.accesses);
}

std::string SimulationResult::report() const {
  if (!ok) return "MASL simulator failed: " + error;
  std::ostringstream output;
  output << "MASL ARCHITECTURE SIMULATOR 0.10.0\n";
  output << "Engine: " << engine_name(engine) << "\n";
  output << "Status: completed (deterministic trace-driven model)\n\n";
  output << "PERFORMANCE\n";
  output << "  Cycles: " << cycles << "\n";
  output << "  Clock: " << frequency_hz << " Hz\n";
  output << "  Estimated time: " << std::fixed << std::setprecision(3)
         << elapsed_nanoseconds() << " ns\n";
  output << "  Dynamic instructions: " << instructions << "\n";
  output << "  IPC (engine instruction/cycle): " << std::setprecision(4)
         << ipc() << "\n";
  output << "  Stalled cycles: " << stalled_cycles << "\n";
  if (engine == Engine::Cpu) {
    output << "  Branch mispredictions: " << branch_mispredictions << "\n";
    output << "  Dependency-stall core-cycles: " << dependency_stall_cycles << "\n";
    output << "  Memory-dependency core-cycles: " << memory_stall_cycles << "\n";
    output << "  Front-end blocked core-cycles: " << frontend_stall_cycles << "\n";
  } else {
    output << "  Peak active warps: " << active_warps_peak << "\n";
    output << "  Effective warp occupancy: "
           << percentage(static_cast<double>(effective_occupancy_percent_x100) / 100.0)
           << "\n";
    const double lane_utilization = issued_lane_slots == 0U ? 0.0 :
        static_cast<double>(active_lane_slots) * 100.0 /
        static_cast<double>(issued_lane_slots);
    output << "  Active-lane efficiency: " << percentage(lane_utilization) << "\n";
    output << "  Shared-memory bank conflicts: "
           << shared_memory_bank_conflicts << "\n";
    output << "  Host/device link cycles: " << link_transfer_cycles << "\n";
    output << "  No-ready SM cycles: " << memory_stall_cycles << "\n";
  }
  output << "\nMEMORY SYSTEM\n";
  output << "  Memory instructions: " << memory_instructions << "\n";
  output << "  L1 cache-line requests (CPU I-fetch + data or GPU global): "
         << memory_transactions << "\n";
  output << "  Requested cache-line bytes: " << memory_bytes << "\n";
  output << "  DRAM transactions: " << dram_transactions << "\n";
  output << "  DRAM bytes transferred: " << dram_bytes << "\n";
  if (engine == Engine::Cpu) {
    output << "  L1I line accesses: " << l1_instruction_stats.accesses
           << ", hit rate: " << percentage(cache_hit_rate(l1_instruction_stats)) << "\n";
  }
  output << "  L1D hit rate: " << percentage(cache_hit_rate(l1_stats)) << "\n";
  output << "  L2 hit rate: " << percentage(cache_hit_rate(l2_stats)) << "\n";
  output << "  LLC hit rate: " << percentage(cache_hit_rate(last_level_stats)) << "\n";
  output << "\nMODEL BOUNDARIES\n";
  output << "  Inclusive tag-based caches; fixed hit latencies; loads/stores share a simplified line-fill path.\n";
  output << "  DRAM uses independent channel queues; no row/bank/refresh timing.\n";
  output << "  No delayed fill merging/MSHRs, dirty writeback, cache coherence, OS noise, DVFS or measured power.\n";
  output << "  CPU model is bounded ROB/scoreboard; GPU model is generated warp-level SIMT.\n";
  return output.str();
}

std::string engine_name(Engine engine) {
  return engine == Engine::Cpu ? "CPU" : "GPU";
}

std::string memory_pattern_name(MemoryPattern pattern) {
  switch (pattern) {
    case MemoryPattern::Coalesced: return "coalesced";
    case MemoryPattern::Strided: return "strided";
    case MemoryPattern::Random: return "random";
  }
  return "unknown";
}

SimulationResult simulate_cpu(const SimulatorConfig& config) {
  SimulationResult result;
  result.engine = Engine::Cpu;
  result.frequency_hz = config.cpu.frequency_hz;
  if (!validate_cpu(config, result.error)) return result;

  const std::uint64_t base_instructions = config.workload.cpu_instructions /
      config.cpu.cores;
  const std::uint64_t extra_instructions = config.workload.cpu_instructions %
      config.cpu.cores;
  std::vector<CpuCoreState> cores;
  cores.reserve(config.cpu.cores);
  for (std::uint32_t core = 0U; core < config.cpu.cores; ++core) {
    const std::uint64_t count = base_instructions +
        (core < extra_instructions ? 1U : 0U);
    cores.emplace_back(count, config.workload.seed +
        static_cast<std::uint64_t>(core) * 0x9E3779B97F4A7C15ULL, config.cpu);
  }
  MemoryHierarchy memory(config.cpu.cores, config.cpu.l1_data, config.cpu.l2,
                         config.cpu.last_level, config.cpu.memory,
                         config.cpu.l1_instruction);

  std::uint64_t cycle = 0U;
  for (;;) {
    bool all_done = true;
    std::uint64_t total_issued_this_cycle = 0U;
    for (std::uint32_t core_id = 0U; core_id < config.cpu.cores; ++core_id) {
      CpuCoreState& core = cores[core_id];
      if (!core.done()) all_done = false;
      if (core.fetch_blocked_until > 0U && core.fetch_blocked_until <= cycle) {
        core.fetch_blocked_until = 0U;
      }
      std::uint32_t retired = 0U;
      while (!core.rob.empty() && retired < config.cpu.retire_width &&
             core.rob.front().issued && core.rob.front().completion_cycle <= cycle) {
        core.rob.pop_front();
        ++core.retired;
        ++retired;
      }
      fetch_cpu(core, config.cpu, config.workload, memory, core_id, cycle);
      const std::uint64_t before = core.issue_count;
      issue_cpu(core, config.cpu, memory, core_id, cycle, result);
      total_issued_this_cycle += core.issue_count - before;
      result.branch_mispredictions += 0U;
    }
    if (all_done) break;
    if (total_issued_this_cycle == 0U) ++result.stalled_cycles;
    if (cycle >= config.maximum_cycles) {
      result.error = "CPU simulation reached maximum_cycles before all instructions retired";
      return result;
    }
    ++cycle;
  }

  for (const auto& core : cores) {
    result.instructions += core.retired;
    result.branch_mispredictions += core.mispredicts;
    result.dependency_stall_cycles += core.dependency_stalls;
    result.memory_stall_cycles += core.memory_dependency_stalls;
    result.frontend_stall_cycles += core.frontend_stalls;
  }
  result.cycles = cycle;
  result.memory_transactions = memory.transactions();
  result.instruction_cache_transactions = memory.instruction_transactions();
  result.memory_bytes = memory.memory_bytes();
  result.dram_transactions = memory.memory_transactions();
  result.dram_bytes = memory.dram_bytes();
  result.l1_instruction_stats = memory.l1_instruction_stats();
  result.l1_stats = memory.l1_stats();
  result.l2_stats = memory.l2_stats();
  result.last_level_stats = memory.last_level_stats();
  if (result.instructions != config.workload.cpu_instructions) {
    result.error = "internal CPU accounting error: retired count differs from requested instruction count";
    return result;
  }
  result.ok = true;
  return result;
}

SimulationResult simulate_gpu(const SimulatorConfig& config) {
  SimulationResult result;
  result.engine = Engine::Gpu;
  result.frequency_hz = config.gpu.frequency_hz;
  if (!validate_gpu(config, result.error)) return result;

  const GpuConfig& gpu = config.gpu;
  const Workload& workload = config.workload;
  const std::uint64_t warp_count = ceil_div(workload.gpu_threads, gpu.warp_width);
  const std::uint64_t threads_per_block = 256U;
  const std::uint64_t warps_per_block = ceil_div(threads_per_block, gpu.warp_width);
  const std::uint64_t registers_per_block =
      static_cast<std::uint64_t>(gpu.registers_per_thread) * threads_per_block * 4U;
  if (registers_per_block == 0U || registers_per_block > gpu.register_file_bytes_per_sm) {
    result.error = "register allocation prevents even one thread block from residing on an SM";
    return result;
  }
  const std::uint64_t blocks_by_registers =
      gpu.register_file_bytes_per_sm / registers_per_block;
  const std::uint64_t blocks_by_shared = gpu.shared_memory_bytes_per_block == 0U
      ? 16U
      : gpu.shared_memory_bytes_per_sm / gpu.shared_memory_bytes_per_block;
  const std::uint64_t blocks_by_warps = gpu.maximum_warps_per_sm / warps_per_block;
  const std::uint64_t resident_blocks = std::min<std::uint64_t>(
      16U, std::min(blocks_by_registers,
                    std::min(blocks_by_shared, blocks_by_warps)));
  const std::uint64_t resident_warps = resident_blocks * warps_per_block;
  if (resident_warps == 0U) {
    result.error = "GPU occupancy calculation produced zero resident warps; check registers, block size and shared memory";
    return result;
  }

  std::vector<GpuSm> sms(gpu.streaming_multiprocessors);
  for (std::uint64_t warp_id = 0U; warp_id < warp_count; ++warp_id) {
    const std::uint32_t sm_id = static_cast<std::uint32_t>(
        warp_id % gpu.streaming_multiprocessors);
    sms[sm_id].waiting.push_back(GpuWarp{
        warp_id, workload.gpu_instructions_per_thread, 0U, 0U});
  }
  MemoryHierarchy memory(gpu.streaming_multiprocessors, gpu.l1_data, gpu.l2,
                         gpu.last_level, gpu.memory);
  std::mt19937_64 random(workload.seed);
  const std::uint64_t link_cycles = calculate_link_cycles(gpu, workload);
  result.link_transfer_cycles = link_cycles;
  std::uint64_t cycle = link_cycles;
  std::uint64_t completed_warps = 0U;
  std::uint64_t occupancy_warp_cycles = 0U;
  const std::uint64_t maximum_warp_instructions =
      warp_count * workload.gpu_instructions_per_thread;

  while (completed_warps < warp_count) {
    if (cycle >= config.maximum_cycles) {
      result.error = "GPU simulation reached maximum_cycles before all warps completed";
      return result;
    }

    std::uint64_t active_warps_this_cycle = 0U;
    for (auto& sm : sms) {
      auto iterator = sm.active.begin();
      while (iterator != sm.active.end()) {
        if (iterator->remaining == 0U && iterator->ready_cycle <= cycle) {
          iterator = sm.active.erase(iterator);
          ++completed_warps;
        } else {
          ++iterator;
        }
      }
      while (sm.active.size() < resident_warps && !sm.waiting.empty()) {
        sm.active.push_back(sm.waiting.front());
        sm.waiting.pop_front();
      }
      active_warps_this_cycle += sm.active.size();
    }
    if (completed_warps >= warp_count) break;

    std::uint64_t ready_warps_this_cycle = 0U;
    for (std::uint32_t sm_id = 0U; sm_id < gpu.streaming_multiprocessors; ++sm_id) {
      GpuSm& sm = sms[sm_id];
      std::vector<bool> issued_this_cycle(sm.active.size(), false);
      std::uint64_t sm_ready_this_cycle = 0U;
      for (std::uint32_t slot = 0U; slot < gpu.issue_width_per_sm; ++slot) {
        if (sm.active.empty()) break;
        bool found = false;
        std::size_t selected = 0U;
        for (std::size_t offset = 0U; offset < sm.active.size(); ++offset) {
          const std::size_t index = (sm.round_robin + offset) % sm.active.size();
          if (!issued_this_cycle[index] && sm.active[index].remaining > 0U &&
              sm.active[index].ready_cycle <= cycle) {
            selected = index;
            found = true;
            break;
          }
        }
        if (!found) break;

        GpuWarp& warp = sm.active[selected];
        issued_this_cycle[selected] = true;
        sm.round_robin = (selected + 1U) % sm.active.size();
        ++ready_warps_this_cycle;
        ++sm_ready_this_cycle;
        const GpuOp op = choose_gpu_op(random, workload);
        std::uint64_t latency = gpu.integer_latency;
        const std::uint64_t active_lanes =
            (static_cast<std::uint64_t>(gpu.warp_width) *
             (100U - workload.divergence_percent) + 50U) / 100U;
        result.active_lane_slots += active_lanes;
        result.issued_lane_slots += gpu.warp_width;
        switch (op) {
          case GpuOp::Integer:
            latency = gpu.integer_latency;
            break;
          case GpuOp::FloatingPoint:
            latency = gpu.floating_point_latency;
            break;
          case GpuOp::Load:
          case GpuOp::Store: {
            ++result.memory_instructions;
            const auto addresses = gpu_global_addresses(gpu, workload, warp, random);
            std::uint64_t ready = cycle;
            for (const std::uint64_t address : addresses) {
              ready = std::max(ready, memory.access(sm_id, cycle, address));
            }
            latency = std::max<std::uint64_t>(1U, ready - cycle);
            break;
          }
          case GpuOp::Branch:
            latency = gpu.branch_latency;
            break;
          case GpuOp::Tensor:
            latency = gpu.tensor_latency;
            break;
          case GpuOp::Shared: {
            const std::uint32_t conflicts = shared_bank_conflicts(
                gpu, random, workload.memory_pattern);
            if (conflicts > 1U) {
              result.shared_memory_bank_conflicts += conflicts - 1U;
            }
            latency = std::max<std::uint32_t>(1U, conflicts);
            break;
          }
        }
        warp.ready_cycle = cycle + std::max<std::uint64_t>(1U, latency);
        --warp.remaining;
        ++warp.pc;
        ++result.instructions;
      }
      if (!sm.active.empty() && sm_ready_this_cycle == 0U) {
        ++result.memory_stall_cycles;
      }
    }

    result.active_warps_peak = std::max(result.active_warps_peak,
                                        active_warps_this_cycle);
    occupancy_warp_cycles += active_warps_this_cycle;
    if (ready_warps_this_cycle == 0U && active_warps_this_cycle > 0U) {
      ++result.stalled_cycles;
    }
    ++cycle;
  }

  const std::uint64_t kernel_cycles = cycle > link_cycles ? cycle - link_cycles : 0U;
  result.cycles = kernel_cycles;
  result.effective_occupancy_percent_x100 =
      kernel_cycles == 0U ? 0U : static_cast<std::uint64_t>(
          static_cast<long double>(occupancy_warp_cycles) * 10'000.0L /
          (static_cast<long double>(gpu.streaming_multiprocessors) *
           gpu.maximum_warps_per_sm * kernel_cycles));
  result.memory_transactions = memory.transactions();
  result.memory_bytes = memory.memory_bytes();
  result.dram_transactions = memory.memory_transactions();
  result.dram_bytes = memory.dram_bytes();
  result.l1_stats = memory.l1_stats();
  result.l2_stats = memory.l2_stats();
  result.last_level_stats = memory.last_level_stats();
  if (result.instructions != maximum_warp_instructions) {
    result.error = "internal GPU accounting error: issued warp-instruction count differs from requested work";
    return result;
  }
  result.ok = true;
  return result;
}

SimulationResult simulate(Engine engine, const SimulatorConfig& config) {
  return engine == Engine::Cpu ? simulate_cpu(config) : simulate_gpu(config);
}

}  // namespace masl::sim
