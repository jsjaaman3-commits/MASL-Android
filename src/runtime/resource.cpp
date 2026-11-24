#include "masl/runtime/resource.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace masl::runtime {

std::optional<Resource> Resource::create(std::string resource_name,
                                         std::uint64_t capacity_value,
                                         std::size_t maximum_waiters,
                                         std::string* error,
                                         std::size_t maximum_trace_records) {
  auto fail = [&](const std::string& message) -> std::optional<Resource> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (resource_name.empty()) return fail("resource name must not be empty");
  if (capacity_value == 0U) return fail("resource capacity must be positive");
  if (maximum_waiters == 0U) return fail("maximum_waiters must be positive");
  if (maximum_trace_records == 0U) return fail("maximum_trace_records must be positive");
  if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
    if (maximum_waiters > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()))
      return fail("maximum_waiters exceeds the runtime counter range");
  }
  return Resource(std::move(resource_name), capacity_value, maximum_waiters,
                  maximum_trace_records);
}

Resource::Resource(std::string resource_name, std::uint64_t capacity_value,
                   std::size_t maximum_waiters, std::size_t maximum_trace_records)
    : name_(std::move(resource_name)), capacity_(capacity_value),
      available_(capacity_value), maximum_waiters_(maximum_waiters),
      maximum_trace_records_(maximum_trace_records) {}

void Resource::increment(std::uint64_t& counter) noexcept {
  if (counter == std::numeric_limits<std::uint64_t>::max()) stats_.counter_overflow = true;
  else ++counter;
}

void Resource::record(std::string action, const std::string& owner,
                      std::uint64_t lease_id, std::uint64_t ticket,
                      std::uint64_t units) {
  if (trace_.size() >= maximum_trace_records_) {
    increment(stats_.trace_records_dropped);
    return;
  }
  trace_.push_back(ResourceTraceRecord{std::move(action), owner, lease_id, ticket,
                                       units, available_, waiters_.size()});
}

std::optional<ResourceLease> Resource::allocate_lease(const std::string& owner,
                                                        std::uint64_t units) {
  if (owner.empty() || units == 0U || units > available_ ||
      next_lease_id_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
  const std::uint64_t id = next_lease_id_++;
  ResourceLease lease{id, owner, units};
  available_ -= units;
  leases_.emplace(id, lease);
  stats_.peak_in_use = std::max(stats_.peak_in_use, in_use());
  increment(stats_.acquired);
  return lease;
}

ResourceAcquireResult Resource::acquire(std::string owner, std::uint64_t units,
                                        ResourceWaitPolicy policy) {
  increment(stats_.acquire_requests);
  if (owner.empty() || units == 0U || units > capacity_) {
    increment(stats_.rejected);
    record("invalid-acquire", owner, 0U, 0U, units);
    return {ResourceAcquireStatus::Invalid, std::nullopt, 0U};
  }

  // Existing waiters retain FIFO precedence even if a smaller later request
  // could fit. This avoids starvation by overtaking.
  if (waiters_.empty() && units <= available_) {
    auto lease = allocate_lease(owner, units);
    if (lease.has_value()) {
      record("acquire", owner, lease->id, 0U, units);
      return {ResourceAcquireStatus::Acquired, lease, 0U};
    }
    increment(stats_.rejected);
    return {ResourceAcquireStatus::IdentifierExhausted, std::nullopt, 0U};
  }

  if (policy == ResourceWaitPolicy::Reject) {
    increment(stats_.rejected);
    record("reject", owner, 0U, 0U, units);
    return {ResourceAcquireStatus::Rejected, std::nullopt, 0U};
  }
  if (waiters_.size() >= maximum_waiters_) {
    increment(stats_.rejected);
    record("wait-limit-reject", owner, 0U, 0U, units);
    return {ResourceAcquireStatus::Rejected, std::nullopt, 0U};
  }
  if (next_ticket_ == std::numeric_limits<std::uint64_t>::max()) {
    increment(stats_.rejected);
    return {ResourceAcquireStatus::IdentifierExhausted, std::nullopt, 0U};
  }
  const std::uint64_t ticket = next_ticket_++;
  waiters_.push_back(Waiter{ticket, std::move(owner), units});
  increment(stats_.blocked);
  record("block", waiters_.back().owner, 0U, ticket, units);
  return {ResourceAcquireStatus::Blocked, std::nullopt, ticket};
}

ResourceReleaseStatus Resource::release(const std::string& owner,
                                         std::uint64_t lease_id,
                                         std::uint64_t units) {
  const auto found = leases_.find(lease_id);
  if (found == leases_.end()) return ResourceReleaseStatus::UnknownLease;
  if (owner.empty() || found->second.owner != owner) return ResourceReleaseStatus::InvalidOwner;
  if (units == 0U) return ResourceReleaseStatus::InvalidAmount;
  if (units > found->second.units) return ResourceReleaseStatus::OverRelease;

  found->second.units -= units;
  available_ += units;
  if (found->second.units == 0U) leases_.erase(found);
  increment(stats_.releases);
  record("release", owner, lease_id, 0U, units);
  grant_waiters();
  return ResourceReleaseStatus::Released;
}

ResourceReleaseStatus Resource::release_all(const std::string& owner,
                                             std::uint64_t lease_id) {
  const auto found = leases_.find(lease_id);
  if (found == leases_.end()) return ResourceReleaseStatus::UnknownLease;
  return release(owner, lease_id, found->second.units);
}

void Resource::grant_waiters() {
  while (!waiters_.empty() && waiters_.front().units <= available_) {
    const Waiter waiter = waiters_.front();
    auto lease = allocate_lease(waiter.owner, waiter.units);
    if (!lease.has_value()) break;
    waiters_.pop_front();
    grants_.emplace(waiter.ticket, *lease);
    record("grant", waiter.owner, lease->id, waiter.ticket, waiter.units);
  }
}

std::optional<ResourceLease> Resource::take_grant(std::uint64_t ticket) {
  const auto found = grants_.find(ticket);
  if (found == grants_.end()) return std::nullopt;
  ResourceLease lease = found->second;
  grants_.erase(found);
  increment(stats_.grants_delivered);
  record("grant-delivered", lease.owner, lease.id, ticket, lease.units);
  return lease;
}

bool Resource::cancel_wait(std::uint64_t ticket) {
  const auto waiting = std::find_if(waiters_.begin(), waiters_.end(),
      [ticket](const Waiter& waiter) { return waiter.ticket == ticket; });
  if (waiting != waiters_.end()) {
    const std::string owner = waiting->owner;
    const std::uint64_t units = waiting->units;
    waiters_.erase(waiting);
    increment(stats_.cancelled);
    record("cancel-wait", owner, 0U, ticket, units);
    grant_waiters();
    return true;
  }

  const auto granted = grants_.find(ticket);
  if (granted == grants_.end()) return false;
  const ResourceLease lease = granted->second;
  const auto allocated = leases_.find(lease.id);
  if (allocated == leases_.end() || allocated->second.owner != lease.owner ||
      allocated->second.units != lease.units) return false;
  grants_.erase(granted);
  available_ += lease.units;
  leases_.erase(allocated);
  increment(stats_.cancelled);
  record("cancel-grant", lease.owner, lease.id, ticket, lease.units);
  grant_waiters();
  return true;
}

bool Resource::invariant_holds() const noexcept {
  if (name_.empty() || capacity_ == 0U || available_ > capacity_ || maximum_waiters_ == 0U ||
      maximum_trace_records_ == 0U || waiters_.size() > maximum_waiters_ ||
      trace_.size() > maximum_trace_records_) return false;
  std::uint64_t leased = 0U;
  for (const auto& [id, lease] : leases_) {
    if (id == 0U || id != lease.id || lease.owner.empty() || lease.units == 0U ||
        lease.units > capacity_ - leased) return false;
    leased += lease.units;
  }
  if (leased != capacity_ - available_) return false;
  for (const auto& waiter : waiters_) {
    if (waiter.ticket == 0U || waiter.owner.empty() || waiter.units == 0U ||
        waiter.units > capacity_) return false;
  }
  for (const auto& [ticket, lease] : grants_) {
    if (ticket == 0U || lease.id == 0U || lease.owner.empty() || lease.units == 0U) return false;
    const auto found = leases_.find(lease.id);
    if (found == leases_.end() || found->second.owner != lease.owner ||
        found->second.units != lease.units) return false;
  }
  return true;
}

}  // namespace masl::runtime
