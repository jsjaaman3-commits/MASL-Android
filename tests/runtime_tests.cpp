#include "masl/runtime/kernel.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
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
  const auto third = RationalTime::make(1U, 3U);
  const auto two_thirds = RationalTime::make(2U, 3U);
  check(third.has_value() && third->denominator == 3U, "normalizes exact rational simulation time");
  check(third.has_value() && two_thirds.has_value() && third->less_than(*two_thirds),
        "compares rational times exactly without floating-point conversion");
  bool rational_order_ok = true;
  for (std::uint64_t an = 0U; an <= 20U; ++an) {
    for (std::uint64_t ad = 1U; ad <= 20U; ++ad) {
      for (std::uint64_t bn = 0U; bn <= 20U; ++bn) {
        for (std::uint64_t bd = 1U; bd <= 20U; ++bd) {
          const RationalTime left{an, ad};
          const RationalTime right{bn, bd};
          const bool expected = an * bd < bn * ad;
          if (left.less_than(right) != expected) rational_order_ok = false;
        }
      }
    }
  }
  check(rational_order_ok, "matches exact cross-product ordering for 176,400 small rational pairs");
  check(RationalTime{UINT64_MAX, UINT64_MAX - 1U}.less_than(RationalTime{UINT64_MAX - 1U, UINT64_MAX}) == false,
        "compares very large rational values without overflowing cross-products");
  if (third.has_value()) {
    const auto sum = third->add(*third);
    check(sum.has_value() && sum->equal_to(*two_thirds), "adds rational times with denominator reduction");
    const auto tripled = third->multiply(3U);
    check(tripled.has_value() && tripled->equal_to(RationalTime{1U, 1U}),
          "multiplies rational time and normalizes the result");
  }
  check(!RationalTime::make(1U, 0U).has_value(), "rejects a zero rational denominator");
  check(!RationalTime{UINT64_MAX, 1U}.add(RationalTime{1U, 1U}).has_value(),
        "detects simulation-time addition overflow");

  auto clock = ClockDomain::create("core", 3'000'000'000ULL);
  check(clock.has_value() && clock->period.numerator_fs == 1'000'000U && clock->period.denominator == 3U,
        "derives an exact fractional-femtosecond period for a 3 GHz clock");
  check(clock.has_value() && clock->edge_time(3U)->equal_to(RationalTime{1'000'000U, 1U}),
        "computes exact clock edge times without cumulative drift");
  check(!ClockDomain::create("bad", 0U).has_value(), "rejects zero-frequency clock domains");

  Simulation simulation(100U);
  check(simulation.add_clock(*ClockDomain::create("core", 1'000'000'000ULL)), "registers a clock domain");
  check(!simulation.add_clock(*ClockDomain::create("core", 2'000'000'000ULL)), "rejects duplicate clock names");
  std::vector<std::int64_t> order;
  check(simulation.register_handler("sink", [&](const Event& event, Simulation& sim) {
    order.push_back(event.payload);
    if (event.payload == 1) (void)sim.schedule(RationalTime{2'000'000U, 1U}, "sink", EventKind::User, 4, 0);
  }), "registers deterministic event handler");
  check(simulation.schedule(RationalTime{1'000'000U, 1U}, "sink", EventKind::User, 1, 5),
        "schedules first event");
  check(simulation.schedule(RationalTime{1'000'000U, 1U}, "sink", EventKind::User, 2, 0),
        "schedules same-time higher-priority event");
  check(simulation.schedule(RationalTime{1'000'000U, 1U}, "sink", EventKind::User, 3, 0),
        "schedules same-priority event with later sequence number");
  auto result = simulation.run_events();
  check(result.ok && result.events_processed == 4U, "dispatches events and accepts handler-scheduled work");
  check(order == std::vector<std::int64_t>({2, 3, 1, 4}),
        "orders events by time, priority, then insertion sequence");
  check(simulation.trace().size() == 8U, "records schedule and dispatch trace entries");

  Simulation cycles(100U);
  (void)cycles.add_clock(*ClockDomain::create("core", 1'000'000'000ULL));
  std::uint64_t edges = 0;
  (void)cycles.register_handler("core", [&](const Event& event, Simulation&) {
    if (event.kind == EventKind::ClockEdge) ++edges;
  });
  auto cycle_result = cycles.run_cycles(8U);
  check(cycle_result.ok && cycle_result.events_processed == 8U && edges == 8U,
        "runs deterministic cycle mode over each clock edge");
  check(cycles.now().equal_to(RationalTime{8'000'000U, 1U}), "advances simulation time to the eighth clock edge");
  auto continued = cycles.run_cycles(2U);
  check(continued.ok && edges == 10U && cycles.now().equal_to(RationalTime{10'000'000U, 1U}),
        "continues cycle mode from the next edge without moving simulation time backwards");

  Simulation limited(2U, 8U);
  (void)limited.register_handler("sink", [](const Event&, Simulation&) {});
  (void)limited.schedule(RationalTime{1U, 1U}, "sink");
  (void)limited.schedule(RationalTime{2U, 1U}, "sink");
  (void)limited.schedule(RationalTime{3U, 1U}, "sink");
  auto limited_result = limited.run_events();
  check(!limited_result.ok && limited_result.limit_reached && limited_result.events_processed == 2U,
        "stops at the configured event-processing budget");
  Simulation pending_limited(10U, 1U);
  (void)pending_limited.register_handler("sink", [](const Event&, Simulation&) {});
  (void)pending_limited.schedule(RationalTime{1U, 1U}, "sink");
  check(!pending_limited.schedule(RationalTime{2U, 1U}, "sink"),
        "enforces an independent pending-event queue capacity");
  Simulation no_clock;
  const auto no_clock_result = no_clock.run_cycles(1U);
  check(!no_clock_result.ok, "rejects cycle mode when no clock domain exists");
  const auto zero_cycle_result = no_clock.run_cycles(0U);
  check(zero_cycle_result.ok && zero_cycle_result.events_processed == 0U,
        "treats zero cycles as a no-op");
  Simulation missing_handler;
  (void)missing_handler.schedule(RationalTime{1U, 1U}, "missing");
  check(!missing_handler.run_events().ok, "reports events with an unregistered target handler");
  Simulation handler_exception;
  (void)handler_exception.register_handler("throws", [](const Event&, Simulation&) { throw std::runtime_error("intentional test exception"); });
  (void)handler_exception.schedule(RationalTime{1U, 1U}, "throws");
  check(!handler_exception.run_events().ok, "converts handler exceptions into deterministic simulation diagnostics");

  std::cout << "Runtime assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
