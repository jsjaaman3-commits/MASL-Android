#include "masl/ir/ir.hpp"
#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"

#include <iostream>
#include <string_view>
#include <utility>

namespace {

int failures = 0;
int assertions = 0;

void check(bool condition, std::string_view description) {
  ++assertions;
  if (condition) {
    std::cout << "[PASS] " << description << '\n';
  } else {
    std::cerr << "[FAIL] " << description << '\n';
    ++failures;
  }
}

struct Pipeline final {
  masl::parser::ParseResult parsed;
  masl::semantic::Result semantic;
};

Pipeline prepare(std::string_view source) {
  auto lexed = masl::lexer::Lexer(source).scan();
  if (!lexed.ok()) {
    std::cerr << "Lexer failed in IR test.\n";
    ++failures;
    return {};
  }
  auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
  if (!parsed.ok()) {
    std::cerr << "Parser failed in IR test.\n";
    for (const auto& diagnostic : parsed.diagnostics)
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    ++failures;
    return {};
  }
  auto semantic = masl::semantic::analyze(*parsed.program);
  return {std::move(parsed), std::move(semantic)};
}

constexpr std::string_view kSource = R"MASL(masl 0.1;
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
  }
})MASL";

}  // namespace

int main() {
  {
    auto pipeline = prepare(kSource);
    check(pipeline.parsed.ok(), "parses the source fixture");
    check(pipeline.semantic.ok(), "semantic gate accepts the valid fixture");
    if (pipeline.parsed.ok() && pipeline.semantic.ok()) {
      auto lowered = masl::ir::lower(*pipeline.parsed.program, pipeline.semantic);
      check(lowered.ok(), "lowers the semantically checked AST to Typed IR");
      if (lowered.ok()) {
        const auto& module = *lowered.module;
        check(module.ir_major == 1U && module.ir_minor == 0U,
              "sets stable Typed IR schema version 1.0");
        check(module.name == "ArithmeticPipeline" && module.declarations.size() == 7U,
              "preserves module identity and all top-level declarations");
        const auto* record = std::get_if<masl::ir::Record>(&module.declarations[1]);
        check(record != nullptr && record->fields.size() == 4U &&
                  record->fields[1].type.kind == masl::semantic::TypeKind::SignedInteger &&
                  record->fields[1].type.bits == 32U,
              "lowers record fields with concrete signed integer types");
        const auto* pipeline_ir = std::get_if<masl::ir::Pipeline>(&module.declarations[4]);
        check(pipeline_ir != nullptr &&
                  pipeline_ir->input_type.kind == masl::semantic::TypeKind::Record &&
                  pipeline_ir->input_type.nominal_name == "Job" &&
                  pipeline_ir->latency.type.kind == masl::semantic::TypeKind::Cycles &&
                  pipeline_ir->latency.type.clock_domain == "core",
              "retains nominal endpoint types and clock-domain-aware cycle units");
        const auto* workload = std::get_if<masl::ir::Workload>(&module.declarations[5]);
        check(workload != nullptr && workload->body.statements.size() == 1U &&
                  workload->body.statements[0].kind == masl::ir::StatementKind::Repeat,
              "normalizes workload and nested control-flow statement structure");
        if (workload != nullptr) {
          const auto& repeat = workload->body.statements[0];
          check(repeat.expression.has_value() &&
                    repeat.expression->type.kind == masl::semantic::TypeKind::IntegerLiteral &&
                    repeat.body.statements.size() == 1U &&
                    repeat.body.statements[0].kind == masl::ir::StatementKind::Submit,
                "preserves typed repeat bound and typed submit payload assignments");
        }
        const std::string text = masl::ir::dump(module);
        check(text.find("MASL Typed IR 1.0") != std::string::npos &&
                  text.find("cycles(core)") != std::string::npos &&
                  text.find("record Job") != std::string::npos &&
                  text.find("`completed` symbols=alu") != std::string::npos,
              "emits a deterministic dump including typed intrinsic symbol references");
        check(masl::ir::verify(module).empty(), "verifies the lowered module");
      }
    }
  }

  {
    auto pipeline = prepare(R"MASL(masl 0.1;
model NestedComponent {
  clock core = 1 GHz;
  record Job { id: u64; }
  component SM {
    param warp_limit: u32 = 48;
    input port jobs: Job;
    output port done: Job;
    queue ready: Job { capacity = 64; discipline = fifo; overflow = block; }
    resource alu_slots { capacity = 1; unit = slot; }
    pipeline alu: Job -> Job {
      input = ready; output = done; clock = core;
      latency = 2 cycles(core); initiation_interval = 1 cycles(core);
      max_in_flight = 4;
      on_start(job) { set job.id = job.id; }
    }
  }
  instances sms: SM[2] { warp_limit = 48; }
})MASL");
    check(pipeline.parsed.ok() && pipeline.semantic.ok(),
          "semantics types component-local queue/resource capacities");
    if (pipeline.parsed.ok() && pipeline.semantic.ok()) {
      auto lowered = masl::ir::lower(*pipeline.parsed.program, pipeline.semantic);
      check(lowered.ok(), "lowers nested component and instance array to Typed IR");
      if (lowered.ok()) {
        const auto* component = std::get_if<masl::ir::Component>(&lowered.module->declarations[2]);
        check(component != nullptr && component->members.size() == 6U,
              "preserves parameters, ports, queue, resource and pipeline component members");
        if (component != nullptr) {
          const auto* queue = std::get_if<masl::ir::Queue>(&component->members[3]);
          const auto* resource = std::get_if<masl::ir::Resource>(&component->members[4]);
          check(queue != nullptr && queue->capacity.type.kind == masl::semantic::TypeKind::IntegerLiteral &&
                    resource != nullptr && resource->capacity.type.kind == masl::semantic::TypeKind::IntegerLiteral,
                "preserves semantic types for component queue and resource capacities");
        }
        const auto* instance = std::get_if<masl::ir::Instance>(&lowered.module->declarations[3]);
        check(instance != nullptr && instance->component_type == "SM" &&
                  instance->count.type.kind == masl::semantic::TypeKind::IntegerLiteral,
              "retains typed component instance array dimensions");
      }
    }
  }

  {
    auto pipeline = prepare(R"MASL(masl 0.1;
model FunctionsAndControlFlow {
  fn twice(value: i32) -> i32 {
    let result: i32 = value * 2;
    return result;
  }
  fn choose(value: i32) -> i32 {
    if value > 0 { return twice(value); }
    else { return 0; }
  }
})MASL");
    check(pipeline.parsed.ok() && pipeline.semantic.ok(),
          "semantics accepts typed functions and full if/else return paths");
    if (pipeline.parsed.ok() && pipeline.semantic.ok()) {
      auto lowered = masl::ir::lower(*pipeline.parsed.program, pipeline.semantic);
      check(lowered.ok(), "lowers functions and nested control-flow into typed blocks");
      if (lowered.ok()) {
        const auto* twice = std::get_if<masl::ir::Function>(&lowered.module->declarations[0]);
        const auto* choose = std::get_if<masl::ir::Function>(&lowered.module->declarations[1]);
        check(twice != nullptr && twice->parameters.size() == 1U &&
                  twice->parameters[0].type.kind == masl::semantic::TypeKind::SignedInteger &&
                  twice->body.statements[0].declared_semantic_type.bits == 32U,
              "retains concrete function and local declaration types");
        check(choose != nullptr && choose->body.statements[0].kind == masl::ir::StatementKind::If &&
                  choose->body.statements[0].else_body.has_value() &&
                  choose->body.statements[0].body.statements[0].kind == masl::ir::StatementKind::Return,
              "retains typed conditions, both branches and return statements");
        const auto* call = choose != nullptr && !choose->body.statements.empty() &&
                           !choose->body.statements[0].body.statements.empty()
            ? choose->body.statements[0].body.statements[0].expression.operator->() : nullptr;
        check(call != nullptr && call->kind == masl::ir::ExpressionKind::Call && call->value == "twice",
              "normalizes direct function calls to a symbolic target and typed arguments");
      }
    }
  }

  {
    auto pipeline = prepare(R"MASL(masl 0.1; model Invalid { queue q: Unknown { capacity = 1; discipline = fifo; overflow = block; } })MASL");
    check(pipeline.parsed.ok() && !pipeline.semantic.ok(),
          "semantic analysis rejects invalid source before lowering");
    if (pipeline.parsed.ok()) {
      auto lowered = masl::ir::lower(*pipeline.parsed.program, pipeline.semantic);
      check(!lowered.ok() && !lowered.diagnostics.empty(),
            "refuses to lower source with failed semantic analysis");
    }
  }

  {
    masl::ir::Module malformed;
    malformed.name = "Malformed";
    masl::ir::Workload workload;
    workload.name = "broken";
    masl::ir::Statement statement;
    statement.kind = masl::ir::StatementKind::Assert;
    masl::ir::Expression unary;
    unary.kind = masl::ir::ExpressionKind::Unary;
    unary.type = masl::semantic::Type{masl::semantic::TypeKind::Bool};
    unary.operation = masl::lexer::TokenKind::Bang;
    statement.expression = unary;
    workload.body.statements.push_back(statement);
    malformed.declarations.emplace_back(workload);
    auto diagnostics = masl::ir::verify(malformed);
    check(!diagnostics.empty(), "IR verifier rejects a malformed unary operand structure");
    check(masl::ir::verify(malformed).size() == diagnostics.size(),
          "IR verification diagnostics are deterministic");
  }

  std::cout << "IR test assertions: " << assertions << ", failures: " << failures << '\n';
  return failures == 0 ? 0 : 1;
}
