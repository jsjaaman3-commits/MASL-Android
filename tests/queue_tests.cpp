#include "masl/runtime/kernel.hpp"
#include "masl/runtime/queue.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
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
  check(!BoundedQueue<int>::create(QueueConfig{"bad", 0U}, &error).has_value() &&
        error.find("positive") != std::string::npos,
        "rejects a zero-capacity queue with a diagnostic");

  auto fifo_result = BoundedQueue<int>::create(
      QueueConfig{"fifo", 3U, QueueDiscipline::Fifo, QueueOverflowPolicy::Reject});
  check(fifo_result.has_value(), "constructs a bounded FIFO queue");
  if (fifo_result.has_value()) {
    auto& fifo = *fifo_result;
    const auto first = fifo.enqueue(11);
    const auto second = fifo.enqueue(22);
    const auto third = fifo.enqueue(33);
    check(first.status == QueueEnqueueStatus::Accepted &&
          second.status == QueueEnqueueStatus::Accepted &&
          third.status == QueueEnqueueStatus::Accepted && fifo.size() == 3U,
          "accepts FIFO items up to but not beyond capacity");
    check(fifo.enqueue(44).status == QueueEnqueueStatus::Rejected,
          "reject overflow returns a deterministic rejected outcome");
    const auto a = fifo.dequeue();
    const auto b = fifo.dequeue();
    const auto c = fifo.dequeue();
    check(a.has_value() && b.has_value() && c.has_value() &&
          a->value == 11 && b->value == 22 && c->value == 33,
          "FIFO returns accepted items in insertion order");
    check(!fifo.dequeue().has_value() && fifo.stats().rejected == 1U &&
          fifo.stats().dequeued == 3U && fifo.stats().high_watermark == 3U &&
          fifo.invariant_holds(),
          "tracks FIFO counters, high-watermark and invariants");
  }

  auto priority_result = BoundedQueue<std::string>::create(
      QueueConfig{"priority", 4U, QueueDiscipline::Priority, QueueOverflowPolicy::Reject});
  check(priority_result.has_value(), "constructs a priority queue");
  if (priority_result.has_value()) {
    auto& priority = *priority_result;
    (void)priority.enqueue("low", "p0", -1);
    (void)priority.enqueue("high-first", "p1", 9);
    (void)priority.enqueue("high-second", "p2", 9);
    (void)priority.enqueue("middle", "p3", 2);
    const auto one = priority.dequeue();
    const auto two = priority.dequeue();
    const auto three = priority.dequeue();
    const auto four = priority.dequeue();
    check(one && two && three && four && one->value == "high-first" &&
          two->value == "high-second" && three->value == "middle" &&
          four->value == "low",
          "priority queue selects greatest priority with stable FIFO ties");
  }

  check(!BoundedQueue<int>::create(QueueConfig{"rr_bad", 4U,
        QueueDiscipline::RoundRobin, QueueOverflowPolicy::Reject}).has_value(),
        "rejects round-robin without at least two declared producers");
  auto rr_result = BoundedQueue<int>::create(QueueConfig{
      "rr", 8U, QueueDiscipline::RoundRobin, QueueOverflowPolicy::Reject,
      {"A", "B"}});
  check(rr_result.has_value(), "constructs round-robin queue with declared producers");
  if (rr_result.has_value()) {
    auto& rr = *rr_result;
    (void)rr.enqueue(1, "A");
    (void)rr.enqueue(2, "A");
    (void)rr.enqueue(3, "B");
    (void)rr.enqueue(4, "B");
    const auto a = rr.dequeue();
    const auto b = rr.dequeue();
    const auto c = rr.dequeue();
    const auto d = rr.dequeue();
    check(a && b && c && d && a->value == 1 && b->value == 3 &&
          c->value == 2 && d->value == 4,
          "round-robin selects declared producers cyclically while preserving per-producer FIFO");
    check(rr.enqueue(5, "undeclared").status == QueueEnqueueStatus::Invalid,
          "rejects an undeclared round-robin producer");
  }

  auto blocking_result = BoundedQueue<int>::create(QueueConfig{
      "blocking", 1U, QueueDiscipline::Fifo, QueueOverflowPolicy::Block, {}, 2U});
  check(blocking_result.has_value(), "constructs a bounded blocking queue");
  if (blocking_result.has_value()) {
    auto& blocking = *blocking_result;
    const auto first = blocking.enqueue(100, "producer-A");
    const auto waiting = blocking.enqueue(200, "producer-B");
    const auto full_wait = blocking.enqueue(300, "producer-C");
    const auto limit = blocking.enqueue(400, "producer-D");
    check(first.status == QueueEnqueueStatus::Accepted &&
          waiting.status == QueueEnqueueStatus::Blocked && waiting.ticket != 0U &&
          full_wait.status == QueueEnqueueStatus::Blocked &&
          limit.status == QueueEnqueueStatus::PendingLimitReached,
          "retains blocked producers and enforces a separate pending limit");
    check(blocking.size() == 1U && blocking.blocked_size() == 2U,
          "keeps pending producer operations outside queue capacity");
    const auto head = blocking.dequeue();
    check(head && head->value == 100 && blocking.size() == 1U &&
          blocking.blocked_size() == 1U,
          "automatically promotes the oldest blocked operation when capacity opens");
    const auto second = blocking.dequeue();
    check(second && second->value == 200 && blocking.size() == 1U &&
          blocking.blocked_size() == 0U,
          "promotes blocked operations without dropping or reordering them");
    const auto third = blocking.dequeue();
    check(third && third->value == 300 && blocking.stats().promoted == 2U &&
          blocking.stats().blocked == 2U && blocking.stats().accepted == 3U &&
          blocking.invariant_holds(),
          "tracks blocked, promoted and accepted outcomes consistently");
  }

  auto drop_result = BoundedQueue<int>::create(
      QueueConfig{"drop", 1U, QueueDiscipline::Fifo, QueueOverflowPolicy::Drop});
  if (drop_result.has_value()) {
    auto& drop = *drop_result;
    (void)drop.enqueue(1);
    check(drop.enqueue(2).status == QueueEnqueueStatus::Dropped &&
          drop.stats().dropped == 1U && drop.size() == 1U,
          "drop policy discards the new item and increments a dedicated counter");
  } else check(false, "constructs drop-policy queue");

  auto bounded_trace_result = BoundedQueue<int>::create(QueueConfig{
      "bounded_trace", 1U, QueueDiscipline::Fifo, QueueOverflowPolicy::Reject, {}, 65'536U, 2U});
  if (bounded_trace_result.has_value()) {
    auto& bounded_trace = *bounded_trace_result;
    (void)bounded_trace.enqueue(1);
    (void)bounded_trace.enqueue(2);
    (void)bounded_trace.dequeue();
    check(bounded_trace.trace().size() == 2U &&
          bounded_trace.stats().trace_records_dropped == 1U,
          "bounds trace memory and counts transitions omitted after the configured limit");
  } else check(false, "constructs queue with bounded trace storage");

  auto invalid_producer_result = BoundedQueue<int>::create(
      QueueConfig{"invalid_producer", 1U, QueueDiscipline::Fifo, QueueOverflowPolicy::Reject});
  if (invalid_producer_result.has_value()) {
    check(invalid_producer_result->enqueue(1, "").status == QueueEnqueueStatus::Invalid,
          "rejects empty producer identities");
  } else check(false, "constructs queue for producer validation");

  auto integrated_result = BoundedQueue<std::int64_t>::create(QueueConfig{
      "event_queue", 1U, QueueDiscipline::Fifo, QueueOverflowPolicy::Block, {}, 4U});
  check(integrated_result.has_value(), "constructs queue for event-kernel integration");
  if (integrated_result.has_value()) {
    auto queue = std::move(*integrated_result);
    Simulation simulation(32U, 32U);
    std::vector<std::int64_t> consumed;
    (void)simulation.register_handler("submit", [&](const Event& event, Simulation&) {
      const auto result = queue.enqueue(event.payload, "workload");
      if (event.payload == 1) check(result.status == QueueEnqueueStatus::Accepted,
                                    "event kernel submits first queue item");
      if (event.payload == 2) check(result.status == QueueEnqueueStatus::Blocked,
                                    "event kernel preserves blocked queue submission");
    });
    (void)simulation.register_handler("consume", [&](const Event&, Simulation&) {
      auto item = queue.dequeue();
      if (item.has_value()) consumed.push_back(item->value);
    });
    (void)simulation.schedule(RationalTime{1U, 1U}, "submit", EventKind::User, 1);
    (void)simulation.schedule(RationalTime{2U, 1U}, "submit", EventKind::User, 2);
    (void)simulation.schedule(RationalTime{3U, 1U}, "consume");
    (void)simulation.schedule(RationalTime{4U, 1U}, "consume");
    const auto result = simulation.run_events();
    check(result.ok && result.events_processed == 4U &&
          consumed == std::vector<std::int64_t>({1, 2}) && queue.invariant_holds(),
          "executes enqueue, backpressure, promotion and dequeue through the event kernel");
    check(queue.trace().size() >= 5U, "records actual queue state transitions in trace");
  }

  std::cout << "Queue assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
