#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace masl::runtime {

enum class ResourceWaitPolicy { Reject, Block };
enum class ResourceAcquireStatus { Acquired, Blocked, Rejected, Invalid, IdentifierExhausted };
enum class ResourceReleaseStatus { Released, InvalidOwner, UnknownLease, InvalidAmount, OverRelease };

struct ResourceLease final {
  std::uint64_t id{0};
  std::string owner;
  std::uint64_t units{0};
};

struct ResourceAcquireResult final {
  ResourceAcquireStatus status{ResourceAcquireStatus::Invalid};
  std::optional<ResourceLease> lease;
  std::uint64_t ticket{0};
};

struct ResourceStats final {
  std::uint64_t acquire_requests{0};
  std::uint64_t acquired{0};
  std::uint64_t blocked{0};
  std::uint64_t rejected{0};
  std::uint64_t releases{0};
  std::uint64_t cancelled{0};
  std::uint64_t grants_delivered{0};
  std::uint64_t peak_in_use{0};
  std::uint64_t trace_records_dropped{0};
  bool counter_overflow{false};
};

struct ResourceTraceRecord final {
  std::string action;
  std::string owner;
  std::uint64_t lease_id{0};
  std::uint64_t ticket{0};
  std::uint64_t units{0};
  std::uint64_t available{0};
  std::size_t waiter_count{0};
};

// Capacity is accounted in units. Each successful acquisition creates an
// owner-bound lease. Blocking waiters are FIFO and grants are allocated on
// release, then retrieved by ticket; no caller needs to poll for free space.
class Resource final {
 public:
  [[nodiscard]] static std::optional<Resource> create(
      std::string name, std::uint64_t capacity,
      std::size_t maximum_waiters = 65'536U, std::string* error = nullptr,
      std::size_t maximum_trace_records = 1'000'000U);

  [[nodiscard]] ResourceAcquireResult acquire(std::string owner, std::uint64_t units,
                                               ResourceWaitPolicy policy);
  [[nodiscard]] ResourceReleaseStatus release(const std::string& owner,
                                                std::uint64_t lease_id,
                                                std::uint64_t units);
  [[nodiscard]] ResourceReleaseStatus release_all(const std::string& owner,
                                                   std::uint64_t lease_id);
  [[nodiscard]] std::optional<ResourceLease> take_grant(std::uint64_t ticket);
  [[nodiscard]] bool cancel_wait(std::uint64_t ticket);

  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] std::uint64_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint64_t available() const noexcept { return available_; }
  [[nodiscard]] std::uint64_t in_use() const noexcept { return capacity_ - available_; }
  [[nodiscard]] std::size_t waiting_count() const noexcept { return waiters_.size(); }
  [[nodiscard]] std::size_t undelivered_grants() const noexcept { return grants_.size(); }
  [[nodiscard]] const ResourceStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const std::vector<ResourceTraceRecord>& trace() const noexcept { return trace_; }
  [[nodiscard]] bool invariant_holds() const noexcept;

 private:
  struct Waiter final {
    std::uint64_t ticket{0};
    std::string owner;
    std::uint64_t units{0};
  };

  Resource(std::string name, std::uint64_t capacity, std::size_t maximum_waiters,
           std::size_t maximum_trace_records);
  void increment(std::uint64_t& counter) noexcept;
  void record(std::string action, const std::string& owner, std::uint64_t lease_id,
              std::uint64_t ticket, std::uint64_t units);
  void grant_waiters();
  [[nodiscard]] std::optional<ResourceLease> allocate_lease(const std::string& owner,
                                                             std::uint64_t units);

  std::string name_;
  std::uint64_t capacity_{0};
  std::uint64_t available_{0};
  std::size_t maximum_waiters_{0};
  std::size_t maximum_trace_records_{0};
  std::uint64_t next_lease_id_{1};
  std::uint64_t next_ticket_{1};
  std::map<std::uint64_t, ResourceLease> leases_;
  std::deque<Waiter> waiters_;
  std::map<std::uint64_t, ResourceLease> grants_;
  ResourceStats stats_{};
  std::vector<ResourceTraceRecord> trace_;
};

}  // namespace masl::runtime
