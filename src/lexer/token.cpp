#include "masl/lexer/token.hpp"

namespace masl::lexer {

std::string_view token_kind_name(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::EndOfFile: return "EndOfFile";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::Number: return "Number";
    case TokenKind::String: return "String";
    case TokenKind::Unit: return "Unit";
    case TokenKind::KwMasl: return "masl";
    case TokenKind::KwModel: return "model";
    case TokenKind::KwClock: return "clock";
    case TokenKind::KwRecord: return "record";
    case TokenKind::KwEnum: return "enum";
    case TokenKind::KwQueue: return "queue";
    case TokenKind::KwCapacity: return "capacity";
    case TokenKind::KwDiscipline: return "discipline";
    case TokenKind::KwOverflow: return "overflow";
    case TokenKind::KwFifo: return "fifo";
    case TokenKind::KwPriority: return "priority";
    case TokenKind::KwRoundRobin: return "round_robin";
    case TokenKind::KwBlock: return "block";
    case TokenKind::KwReject: return "reject";
    case TokenKind::KwDrop: return "drop";
    case TokenKind::KwResource: return "resource";
    case TokenKind::KwUnit: return "unit";
    case TokenKind::KwSlot: return "slot";
    case TokenKind::KwPipeline: return "pipeline";
    case TokenKind::KwInput: return "input";
    case TokenKind::KwOutput: return "output";
    case TokenKind::KwOnStart: return "on_start";
    case TokenKind::KwLatency: return "latency";
    case TokenKind::KwInitiationInterval: return "initiation_interval";
    case TokenKind::KwMaxInFlight: return "max_in_flight";
    case TokenKind::KwComponent: return "component";
    case TokenKind::KwParam: return "param";
    case TokenKind::KwPort: return "port";
    case TokenKind::KwConnect: return "connect";
    case TokenKind::KwInstances: return "instances";
    case TokenKind::KwWorkload: return "workload";
    case TokenKind::KwRepeat: return "repeat";
    case TokenKind::KwAs: return "as";
    case TokenKind::KwSubmit: return "submit";
    case TokenKind::KwExperiment: return "experiment";
    case TokenKind::KwMode: return "mode";
    case TokenKind::KwCycle: return "cycle";
    case TokenKind::KwEvent: return "event";
    case TokenKind::KwStopWhen: return "stop_when";
    case TokenKind::KwIdle: return "idle";
    case TokenKind::KwMaxCycles: return "max_cycles";
    case TokenKind::KwMaxEvents: return "max_events";
    case TokenKind::KwSeed: return "seed";
    case TokenKind::KwExpect: return "expect";
    case TokenKind::KwCompleted: return "completed";
    case TokenKind::KwExport: return "export";
    case TokenKind::KwTrace: return "trace";
    case TokenKind::KwStats: return "stats";
    case TokenKind::KwCsv: return "csv";
    case TokenKind::KwFn: return "fn";
    case TokenKind::KwLet: return "let";
    case TokenKind::KwSet: return "set";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwAssert: return "assert";
    case TokenKind::KwTrue: return "true";
    case TokenKind::KwFalse: return "false";
    case TokenKind::LeftBrace: return "{";
    case TokenKind::RightBrace: return "}";
    case TokenKind::LeftParen: return "(";
    case TokenKind::RightParen: return ")";
    case TokenKind::LeftBracket: return "[";
    case TokenKind::RightBracket: return "]";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Colon: return ":";
    case TokenKind::Comma: return ",";
    case TokenKind::Dot: return ".";
    case TokenKind::Arrow: return "->";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Assign: return "=";
    case TokenKind::Equal: return "==";
    case TokenKind::Bang: return "!";
    case TokenKind::NotEqual: return "!=";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::AndAnd: return "&&";
    case TokenKind::OrOr: return "||";
  }
  return "UnknownToken";
}

}  // namespace masl::lexer
