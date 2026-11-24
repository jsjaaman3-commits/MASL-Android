#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"

#include <iostream>
#include <string_view>
#include <utility>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, std::string_view name) {
  ++assertions;
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
  }
}

masl::semantic::Result analyze_source(std::string_view source) {
  auto lexed = masl::lexer::Lexer(source).scan();
  if (!lexed.ok()) {
    std::cerr << "Unexpected lexer failure in semantic test.\n";
    ++failures;
    return {};
  }
  auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
  if (!parsed.ok()) {
    std::cerr << "Unexpected parser failure in semantic test.\n";
    for (const auto& diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    ++failures;
    return {};
  }
  return masl::semantic::analyze(*parsed.program);
}

bool has_code(const masl::semantic::Result& result, std::string_view code) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.code == code) return true;
  }
  return false;
}

constexpr std::string_view kValidModel = R"MASL(masl 0.1;
model ArithmeticPipeline {
  clock core = 1 GHz;
  record Job { id: u64; a: i32; b: i32; result: i32; }
  queue incoming: Job { capacity = 32; discipline = fifo; overflow = block; }
  queue output: Job { capacity = 32; discipline = fifo; overflow = block; }
  pipeline alu: Job -> Job {
    input = incoming; output = output; clock = core;
    latency = 3 cycles(core); initiation_interval = 1 cycles(core);
    max_in_flight = 8;
    on_start(job) { set job.result = job.a + job.b; }
  }
  workload demo {
    repeat 16 as i {
      submit incoming, Job { id = i; a = i; b = i * 2; result = 0; };
    }
  }
  experiment baseline {
    workload = demo; mode = cycle; stop_when = idle; max_cycles = 1000;
    expect completed(alu) == 16;
    export trace = "output/trace.json";
    export stats = "output/stats.json";
  }
  fn sum(a: i32, b: i32) -> i32 { return a + b; }
}
)MASL";

}  // namespace

int main() {
  {
    const auto result = analyze_source(kValidModel);
    if (!result.ok()) {
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << "\n";
      }
    }
    check(result.ok(), "accepts a complete typed arithmetic-pipeline model");
    check(result.expression_types.size() >= 20U,
          "retains inferred types for checked expressions");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model Duplicate { record Item { value: i32; } record Item { other: i64; } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3001"),
          "rejects duplicate model declarations");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadType { record Item { value: ImaginaryType; } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3002"),
          "rejects unknown field types");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadQueue {
  record Item { value: i32; }
  queue q: Item { capacity = 0; discipline = fifo; overflow = block; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3004"),
          "rejects non-positive queue capacities");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadClock { clock core = 0 GHz; }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3005"),
          "rejects zero-frequency clocks");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadDomain {
  clock core = 1 GHz;
  clock memory = 800 MHz;
  record Job { value: i32; }
  queue qin: Job { capacity = 4; discipline = fifo; overflow = block; }
  queue qout: Job { capacity = 4; discipline = fifo; overflow = block; }
  pipeline p: Job -> Job {
    input = qin; output = qout; clock = core;
    latency = 2 cycles(memory); initiation_interval = 1 cycles(core);
    max_in_flight = 2;
  }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3061"),
          "rejects a cycle quantity from the wrong clock domain");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadHandler {
  record Job { value: i32; }
  pipeline p: Job -> Job {
    input = q; output = q; clock = missing;
    latency = 1 ns; initiation_interval = 1 cycles(missing); max_in_flight = 1;
    on_start(job) { set job.missing = 2; }
  }
}
)MASL");
    if (!has_code(result, "MASL-E3030")) {
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << "\n";
      }
    }
    check(!result.ok() && has_code(result, "MASL-E3030"),
          "rejects writes to nonexistent record fields");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadFunction {
  fn calculate(value: i32) -> i32 { return true; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3061"),
          "rejects function return type mismatches");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadCondition {
  workload w { if 1 { assert true; } }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3061"),
          "requires boolean branch conditions");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadSubmit {
  record Item { a: i32; b: i32; }
  queue q: Item { capacity = 4; discipline = fifo; overflow = block; }
  workload w { submit q, Item { a = 1; }; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3040"),
          "requires every record field in a submit initializer");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadLoop { workload w { repeat -1 as i { assert true; } } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3032"),
          "rejects negative repeat bounds");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadCall {
  fn f(a: i32) -> i32 { return a; }
  fn g() -> i32 { return f(1, 2); }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3057"),
          "checks function call arity");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadExperiment {
  workload w { assert true; }
  experiment e { workload = w; mode = cycle; stop_when = max_cycles; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3022"),
          "requires max_cycles when selected as the stop condition");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model ComponentForwardReference {
  instances sms: SM[2] { warp_limit = 32; }
  clock core = 1 GHz;
  record Job { value: i32; }
  component SM {
    param warp_limit: u32 = 48;
    input port jobs: Job;
    output port done: Job;
    pipeline execute: Job -> Job {
      input = jobs; output = done; clock = core;
      latency = 2 cycles(core); initiation_interval = 1 cycles(core);
      max_in_flight = 4;
      on_start(job) { set job.value = job.value + 1; }
    }
  }
}
)MASL");
    check(result.ok(), "resolves component parameters and types independently of declaration order");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model EngineeringTypes {
  clock memory = 1.4 GHz;
  fn timing() -> cycles { return 4 cycles(memory); }
  fn transfer() -> bandwidth { return 64 bytes_per_cycle(memory); }
  fn duration() -> latency { return 8 ns; }
}
)MASL");
    check(result.ok(), "accepts clock-relative engineering types and generic function results");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model MixedSigns {
  fn bad(a: i32, b: u32) -> i32 { return a + b; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3060"),
          "rejects mixed signed and unsigned arithmetic without a lossless common type");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model OverflowingLiteral {
  fn bad(value: i16) -> i16 { return value + 50000; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3062"),
          "rejects an integer literal outside the operand type range");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model MixedCompare {
  fn bad(a: i32, b: u32) -> bool { return a < b; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3047"),
          "rejects comparisons without a lossless common numeric type");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model BadFieldRead {
  record Item { value: i32; }
  fn bad(item: Item) -> i32 { return item.missing; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3059"),
          "rejects reads of nonexistent record fields");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model DuplicateFields { record Item { value: i32; value: u32; } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3001"),
          "rejects duplicate record field names");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model MissingReturn { fn bad(value: i32) -> i32 { if value > 0 { return value; } } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3025"),
          "requires a return value on every statically recognized function path");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model NominalEnums {
  enum StateA { ready, busy }
  enum StateB { ready, busy }
  fn bad(value: StateA) -> StateB { return value; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3061"),
          "keeps enum types nominal even when their variants are identical");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model ClockDomainArithmetic {
  clock a = 1 GHz;
  clock b = 2 GHz;
  fn bad() -> cycles { return 2 cycles(a) + 3 cycles(b); }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3049"),
          "prevents arithmetic across distinct clock domains");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model DuplicateProperty {
  record Item { value: i32; }
  queue q: Item {
    capacity = 2; capacity = 3;
    discipline = fifo; overflow = block;
  }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3063"),
          "rejects duplicate properties in a declaration block");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model IntegerBoundaries {
  fn maximum() -> u64 { return 18446744073709551615; }
  fn minimum() -> i64 { return -9223372036854775808; }
}
)MASL");
    check(result.ok(), "accepts full-width unsigned maximum and signed minimum integer literals");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model FloatOverflow { fn value() -> f32 { return 1e100; } }
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3064"),
          "rejects floating literals outside the target IEEE type range");
  }

  {
    const auto result = analyze_source(R"MASL(masl 0.1;
model NumericWidening {
  fn widen(value: i16) -> i64 { return value; }
  fn narrow(value: i64) -> i16 { return value; }
}
)MASL");
    check(!result.ok() && has_code(result, "MASL-E3061"),
          "allows value-preserving integer widening and rejects narrowing");
  }

  std::cout << "Semantic tests: " << (assertions - failures) << '/' << assertions
            << " passed.\n";
  return failures == 0 ? 0 : 1;
}
