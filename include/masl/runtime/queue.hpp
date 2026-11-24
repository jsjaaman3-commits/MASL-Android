#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace masl::runtime {

enum class QueueDiscipline { Fifo, Priority, RoundRobin };
enum class QueueOverflowPolicy { Block, Reject, Drop };
enum class QueueEnqueueStatus {
  Accepted,
  Blocked,
  Rejected,
  Dropped,
  Invalid,
  PendingLimitReached,
  IdentifierExhausted
};

struct QueueConfig final {
  std::string name;
  std::size_t capacity{0};
  QueueDiscipline discipline{QueueDiscipline::Fifo};
  QueueOverflowPolicy overflow{QueueOverflowPolicy::Reject};
  std::vector<std::string> producers;
  std::size_t maximum_blocked{65'536U};
  std::size_t maximum_trace_records{1'000'000U};

  QueueConfig() = default;
  QueueConfig(std::string queue_name, std::size_t queue_capacity,
              QueueDiscipline queue_discipline = QueueDiscipline::Fifo,
              QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::Reject,
              std::vector<std::string> declared_producers = {},
              std::size_t blocked_limit = 65'536U,
              std::size_t trace_limit = 1'000'000U)
      : name(std::move(queue_name)), capacity(queue_capacity),
        discipline(queue_discipline), overflow(overflow_policy),
        producers(std::move(declared_producers)), maximum_blocked(blocked_limit),
        maximum_trace_records(trace_limit) {}
};

struct QueueStats final {
  std::uint64_t submitted{0};
  std::uint64_t accepted{0};
  std::uint64_t blocked{0};
  std::uint64_t rejected{0};
  std::uint64_t dropped{0};
  std::uint64_t dequeued{0};
  std::uint64_t promoted{0};
  std::size_t high_watermark{0};
  std::uint64_t trace_records_dropped{0};
  bool counter_overflow{false};
};

struct QueueTraceRecord final {
  std::string action;
  std::uint64_t sequence{0};
  std::uint64_t ticket{0};
  std::string producer;
  std::size_t occupancy{0};
  std::size_t blocked_count{0};
};

template <typename T>
struct QueueItem final {
  T value;
  std::string producer;
  std::int64_t priority{0};
  std::uint64_t sequence{0};
};

struct QueueEnqueueResult final {
  QueueEnqueueStatus status{QueueEnqueueStatus::Invalid};
  std::uint64_t ticket{0};
  [[nodiscard]] bool accepted() const noexcept {
    return status == QueueEnqueueStatus::Accepted;
  }
};

// A deterministic, bounded, simulation-thread-owned queue. Blocked producers
// are retained in a separately bounded FIFO and promoted automatically when
// a dequeue creates capacity. The class is intentionally not internally
// synchronized: one Simulation owns and mutates a model's runtime state.
template <typename T>
class BoundedQueue final {
 public:
  [[nodiscard]] static std::optional<BoundedQueue> create(
      QueueConfig config, std::string* error = nullptr) {
    auto fail = [&](const std::string& message) -> std::optional<BoundedQueue> {
      if (error != nullptr) *error = message;
      return std::nullopt;
    };
    if (config.name.empty()) return fail("queue name must not be empty");
    if (config.capacity == 0U) return fail("queue capacity must be positive");
    if (config.maximum_trace_records == 0U)
      return fail("maximum_trace_records must be positive");
    if (config.capacity > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()))
      return fail("queue capacity exceeds the runtime counter range");
    if (config.overflow == QueueOverflowPolicy::Block && config.maximum_blocked == 0U)
      return fail("blocking queue requires a positive maximum_blocked limit");
    if (config.discipline == QueueDiscipline::RoundRobin) {
      if (config.producers.size() < 2U)
        return fail("round-robin queue requires at least two declared producers");
      for (std::size_t i = 0; i < config.producers.size(); ++i) {
        if (config.producers[i].empty()) return fail("producer names must not be empty");
        if (std::find(config.producers.begin(), config.producers.begin() +
                      static_cast<std::ptrdiff_t>(i), config.producers[i]) !=
            config.producers.begin() + static_cast<std::ptrdiff_t>(i))
          return fail("round-robin producer names must be unique");
      }
    }
    return BoundedQueue(std::move(config));
  }

  [[nodiscard]] QueueEnqueueResult enqueue(T value, std::string producer = "default",
                                            std::int64_t priority = 0) {
    increment(stats_.submitted);
    if (producer.empty()) {
      increment(stats_.rejected);
      return {QueueEnqueueStatus::Invalid, 0U};
    }
    if (config_.discipline == QueueDiscipline::RoundRobin &&
        std::find(config_.producers.begin(), config_.producers.end(), producer) ==
            config_.producers.end()) {
      increment(stats_.rejected);
      return {QueueEnqueueStatus::Invalid, 0U};
    }
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
      increment(stats_.rejected);
      return {QueueEnqueueStatus::IdentifierExhausted, 0U};
    }

    QueueItem<T> item{std::move(value), std::move(producer), priority, next_sequence_++};
    if (items_.size() < config_.capacity && blocked_.empty()) {
      const auto sequence = item.sequence;
      const auto producer_name = item.producer;
      items_.push_back(std::move(item));
      increment(stats_.accepted);
      update_high_watermark();
      record("enqueue", sequence, 0U, producer_name);
      return {QueueEnqueueStatus::Accepted, 0U};
    }

    if (config_.overflow == QueueOverflowPolicy::Reject) {
      increment(stats_.rejected);
      record("reject", item.sequence, 0U, item.producer);
      return {QueueEnqueueStatus::Rejected, 0U};
    }
    if (config_.overflow == QueueOverflowPolicy::Drop) {
      increment(stats_.dropped);
      record("drop", item.sequence, 0U, item.producer);
      return {QueueEnqueueStatus::Dropped, 0U};
    }
    if (blocked_.size() >= config_.maximum_blocked) {
      increment(stats_.rejected);
      record("blocked-limit-reject", item.sequence, 0U, item.producer);
      return {QueueEnqueueStatus::PendingLimitReached, 0U};
    }
    if (next_ticket_ == std::numeric_limits<std::uint64_t>::max()) {
      increment(stats_.rejected);
      return {QueueEnqueueStatus::IdentifierExhausted, 0U};
    }
    const std::uint64_t ticket = next_ticket_++;
    const auto sequence = item.sequence;
    const auto producer_name = item.producer;
    blocked_.push_back(PendingItem{ticket, std::move(item)});
    increment(stats_.blocked);
    record("block", sequence, ticket, producer_name);
    return {QueueEnqueueStatus::Blocked, ticket};
  }

  [[nodiscard]] std::optional<QueueItem<T>> dequeue() {
    if (items_.empty()) return std::nullopt;
    const std::size_t index = selected_index();
    auto iterator = items_.begin() + static_cast<std::ptrdiff_t>(index);
    QueueItem<T> result = std::move(*iterator);
    items_.erase(iterator);
    increment(stats_.dequeued);
    record("dequeue", result.sequence, 0U, result.producer);
    promote_blocked();
    return result;
  }

  [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return config_.capacity; }
  [[nodiscard]] std::size_t blocked_size() const noexcept { return blocked_.size(); }
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
  [[nodiscard]] bool full() const noexcept { return items_.size() >= config_.capacity; }
  [[nodiscard]] const QueueConfig& config() const noexcept { return config_; }
  [[nodiscard]] const QueueStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const std::vector<QueueTraceRecord>& trace() const noexcept { return trace_; }

  [[nodiscard]] bool invariant_holds() const noexcept {
    if (config_.capacity == 0U || items_.size() > config_.capacity) return false;
    if (blocked_.size() > config_.maximum_blocked) return false;
    if (config_.overflow != QueueOverflowPolicy::Block && !blocked_.empty()) return false;
    if (config_.discipline == QueueDiscipline::RoundRobin &&
        (config_.producers.size() < 2U || round_robin_cursor_ >= config_.producers.size()))
      return false;
    return true;
  }

 private:
  struct PendingItem final {
    std::uint64_t ticket{0};
    QueueItem<T> item;
  };

  explicit BoundedQueue(QueueConfig config) : config_(std::move(config)) {}

  void increment(std::uint64_t& counter) noexcept {
    if (counter == std::numeric_limits<std::uint64_t>::max()) stats_.counter_overflow = true;
    else ++counter;
  }

  void update_high_watermark() noexcept {
    stats_.high_watermark = std::max(stats_.high_watermark, items_.size());
  }

  void record(std::string action, std::uint64_t sequence, std::uint64_t ticket,
              const std::string& producer) {
    if (trace_.size() >= config_.maximum_trace_records) {
      increment(stats_.trace_records_dropped);
      return;
    }
    trace_.push_back(QueueTraceRecord{std::move(action), sequence, ticket, producer,
                                      items_.size(), blocked_.size()});
  }

  [[nodiscard]] std::size_t selected_index() {
    if (config_.discipline == QueueDiscipline::Fifo) return 0U;
    if (config_.discipline == QueueDiscipline::Priority) {
      std::size_t best = 0U;
      for (std::size_t i = 1U; i < items_.size(); ++i) {
        if (items_[i].priority > items_[best].priority ||
            (items_[i].priority == items_[best].priority &&
             items_[i].sequence < items_[best].sequence)) best = i;
      }
      return best;
    }

    for (std::size_t offset = 0U; offset < config_.producers.size(); ++offset) {
      const std::size_t producer_index = (round_robin_cursor_ + offset) % config_.producers.size();
      const std::string& producer_name = config_.producers[producer_index];
      std::optional<std::size_t> best;
      for (std::size_t i = 0U; i < items_.size(); ++i) {
        if (items_[i].producer == producer_name &&
            (!best.has_value() || items_[i].sequence < items_[*best].sequence)) best = i;
      }
      if (best.has_value()) {
        round_robin_cursor_ = (producer_index + 1U) % config_.producers.size();
        return *best;
      }
    }
    return 0U;
  }

  void promote_blocked() {
    while (items_.size() < config_.capacity && !blocked_.empty()) {
      PendingItem pending = std::move(blocked_.front());
      blocked_.pop_front();
      const auto sequence = pending.item.sequence;
      const auto ticket = pending.ticket;
      const auto producer_name = pending.item.producer;
      items_.push_back(std::move(pending.item));
      increment(stats_.accepted);
      increment(stats_.promoted);
      update_high_watermark();
      record("promote", sequence, ticket, producer_name);
    }
  }

  QueueConfig config_;
  std::deque<QueueItem<T>> items_;
  std::deque<PendingItem> blocked_;
  std::vector<QueueTraceRecord> trace_;
  QueueStats stats_{};
  std::uint64_t next_sequence_{0};
  std::uint64_t next_ticket_{1};
  std::size_t round_robin_cursor_{0};
};

}  // namespace masl::runtime
