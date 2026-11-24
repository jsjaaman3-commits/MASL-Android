#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace masl::runtime {

struct RationalTime final {
  std::uint64_t numerator_fs{0};
  std::uint64_t denominator{1};

  [[nodiscard]] static RationalTime zero() noexcept { return {}; }
  [[nodiscard]] static std::optional<RationalTime> make(std::uint64_t numerator_fs,
                                                        std::uint64_t denominator) noexcept;
  [[nodiscard]] std::optional<RationalTime> add(const RationalTime& other) const noexcept;
  [[nodiscard]] std::optional<RationalTime> multiply(std::uint64_t factor) const noexcept;
  [[nodiscard]] bool less_than(const RationalTime& other) const noexcept;
  [[nodiscard]] bool equal_to(const RationalTime& other) const noexcept;
  [[nodiscard]] std::string to_string() const;
};

struct ClockDomain final {
  std::string name;
  std::uint64_t frequency_hz{0};
  RationalTime period{};
  [[nodiscard]] static std::optional<ClockDomain> create(std::string name,
                                                         std::uint64_t frequency_hz);
  [[nodiscard]] std::optional<RationalTime> edge_time(std::uint64_t edge_index) const noexcept;
};

enum class EventKind : std::uint16_t {
  User = 0, ClockEdge = 1, PipelineComplete = 2, QueueRetry = 3, Timeout = 4
};

struct Event final {
  RationalTime time{};
  std::int32_t priority{0};
  std::uint64_t sequence{0};
  std::string target;
  EventKind kind{EventKind::User};
  std::int64_t payload{0};
};

struct TraceRecord final {
  std::string action;
  Event event;
};

struct RunResult final {
  bool ok{false};
  bool limit_reached{false};
  std::uint64_t events_processed{0};
  std::vector<std::string> diagnostics;
};

class Simulation final {
 public:
  using Handler = std::function<void(const Event&, Simulation&)>;
  explicit Simulation(std::uint64_t maximum_events = 1'000'000U,
                      std::uint64_t maximum_pending_events = 1'000'000U);
  [[nodiscard]] bool add_clock(ClockDomain clock);
  [[nodiscard]] bool register_handler(std::string target, Handler handler);
  [[nodiscard]] bool schedule(RationalTime time, std::string target,
                              EventKind kind = EventKind::User,
                              std::int64_t payload = 0, std::int32_t priority = 0);
  [[nodiscard]] RunResult run_events(std::optional<RationalTime> until = std::nullopt);
  [[nodiscard]] RunResult run_cycles(std::uint64_t cycle_count);
  [[nodiscard]] const std::vector<TraceRecord>& trace() const noexcept { return trace_; }
  [[nodiscard]] RationalTime now() const noexcept { return now_; }
  [[nodiscard]] std::uint64_t pending_events() const noexcept { return queue_.size(); }
  [[nodiscard]] const std::map<std::string, ClockDomain>& clocks() const noexcept { return clocks_; }
 private:
  struct Later final {
    bool operator()(const Event& lhs, const Event& rhs) const noexcept;
  };
  std::priority_queue<Event, std::vector<Event>, Later> queue_;
  std::map<std::string, ClockDomain> clocks_;
  std::map<std::string, Handler> handlers_;
  std::vector<TraceRecord> trace_;
  RationalTime now_{};
  std::uint64_t next_sequence_{0};
  std::uint64_t maximum_events_{1'000'000U};
  std::uint64_t maximum_pending_events_{1'000'000U};
  std::uint64_t events_processed_total_{0};
};

}  // namespace masl::runtime
