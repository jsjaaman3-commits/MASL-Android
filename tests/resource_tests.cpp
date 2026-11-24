#include "masl/runtime/kernel.hpp"
#include "masl/runtime/resource.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
int failures = 0;
int assertions = 0;
void check(bool condition, std::string_view message) {
  ++assertions;
  if (condition) std::cout << "[PASS] " << message << '\n';
  else { std::cerr << "[FAIL] " << message << '\n'; ++failures; }
}
}

int main() {
  using namespace masl::runtime;
  std::string error;
  check(!Resource::create("bad", 0U, 4U, &error).has_value() &&
        error.find("positive") != std::string::npos,
        "rejects zero resource capacity with a diagnostic");
  check(!Resource::create("bad", 4U, 0U, &error).has_value(),
        "rejects a zero waiter limit");
  check(!Resource::create("bad", 4U, 2U, &error, 0U).has_value(),
        "rejects a zero trace-memory limit");

  auto created = Resource::create("alu_slots", 5U, 4U);
  check(created.has_value(), "constructs a finite-capacity resource");
  if (created.has_value()) {
    auto& resource = *created;
    auto a = resource.acquire("owner-A", 3U, ResourceWaitPolicy::Reject);
    auto b = resource.acquire("owner-B", 2U, ResourceWaitPolicy::Reject);
    check(a.status == ResourceAcquireStatus::Acquired && a.lease.has_value() &&
          b.status == ResourceAcquireStatus::Acquired && b.lease.has_value() &&
          resource.available() == 0U && resource.invariant_holds(),
          "allocates multiple owner-bound leases up to exact capacity");
    check(resource.acquire("owner-C", 1U, ResourceWaitPolicy::Reject).status ==
          ResourceAcquireStatus::Rejected,
          "explicit reject policy refuses acquisition when capacity is exhausted");
    check(resource.release("intruder", a.lease->id, 1U) == ResourceReleaseStatus::InvalidOwner,
          "prevents another owner from releasing a lease");
    check(resource.release("owner-A", a.lease->id, 0U) == ResourceReleaseStatus::InvalidAmount,
          "rejects zero-unit release");
    check(resource.release("owner-A", a.lease->id, 4U) == ResourceReleaseStatus::OverRelease,
          "rejects release beyond the lease's acquired units");
    check(resource.release("owner-A", a.lease->id, 1U) == ResourceReleaseStatus::Released &&
          resource.available() == 1U && resource.invariant_holds(),
          "supports partial release while preserving lease ownership");
    check(resource.release_all("owner-A", a.lease->id) == ResourceReleaseStatus::Released &&
          resource.available() == 3U,
          "releases the remaining amount from a partially released lease");
    check(resource.release_all("owner-A", a.lease->id) == ResourceReleaseStatus::UnknownLease,
          "detects double release after a lease is fully released");
    check(resource.release_all("owner-B", b.lease->id) == ResourceReleaseStatus::Released &&
          resource.available() == resource.capacity() && resource.invariant_holds(),
          "restores full capacity after all leases are released");
  }

  auto blocking_created = Resource::create("register_file", 5U, 8U);
  check(blocking_created.has_value(), "constructs resource for FIFO contention tests");
  if (blocking_created.has_value()) {
    auto& resource = *blocking_created;
    auto holder = resource.acquire("holder", 5U, ResourceWaitPolicy::Reject);
    const auto large = resource.acquire("large-waiter", 4U, ResourceWaitPolicy::Block);
    const auto small = resource.acquire("small-waiter", 1U, ResourceWaitPolicy::Block);
    check(holder.lease && large.status == ResourceAcquireStatus::Blocked &&
          small.status == ResourceAcquireStatus::Blocked && large.ticket < small.ticket &&
          resource.waiting_count() == 2U,
          "queues contending acquisitions as bounded FIFO waiters");
    check(resource.release("holder", holder.lease->id, 1U) == ResourceReleaseStatus::Released &&
          resource.available() == 1U && resource.waiting_count() == 2U &&
          !resource.take_grant(large.ticket).has_value() &&
          !resource.take_grant(small.ticket).has_value(),
          "does not let a smaller later request bypass a larger FIFO head waiter");
    check(resource.release("holder", holder.lease->id, 3U) == ResourceReleaseStatus::Released &&
          resource.available() == 0U && resource.waiting_count() == 1U,
          "grants the head waiter when its complete requested capacity becomes available");
    const auto large_lease = resource.take_grant(large.ticket);
    check(large_lease.has_value() && large_lease->owner == "large-waiter" &&
          large_lease->units == 4U && !resource.take_grant(large.ticket).has_value(),
          "delivers a granted lease exactly once by waiter ticket");
    check(resource.available() == 0U && !resource.take_grant(small.ticket).has_value(),
          "keeps later waiters pending while the granted head lease occupies capacity");
    check(resource.release_all("large-waiter", large_lease->id) == ResourceReleaseStatus::Released,
          "releasing a granted lease services the next FIFO waiter");
    const auto small_lease = resource.take_grant(small.ticket);
    check(small_lease.has_value() && small_lease->owner == "small-waiter" &&
          resource.invariant_holds(),
          "makes the later waiter eligible after the head waiter completes");
  }

  auto cancellation_created = Resource::create("cancel_test", 2U, 2U);
  check(cancellation_created.has_value(), "constructs resource for cancellation tests");
  if (cancellation_created.has_value()) {
    auto& resource = *cancellation_created;
    auto holder = resource.acquire("holder", 2U, ResourceWaitPolicy::Reject);
    const auto waiting = resource.acquire("waiting", 2U, ResourceWaitPolicy::Block);
    check(resource.cancel_wait(waiting.ticket) && resource.waiting_count() == 0U &&
          !resource.cancel_wait(waiting.ticket),
          "cancels a pending waiter once and rejects repeated cancellation");
    const auto granted_wait = resource.acquire("granted", 2U, ResourceWaitPolicy::Block);
    (void)resource.release_all("holder", holder.lease->id);
    check(resource.undelivered_grants() == 1U && resource.cancel_wait(granted_wait.ticket) &&
          resource.available() == 2U && resource.undelivered_grants() == 0U,
          "cancels an undelivered grant and returns its allocated capacity safely");
    check(resource.stats().cancelled == 2U && resource.invariant_holds(),
          "tracks pending and granted cancellation without leaking resource units");
  }

  auto limit_created = Resource::create("wait_limit", 1U, 1U);
  if (limit_created.has_value()) {
    auto& resource = *limit_created;
    auto holder = resource.acquire("holder", 1U, ResourceWaitPolicy::Reject);
    const auto one = resource.acquire("one", 1U, ResourceWaitPolicy::Block);
    const auto two = resource.acquire("two", 1U, ResourceWaitPolicy::Block);
    check(one.status == ResourceAcquireStatus::Blocked &&
          two.status == ResourceAcquireStatus::Rejected && resource.waiting_count() == 1U,
          "enforces a hard bound on waiting resource acquisitions");
    (void)resource.release_all("holder", holder.lease->id);
    check(resource.take_grant(one.ticket).has_value(),
          "continues to service the waiter that fit within the configured limit");
  } else check(false, "constructs resource for waiter-limit tests");

  auto trace_created = Resource::create("bounded_trace_resource", 1U, 4U, &error, 2U);
  if (trace_created.has_value()) {
    auto& resource = *trace_created;
    auto lease = resource.acquire("owner", 1U, ResourceWaitPolicy::Reject);
    (void)resource.release_all("owner", lease.lease->id);
    (void)resource.acquire("owner-2", 1U, ResourceWaitPolicy::Reject);
    check(resource.trace().size() == 2U &&
          resource.stats().trace_records_dropped == 1U && resource.invariant_holds(),
          "bounds resource trace memory and counts omitted transition records");
  } else check(false, "constructs resource with bounded trace storage");

  auto event_created = Resource::create("event_resource", 1U, 4U);
  check(event_created.has_value(), "constructs resource for event-kernel integration");
  if (event_created.has_value()) {
    auto& resource = *event_created;
    auto holder = resource.acquire("pipeline-A", 1U, ResourceWaitPolicy::Reject);
    const auto request = resource.acquire("pipeline-B", 1U, ResourceWaitPolicy::Block);
    std::vector<std::uint64_t> received;
    Simulation simulation(16U, 16U);
    (void)simulation.register_handler("release", [&](const Event&, Simulation&) {
      const auto status = resource.release_all("pipeline-A", holder.lease->id);
      check(status == ResourceReleaseStatus::Released,
            "simulation event releases a resource lease");
    });
    (void)simulation.register_handler("resume", [&](const Event&, Simulation&) {
      const auto lease = resource.take_grant(request.ticket);
      if (lease) received.push_back(lease->id);
    });
    (void)simulation.schedule(RationalTime{10U, 1U}, "release", EventKind::User);
    (void)simulation.schedule(RationalTime{11U, 1U}, "resume", EventKind::QueueRetry);
    const auto run = simulation.run_events();
    check(run.ok && run.events_processed == 2U && received.size() == 1U &&
          resource.invariant_holds(),
          "resumes a blocked resource consumer through deterministic kernel events");
  }

  std::cout << "Resource assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
