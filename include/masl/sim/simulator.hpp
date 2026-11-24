#pragma once

#include <cstdint>
#include <string>

namespace masl::sim {

enum class Engine : std::uint8_t { Cpu, Gpu };
enum class MemoryPattern : std::uint8_t { Coalesced, Strided, Random };

struct CacheConfig final {
  std::uint64_t capacity_bytes{32U * 1024U};
  std::uint32_t line_bytes{64U};
  std::uint32_t associativity{8U};
  std::uint32_t hit_latency_cycles{1U};
};

struct MemoryConfig final {
  std::uint32_t channels{8U};
  std::uint32_t bytes_per_cycle_per_channel{32U};
  std::uint32_t latency_cycles{180U};
  std::uint32_t line_bytes{64U};
};

struct CpuConfig final {
  std::uint32_t cores{8U};
  std::uint64_t frequency_hz{3'000'000'000ULL};
  std::uint32_t fetch_width{4U};
  std::uint32_t issue_width{4U};
  std::uint32_t retire_width{4U};
  std::uint32_t reorder_buffer_entries{192U};
  std::uint32_t integer_units{2U};
  std::uint32_t floating_point_units{1U};
  std::uint32_t load_store_units{2U};
  std::uint32_t branch_mispredict_penalty{14U};
  std::uint32_t branch_mispredict_percent{5U};
  std::uint32_t integer_latency{1U};
  std::uint32_t floating_point_latency{4U};
  CacheConfig l1_instruction{32U * 1024U, 64U, 8U, 1U};
  CacheConfig l1_data{32U * 1024U, 64U, 8U, 1U};
  CacheConfig l2{512U * 1024U, 64U, 8U, 9U};
  CacheConfig last_level{16U * 1024U * 1024U, 64U, 16U, 28U};
  MemoryConfig memory{};
};

struct GpuConfig final {
  std::uint32_t streaming_multiprocessors{32U};
  std::uint64_t frequency_hz{1'500'000'000ULL};
  std::uint32_t warp_width{32U};
  std::uint32_t maximum_warps_per_sm{48U};
  std::uint32_t issue_width_per_sm{2U};
  std::uint32_t register_file_bytes_per_sm{256U * 1024U};
  std::uint32_t registers_per_thread{32U};
  std::uint32_t shared_memory_bytes_per_sm{96U * 1024U};
  std::uint32_t shared_memory_bytes_per_block{0U};
  std::uint32_t shared_memory_banks{32U};
  std::uint32_t shared_memory_bank_width_bytes{4U};
  std::uint32_t integer_latency{1U};
  std::uint32_t floating_point_latency{4U};
  std::uint32_t tensor_latency{8U};
  std::uint32_t branch_latency{1U};
  CacheConfig l1_data{128U * 1024U, 128U, 8U, 1U};
  CacheConfig l2{4U * 1024U * 1024U, 128U, 16U, 12U};
  CacheConfig last_level{16U * 1024U * 1024U, 128U, 16U, 30U};
  MemoryConfig memory{16U, 64U, 260U, 128U};
  std::uint64_t link_bandwidth_bytes_per_cycle{32U};
  std::uint32_t link_latency_cycles{180U};
};

struct Workload final {
  std::uint64_t cpu_instructions{50'000U};
  std::uint64_t gpu_threads{32'768U};
  std::uint32_t gpu_instructions_per_thread{64U};
  std::uint64_t working_set_bytes{8U * 1024U * 1024U};
  std::uint64_t host_to_device_bytes{4U * 1024U * 1024U};
  std::uint64_t device_to_host_bytes{1U * 1024U * 1024U};
  std::uint32_t memory_instruction_percent{24U};
  std::uint32_t store_percent_of_memory{25U};
  std::uint32_t floating_point_percent{30U};
  std::uint32_t branch_percent{8U};
  std::uint32_t tensor_percent{4U};
  std::uint32_t shared_memory_percent{5U};
  std::uint32_t dependency_percent{70U};
  std::uint32_t divergence_percent{15U};
  MemoryPattern memory_pattern{MemoryPattern::Coalesced};
  std::uint64_t seed{20261123ULL};
};

struct CacheStats final {
  std::uint64_t accesses{0U};
  std::uint64_t hits{0U};
  std::uint64_t misses{0U};
};

struct SimulationResult final {
  bool ok{false};
  Engine engine{Engine::Cpu};
  std::string error;
  std::uint64_t cycles{0U};
  std::uint64_t frequency_hz{0U};
  std::uint64_t instructions{0U};
  std::uint64_t memory_instructions{0U};
  std::uint64_t memory_transactions{0U};
  std::uint64_t instruction_cache_transactions{0U};
  std::uint64_t memory_bytes{0U};
  std::uint64_t dram_transactions{0U};
  std::uint64_t dram_bytes{0U};
  std::uint64_t branch_mispredictions{0U};
  std::uint64_t shared_memory_bank_conflicts{0U};
  std::uint64_t link_transfer_cycles{0U};
  std::uint64_t active_warps_peak{0U};
  std::uint64_t active_lane_slots{0U};
  std::uint64_t issued_lane_slots{0U};
  std::uint64_t stalled_cycles{0U};
  std::uint64_t dependency_stall_cycles{0U};
  std::uint64_t memory_stall_cycles{0U};
  std::uint64_t frontend_stall_cycles{0U};
  std::uint64_t effective_occupancy_percent_x100{0U};
  CacheStats l1_instruction_stats{};
  CacheStats l1_stats{};
  CacheStats l2_stats{};
  CacheStats last_level_stats{};

  [[nodiscard]] double elapsed_nanoseconds() const noexcept;
  [[nodiscard]] double ipc() const noexcept;
  [[nodiscard]] double cache_hit_rate(const CacheStats& stats) const noexcept;
  [[nodiscard]] std::string report() const;
};

struct SimulatorConfig final {
  CpuConfig cpu{};
  GpuConfig gpu{};
  Workload workload{};
  std::uint64_t maximum_cycles{100'000'000U};
  std::uint64_t maximum_dynamic_instructions{20'000'000U};
};

[[nodiscard]] SimulationResult simulate_cpu(const SimulatorConfig& config);
[[nodiscard]] SimulationResult simulate_gpu(const SimulatorConfig& config);
[[nodiscard]] SimulationResult simulate(Engine engine, const SimulatorConfig& config);
[[nodiscard]] std::string engine_name(Engine engine);
[[nodiscard]] std::string memory_pattern_name(MemoryPattern pattern);

}  // namespace masl::sim
