#include "masl/runtime/kernel.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace masl::runtime {
namespace {

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& result) noexcept {
  if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
  result = a + b; return true;
}
bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& result) noexcept {
  if (a != 0U && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
  result = a * b; return true;
}

// Exact comparison without cross-product overflow (continued-fraction ordering).
bool fraction_less(std::uint64_t an, std::uint64_t ad,
                   std::uint64_t bn, std::uint64_t bd) noexcept {
  bool inverted = false;
  for (;;) {
    const std::uint64_t aq = an / ad;
    const std::uint64_t bq = bn / bd;
    if (aq != bq) return inverted ? aq > bq : aq < bq;
    const std::uint64_t ar = an % ad;
    const std::uint64_t br = bn % bd;
    if (ar == 0U || br == 0U) {
      if (ar == 0U && br == 0U) return false;
      const bool ordinary = ar == 0U;
      return inverted ? !ordinary : ordinary;
    }
    an = ad; ad = ar; bn = bd; bd = br;
    inverted = !inverted;
  }
}

}  // namespace

std::optional<RationalTime> RationalTime::make(std::uint64_t numerator,
                                               std::uint64_t denominator_value) noexcept {
  if (denominator_value == 0U) return std::nullopt;
  const auto divisor = std::gcd(numerator, denominator_value);
  return RationalTime{numerator / divisor, denominator_value / divisor};
}

std::optional<RationalTime> RationalTime::add(const RationalTime& other) const noexcept {
  if (denominator == 0U || other.denominator == 0U) return std::nullopt;
  const auto common = std::gcd(denominator, other.denominator);
  const auto left_factor = other.denominator / common;
  const auto right_factor = denominator / common;
  std::uint64_t left = 0, right = 0, numerator = 0, den = 0;
  if (!checked_mul(numerator_fs, left_factor, left) ||
      !checked_mul(other.numerator_fs, right_factor, right) ||
      !checked_add(left, right, numerator) ||
      !checked_mul(denominator, left_factor, den)) return std::nullopt;
  return make(numerator, den);
}

std::optional<RationalTime> RationalTime::multiply(std::uint64_t factor) const noexcept {
  if (denominator == 0U) return std::nullopt;
  const auto common = std::gcd(factor, denominator);
  std::uint64_t numerator = 0;
  if (!checked_mul(numerator_fs, factor / common, numerator)) return std::nullopt;
  return make(numerator, denominator / common);
}

bool RationalTime::less_than(const RationalTime& other) const noexcept {
  if (denominator == 0U || other.denominator == 0U) return false;
  return fraction_less(numerator_fs, denominator, other.numerator_fs, other.denominator);
}

bool RationalTime::equal_to(const RationalTime& other) const noexcept {
  if (denominator == 0U || other.denominator == 0U) return false;
  return !less_than(other) && !other.less_than(*this);
}

std::string RationalTime::to_string() const {
  std::ostringstream output;
  output << numerator_fs;
  if (denominator != 1U) output << '/' << denominator;
  output << " fs";
  return output.str();
}

std::optional<ClockDomain> ClockDomain::create(std::string clock_name,
                                               std::uint64_t frequency) {
  if (clock_name.empty() || frequency == 0U) return std::nullopt;
  auto period_value = RationalTime::make(1'000'000'000'000'000ULL, frequency);
  if (!period_value.has_value()) return std::nullopt;
  return ClockDomain{std::move(clock_name), frequency, *period_value};
}

std::optional<RationalTime> ClockDomain::edge_time(std::uint64_t edge_index) const noexcept {
  return period.multiply(edge_index);
}

Simulation::Simulation(std::uint64_t maximum_events, std::uint64_t maximum_pending_events)
    : maximum_events_(maximum_events == 0U ? 1U : maximum_events),
      maximum_pending_events_(maximum_pending_events == 0U ? 1U : maximum_pending_events) {}

bool Simulation::add_clock(ClockDomain clock) {
  if (clock.name.empty() || clock.frequency_hz == 0U || clock.period.denominator == 0U ||
      clocks_.contains(clock.name)) return false;
  clocks_.emplace(clock.name, std::move(clock));
  return true;
}

bool Simulation::register_handler(std::string target, Handler handler) {
  if (target.empty() || !handler || handlers_.contains(target)) return false;
  handlers_.emplace(std::move(target), std::move(handler));
  return true;
}

bool Simulation::schedule(RationalTime time, std::string target,
                          EventKind kind, std::int64_t payload, std::int32_t priority) {
  if (time.denominator == 0U || target.empty() || time.less_than(now_) ||
      queue_.size() >= maximum_pending_events_ ||
      next_sequence_ == std::numeric_limits<std::uint64_t>::max()) return false;
  Event event{time, priority, next_sequence_++, std::move(target), kind, payload};
  trace_.push_back(TraceRecord{"schedule", event});
  queue_.push(std::move(event));
  return true;
}

bool Simulation::Later::operator()(const Event& lhs, const Event& rhs) const noexcept {
  if (lhs.time.less_than(rhs.time)) return false;
  if (rhs.time.less_than(lhs.time)) return true;
  if (lhs.priority != rhs.priority) return lhs.priority > rhs.priority;
  return lhs.sequence > rhs.sequence;
}

RunResult Simulation::run_events(std::optional<RationalTime> until) {
  RunResult result;
  while (!queue_.empty()) {
    const Event next = queue_.top();
    if (until.has_value() && until->less_than(next.time)) break;
    if (events_processed_total_ >= maximum_events_) {
      result.limit_reached = true;
      result.diagnostics.push_back("maximum event-processing limit reached");
      result.ok = false;
      return result;
    }
    queue_.pop();
    if (next.time.less_than(now_)) {
      result.diagnostics.push_back("event calendar ordering invariant violated");
      result.ok = false;
      return result;
    }
    now_ = next.time;
    trace_.push_back(TraceRecord{"dispatch", next});
    ++result.events_processed;
    ++events_processed_total_;
    const auto handler = handlers_.find(next.target);
    if (handler != handlers_.end()) {
      try {
        handler->second(next, *this);
      } catch (const std::exception& error) {
        result.diagnostics.push_back("event handler '" + next.target + "' failed: " + error.what());
        result.ok = false;
        return result;
      } catch (...) {
        result.diagnostics.push_back("event handler '" + next.target + "' failed with an unknown exception");
        result.ok = false;
        return result;
      }
    } else if (next.kind != EventKind::ClockEdge) {
      result.diagnostics.push_back("no handler registered for event target '" + next.target + "'");
      result.ok = false;
      return result;
    }
  }
  result.ok = true;
  return result;
}

RunResult Simulation::run_cycles(std::uint64_t cycle_count) {
  RunResult failure;
  failure.ok = false;
  if (cycle_count == 0U) { failure.ok = true; return failure; }
  if (clocks_.empty()) {
    failure.diagnostics.push_back("cycle mode requires at least one registered clock domain");
    return failure;
  }

  struct ScheduledEdge final { RationalTime time; std::string name; std::uint64_t index; };
  std::vector<ScheduledEdge> planned;
  auto first_edge_after_now = [&](const ClockDomain& clock) -> std::optional<std::uint64_t> {
    if (RationalTime::zero().equal_to(now_)) return 1U;
    std::uint64_t low = 0U;
    std::uint64_t high = 1U;
    for (;;) {
      const auto time = clock.edge_time(high);
      if (!time.has_value()) return std::nullopt;
      if (now_.less_than(*time)) break;
      low = high;
      if (high > std::numeric_limits<std::uint64_t>::max() / 2U) return std::nullopt;
      high *= 2U;
    }
    while (low + 1U < high) {
      const std::uint64_t middle = low + (high - low) / 2U;
      const auto time = clock.edge_time(middle);
      if (!time.has_value()) return std::nullopt;
      if (now_.less_than(*time)) high = middle;
      else low = middle;
    }
    return high;
  };

  if (cycle_count > maximum_pending_events_ ||
      clocks_.size() > maximum_pending_events_ / cycle_count) {
    failure.limit_reached = true;
    failure.diagnostics.push_back("requested clock edges exceed pending-event capacity");
    return failure;
  }
  const std::uint64_t requested = cycle_count * static_cast<std::uint64_t>(clocks_.size());
  if (queue_.size() > maximum_pending_events_ - requested ||
      requested > std::numeric_limits<std::uint64_t>::max() - next_sequence_) {
    failure.limit_reached = true;
    failure.diagnostics.push_back("requested clock edges exceed pending-event or sequence capacity");
    return failure;
  }
  planned.reserve(static_cast<std::size_t>(requested));
  for (const auto& [name, clock] : clocks_) {
    const auto first_edge = first_edge_after_now(clock);
    if (!first_edge.has_value() ||
        first_edge.value() > std::numeric_limits<std::uint64_t>::max() - (cycle_count - 1U)) {
      failure.diagnostics.push_back("clock edge index overflow in domain '" + name + "'");
      return failure;
    }
    for (std::uint64_t i = 0U; i < cycle_count; ++i) {
      const auto edge_index = *first_edge + i;
      if (edge_index > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        failure.diagnostics.push_back("clock edge index exceeds the event payload range in domain '" + name + "'");
        return failure;
      }
      const auto edge_time = clock.edge_time(edge_index);
      if (!edge_time.has_value()) {
        failure.diagnostics.push_back("clock edge time overflow in domain '" + name + "'");
        return failure;
      }
      planned.push_back(ScheduledEdge{*edge_time, name, edge_index});
    }
  }
  // Commit only after all edge times and all resource limits have been validated.
  for (const auto& edge : planned) {
    const bool scheduled = schedule(edge.time, edge.name, EventKind::ClockEdge,
                                     static_cast<std::int64_t>(edge.index));
    if (!scheduled) {
      failure.diagnostics.push_back("validated clock edge could not be committed");
      return failure;
    }
  }
  return run_events();
}

}  // namespace masl::runtime
