#include "masl/ast/ast.hpp"
#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
  }
}

masl::parser::ParseResult parse_source(std::string_view source) {
  auto lexed = masl::lexer::Lexer(source).scan();
  if (!lexed.ok()) {
    std::cerr << "Unexpected lexer failure in parser test.\n";
    ++failures;
    return {};
  }
  return masl::parser::Parser(std::move(lexed.tokens)).parse();
}

template <typename T>
const T* member_as(const masl::ast::ModelMember& member) {
  return std::get_if<T>(&member);
}

const std::string kArithmeticSource = R"MASL(masl 0.1;
model ArithmeticPipeline {
  clock core = 1 GHz;
  record Job { id: u64; a: i32; b: i32; result: i32; }
  queue incoming: Job { capacity = 32; discipline = fifo; overflow = block; }
  queue output: Job { capacity = 32; discipline = fifo; overflow = block; }
  pipeline alu: Job -> Job {
    input = incoming;
    output = output;
    clock = core;
    latency = 3 cycles(core);
    initiation_interval = 1 cycles(core);
    max_in_flight = 8;
    on_start(job) { set job.result = job.a + job.b; }
  }
  workload demo {
    repeat 16 as i {
      submit incoming, Job { id = i; a = i; b = i * 2; result = 0; };
    }
  }
  experiment baseline {
    workload = demo;
    mode = cycle;
    stop_when = idle;
    max_cycles = 1000;
    expect completed(alu) == 16;
    export trace = "output/trace.json";
    export stats = "output/stats.json";
  }
})MASL";

}  // namespace

int main() {
  using namespace masl;

  {
    auto parsed = parse_source(kArithmeticSource);
    check(parsed.ok(), "parses the complete arithmetic pipeline source program");
    check(parsed.program != nullptr && parsed.program->model.name == "ArithmeticPipeline",
          "retains model name and source version");
    if (parsed.ok()) {
      const auto& members = parsed.program->model.members;
      check(parsed.program->language_major == 0U &&
                parsed.program->language_minor == 1U,
            "stores language version 0.1");
      check(members.size() == 7U, "builds seven top-level declaration nodes");
      check(member_as<ast::ClockDeclaration>(members[0]) != nullptr,
            "builds clock declaration AST node");
      const auto* record = member_as<ast::RecordDeclaration>(members[1]);
      check(record != nullptr && record->name == "Job" &&
                record->fields.size() == 4U,
            "builds typed record and field declarations");
      const auto* incoming = member_as<ast::QueueDeclaration>(members[2]);
      const auto* output = member_as<ast::QueueDeclaration>(members[3]);
      check(incoming != nullptr && output != nullptr &&
                incoming->discipline == "fifo" && incoming->overflow == "block" &&
                output->name == "output",
            "parses queues including contextual output name and policies");
      const auto* pipeline = member_as<ast::PipelineDeclaration>(members[4]);
      check(pipeline != nullptr && pipeline->input_queue == "incoming" &&
                pipeline->output_queue == "output" && pipeline->clock == "core" &&
                pipeline->handler_parameter == "job" && pipeline->on_start.has_value(),
            "parses pipeline ports, clock, timing and handler");
      check(pipeline != nullptr && pipeline->latency != nullptr &&
                std::holds_alternative<ast::UnitExpr>(pipeline->latency->node) &&
                pipeline->initiation_interval != nullptr &&
                pipeline->max_in_flight != nullptr,
            "retains typed unit-expression nodes for pipeline timing");
      const auto* workload = member_as<ast::WorkloadDeclaration>(members[5]);
      check(workload != nullptr && workload->body.statements.size() == 1U,
            "parses workload and bounded repeat statement");
      if (workload != nullptr && !workload->body.statements.empty()) {
        const auto* repeat = std::get_if<ast::RepeatStatement>(
            &workload->body.statements.front()->node);
        check(repeat != nullptr && repeat->index_name == "i" &&
                  repeat->body.statements.size() == 1U,
              "builds repeat body and submit statement AST");
        if (repeat != nullptr && !repeat->body.statements.empty()) {
          const auto* submit = std::get_if<ast::SubmitStatement>(
              &repeat->body.statements.front()->node);
          check(submit != nullptr && submit->queue_name == "incoming" &&
                    submit->record_type == "Job" && submit->fields.size() == 4U,
                "parses submitted record fields");
        }
      }
      const auto* experiment = member_as<ast::ExperimentDeclaration>(members[6]);
      check(experiment != nullptr && experiment->workload == "demo" &&
                experiment->mode == "cycle" && experiment->stop_when == "idle" &&
                experiment->expectations.size() == 1U &&
                experiment->exports.size() == 2U,
            "parses experiment configuration, expectation and exports");
      if (experiment != nullptr && !experiment->expectations.empty()) {
        const auto& left = experiment->expectations.front().left;
        check(left != nullptr && std::holds_alternative<ast::CallExpr>(left->node),
              "parses completed(pipeline) as a built-in call expression");
      }
    }
  }

  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model Expressions {
  fn calc(a: i32, b: i32) -> i32 {
    return 1 + 2 * 3 - (4 / 2);
  }
})MASL");
    check(parsed.ok(), "parses typed function and nested expression");
    if (parsed.ok()) {
      const auto* function = member_as<ast::FunctionDeclaration>(
          parsed.program->model.members.front());
      const auto* returned = function != nullptr && !function->body.statements.empty()
          ? std::get_if<ast::ReturnStatement>(&function->body.statements.front()->node)
          : nullptr;
      const auto* subtraction = returned != nullptr && returned->value != nullptr
          ? std::get_if<ast::BinaryExpr>(&returned->value->node) : nullptr;
      const auto* addition = subtraction != nullptr
          ? std::get_if<ast::BinaryExpr>(&subtraction->left->node) : nullptr;
      const auto* multiplication = addition != nullptr
          ? std::get_if<ast::BinaryExpr>(&addition->right->node) : nullptr;
      check(function != nullptr && function->parameters.size() == 2U &&
                function->return_type == "i32",
            "retains function parameters and return type");
      check(subtraction != nullptr && subtraction->op == lexer::TokenKind::Minus &&
                addition != nullptr && addition->op == lexer::TokenKind::Plus &&
                multiplication != nullptr && multiplication->op == lexer::TokenKind::Star,
            "Pratt parser applies multiplication precedence over addition");
    }
  }

  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model Components {
  clock core = 1 GHz;
  record Job { result: i32; a: i32; b: i32; }
  component SM {
    param warp_limit: u32 = 48;
    input port jobs: Job;
    output port completed: Job;
    queue ready: Job { capacity = 64; discipline = fifo; overflow = block; }
    pipeline fp32: Job -> Job {
      input = ready; output = completed; clock = core;
      latency = 4 cycles(core); initiation_interval = 1 cycles(core);
      max_in_flight = 16;
      on_start(job) { set job.result = job.a + job.b; }
    }
    connect jobs -> ready;
  }
  instances sms: SM[2] { warp_limit = 48; }
})MASL");
    check(parsed.ok(), "parses component and component-array declarations");
    if (parsed.ok()) {
      const auto* component = member_as<ast::ComponentDeclaration>(
          parsed.program->model.members[2]);
      const auto* array = member_as<ast::InstanceDeclaration>(
          parsed.program->model.members[3]);
      check(component != nullptr && component->members.size() == 6U,
            "builds reusable component member hierarchy");
      check(array != nullptr && array->name == "sms" &&
                array->component_type == "SM" && array->count != nullptr &&
                array->parameters.size() == 1U,
            "parses fixed-size component instance arrays");
      if (component != nullptr) {
        bool saw_input = false;
        bool saw_output = false;
        bool saw_connection = false;
        for (const auto& member : component->members) {
          if (const auto* port = std::get_if<ast::PortDeclaration>(&member)) {
            saw_input = saw_input || (port->is_input && port->name == "jobs");
            saw_output = saw_output || (!port->is_input && port->name == "completed");
          }
          saw_connection = saw_connection ||
              std::holds_alternative<ast::ConnectionDeclaration>(member);
        }
        check(saw_input && saw_output && saw_connection,
              "parses typed directional ports and connections");
      }
    }
  }

  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model Branching {
  workload w {
    if true { assert 1 < 2; } else if false { assert false; } else { assert true; }
  }
})MASL");
    check(parsed.ok(), "parses if/else-if/else statement blocks");
    if (parsed.ok()) {
      const auto* workload = member_as<ast::WorkloadDeclaration>(
          parsed.program->model.members.front());
      const auto* branch = workload != nullptr && !workload->body.statements.empty()
          ? std::get_if<ast::IfStatement>(&workload->body.statements.front()->node)
          : nullptr;
      check(branch != nullptr && branch->else_block.has_value() &&
                branch->else_block->statements.size() == 1U,
            "represents else-if as nested if inside else block");
    }
  }

  {
    auto parsed = parse_source("masl 0.1; model Broken { clock core = 1 GHz }");
    check(!parsed.ok(), "rejects missing declaration semicolon");
    bool has_missing_semicolon = false;
    bool has_location = false;
    for (const auto& diagnostic : parsed.diagnostics) {
      has_missing_semicolon = has_missing_semicolon || diagnostic.code == "MASL-E2002";
      has_location = has_location || diagnostic.span.start.line == 1U;
    }
    check(has_missing_semicolon, "reports stable parser syntax diagnostic identifier");
    check(has_location, "attaches source location to parser diagnostic");
  }

  {
    auto parsed = parse_source("masl 0.1; model Broken { mystery item; }");
    bool has_unexpected = false;
    for (const auto& diagnostic : parsed.diagnostics) {
      has_unexpected = has_unexpected || diagnostic.code == "MASL-E2001";
    }
    check(!parsed.ok() && has_unexpected,
          "recovers from unknown top-level declaration with syntax diagnostic");
  }


  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model StandardDeclarations {
  clock memory = 1.4 GHz;
  enum WorkState { created, queued, running, waiting, completed, failed }
  resource alu_slots { capacity = 2; unit = slot; }
  queue work: Job { capacity = 16; discipline = priority; overflow = reject; }
  fn timing() -> cycles { return 4 cycles(memory); }
  fn transfer() -> bandwidth { return 64 bytes_per_cycle(memory); }
})MASL");
    check(parsed.ok(), "parses enum, resource, priority queue and clock-relative unit literals");
    if (parsed.ok()) {
      const auto* enumeration = member_as<ast::EnumDeclaration>(
          parsed.program->model.members[1]);
      const auto* resource = member_as<ast::ResourceDeclaration>(
          parsed.program->model.members[2]);
      const auto* queue = member_as<ast::QueueDeclaration>(
          parsed.program->model.members[3]);
      check(enumeration != nullptr && enumeration->variants.size() == 6U,
            "stores enum variants");
      check(resource != nullptr && resource->unit == "slot" &&
                resource->capacity != nullptr,
            "stores resource capacity and unit");
      check(queue != nullptr && queue->discipline == "priority" &&
                queue->overflow == "reject",
            "stores alternative queue policies");
      const auto* timing = member_as<ast::FunctionDeclaration>(
          parsed.program->model.members[4]);
      const auto* transfer = member_as<ast::FunctionDeclaration>(
          parsed.program->model.members[5]);
      const auto* timing_return = timing != nullptr && !timing->body.statements.empty()
          ? std::get_if<ast::ReturnStatement>(&timing->body.statements.front()->node)
          : nullptr;
      const auto* transfer_return = transfer != nullptr && !transfer->body.statements.empty()
          ? std::get_if<ast::ReturnStatement>(&transfer->body.statements.front()->node)
          : nullptr;
      const auto* cycles = timing_return != nullptr && timing_return->value != nullptr
          ? std::get_if<ast::UnitExpr>(&timing_return->value->node) : nullptr;
      const auto* bandwidth = transfer_return != nullptr && transfer_return->value != nullptr
          ? std::get_if<ast::UnitExpr>(&transfer_return->value->node) : nullptr;
      check(cycles != nullptr && cycles->unit == "cycles" &&
                cycles->clock_domain == "memory",
            "retains cycle unit and clock-domain binding");
      check(bandwidth != nullptr && bandwidth->unit == "bytes_per_cycle" &&
                bandwidth->clock_domain == "memory",
            "retains bytes-per-cycle unit and clock-domain binding");
    }
  }

  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model Operators {
  fn f() -> i32 { return 9 - 4 - 1; }
  fn g(x: i32) -> i32 { return -(x + 2) * 3; }
})MASL");
    check(parsed.ok(), "parses unary operations and chained binary operations");
    if (parsed.ok()) {
      const auto* function = member_as<ast::FunctionDeclaration>(
          parsed.program->model.members[0]);
      const auto* returned = function != nullptr && !function->body.statements.empty()
          ? std::get_if<ast::ReturnStatement>(&function->body.statements.front()->node)
          : nullptr;
      const auto* outer = returned != nullptr && returned->value != nullptr
          ? std::get_if<ast::BinaryExpr>(&returned->value->node) : nullptr;
      const auto* left = outer != nullptr
          ? std::get_if<ast::BinaryExpr>(&outer->left->node) : nullptr;
      check(outer != nullptr && outer->op == lexer::TokenKind::Minus &&
                left != nullptr && left->op == lexer::TokenKind::Minus,
            "binary subtraction is left-associative");
    }
  }

  {
    auto parsed = parse_source("masl 0.1.0; model BadVersion {}");
    bool has_version_error = false;
    for (const auto& diagnostic : parsed.diagnostics) {
      has_version_error = has_version_error || diagnostic.code == "MASL-E2003";
    }
    check(!parsed.ok() && has_version_error,
          "rejects source version outside major.minor grammar");
  }


  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model ContextualNames {
  fn output_name() -> i32 {
    let output: i32 = 7;
    return output;
  }
})MASL");
    check(parsed.ok(), "accepts contextual output token in declaration and expression name positions");
    if (parsed.ok()) {
      const auto* function = member_as<ast::FunctionDeclaration>(
          parsed.program->model.members.front());
      const auto* returned = function != nullptr && function->body.statements.size() == 2U
          ? std::get_if<ast::ReturnStatement>(&function->body.statements[1]->node)
          : nullptr;
      const auto* name = returned != nullptr && returned->value != nullptr
          ? std::get_if<ast::NameExpr>(&returned->value->node) : nullptr;
      check(name != nullptr && name->name == "output",
            "builds NameExpr for reserved contextual name output");
    }
  }

  {
    auto parsed = parse_source(R"MASL(masl 0.1;
model LatencyType { fn duration() -> latency { return 2 ns; } }
)MASL");
    check(parsed.ok(), "accepts reserved engineering type name latency in function signatures");
  }

  if (failures != 0) {
    std::cerr << failures << " parser test(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All parser tests passed.\n";
  return EXIT_SUCCESS;
}
