#include "masl/parser/parser.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>
#include <utility>

namespace masl::parser {
namespace {

using lexer::SourcePosition;
using lexer::TokenKind;

std::string decode_string_literal(std::string_view raw) {
  if (raw.size() < 2U) {
    return {};
  }
  std::string decoded;
  decoded.reserve(raw.size() - 2U);
  for (std::size_t index = 1; index + 1U < raw.size(); ++index) {
    char value = raw[index];
    if (value == '\\' && index + 2U < raw.size()) {
      ++index;
      switch (raw[index]) {
        case '"': decoded.push_back('"'); break;
        case '\\': decoded.push_back('\\'); break;
        case 'n': decoded.push_back('\n'); break;
        case 'r': decoded.push_back('\r'); break;
        case 't': decoded.push_back('\t'); break;
        case '0': decoded.push_back('\0'); break;
        default: decoded.push_back(raw[index]); break;
      }
    } else {
      decoded.push_back(value);
    }
  }
  return decoded;
}

bool parse_unsigned_component(std::string_view value, unsigned int& result) {
  if (value.empty()) {
    return false;
  }
  unsigned int parsed = 0;
  const auto conversion = std::from_chars(value.data(),
                                           value.data() + value.size(), parsed);
  if (conversion.ec != std::errc{} ||
      conversion.ptr != value.data() + value.size()) {
    return false;
  }
  result = parsed;
  return true;
}

}  // namespace

Parser::Parser(std::vector<lexer::Token> tokens) : tokens_(std::move(tokens)) {
  if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
    SourcePosition end{};
    if (!tokens_.empty()) {
      end = tokens_.back().span.end;
    }
    tokens_.push_back(lexer::Token{TokenKind::EndOfFile, "", {end, end}});
  }
}

const lexer::Token& Parser::current() const noexcept {
  return tokens_[current_ < tokens_.size() ? current_ : tokens_.size() - 1U];
}

const lexer::Token& Parser::previous() const noexcept {
  return tokens_[current_ == 0U ? 0U : current_ - 1U];
}

bool Parser::at_end() const noexcept {
  return current().kind == TokenKind::EndOfFile;
}

bool Parser::check(TokenKind kind) const noexcept { return current().kind == kind; }

bool Parser::match(TokenKind kind) noexcept {
  if (!check(kind)) {
    return false;
  }
  static_cast<void>(advance());
  return true;
}

const lexer::Token& Parser::advance() noexcept {
  if (!at_end()) {
    ++current_;
  }
  return previous();
}

bool Parser::expect(TokenKind kind, std::string message) {
  if (match(kind)) {
    return true;
  }
  error("MASL-E2002", std::move(message), current().span);
  return false;
}

bool Parser::is_name_token() const noexcept {
  return check(TokenKind::Identifier) || check(TokenKind::KwOutput) ||
         check(TokenKind::KwCompleted);
}

std::string Parser::parse_name() {
  if (is_name_token()) {
    return advance().lexeme;
  }
  error("MASL-E2002", "Expected a name (identifier).", current().span);
  if (!at_end() && !check(TokenKind::RightBrace) &&
      !check(TokenKind::Semicolon)) {
    static_cast<void>(advance());
  }
  return "<error>";
}

std::string Parser::parse_type_name() {
  if (check(TokenKind::Identifier) || check(TokenKind::Unit) ||
      check(TokenKind::KwLatency)) {
    return advance().lexeme;
  }
  error("MASL-E2002", "Expected a type name.", current().span);
  if (!at_end() && !check(TokenKind::RightBrace) &&
      !check(TokenKind::Semicolon)) {
    static_cast<void>(advance());
  }
  return "<error>";
}

bool Parser::is_comparison_operator() const noexcept {
  return check(TokenKind::Equal) || check(TokenKind::NotEqual) ||
         check(TokenKind::Less) || check(TokenKind::LessEqual) ||
         check(TokenKind::Greater) || check(TokenKind::GreaterEqual);
}

SourceSpan Parser::joined_span(SourceSpan start, SourceSpan end) const noexcept {
  return SourceSpan{start.start, end.end};
}

void Parser::error(std::string code, std::string message, SourceSpan span) {
  result_.diagnostics.push_back(
      ParseDiagnostic{std::move(code), std::move(message), span});
}

void Parser::synchronize() {
  while (!at_end()) {
    if (previous().kind == TokenKind::Semicolon) {
      return;
    }
    if (check(TokenKind::RightBrace)) {
      return;
    }
    switch (current().kind) {
      case TokenKind::KwClock:
      case TokenKind::KwRecord:
      case TokenKind::KwEnum:
      case TokenKind::KwQueue:
      case TokenKind::KwResource:
      case TokenKind::KwPipeline:
      case TokenKind::KwComponent:
      case TokenKind::KwInstances:
      case TokenKind::KwWorkload:
      case TokenKind::KwExperiment:
      case TokenKind::KwFn:
        return;
      default:
        static_cast<void>(advance());
        break;
    }
  }
}

ParseResult Parser::parse() {
  result_.program = std::make_unique<ast::Program>(parse_program());
  return std::move(result_);
}

ast::Program Parser::parse_program() {
  ast::Program program;
  const SourceSpan start = current().span;
  if (expect(TokenKind::KwMasl, "Expected source version directive 'masl'.")) {
    if (check(TokenKind::Number)) {
      const lexer::Token version_token = advance();
      const std::string_view text(version_token.lexeme);
      const std::size_t separator = text.find('.');
      const bool one_separator = separator != std::string_view::npos &&
          text.find('.', separator + 1U) == std::string_view::npos;
      const bool has_exponent = text.find_first_of("eE") != std::string_view::npos;
      if (!one_separator || has_exponent ||
          !parse_unsigned_component(text.substr(0U, separator),
                                     program.language_major) ||
          !parse_unsigned_component(text.substr(separator + 1U),
                                     program.language_minor)) {
        error("MASL-E2003",
              "Language version must have the form major.minor, for example 0.1.",
              version_token.span);
      }
      if (match(TokenKind::Dot)) {
        SourceSpan invalid_version_span = joined_span(version_token.span, previous().span);
        if (check(TokenKind::Number)) {
          invalid_version_span = joined_span(invalid_version_span, advance().span);
        }
        while (match(TokenKind::Dot)) {
          invalid_version_span = joined_span(invalid_version_span, previous().span);
          if (check(TokenKind::Number)) {
            invalid_version_span = joined_span(invalid_version_span, advance().span);
          } else {
            break;
          }
        }
        error("MASL-E2003",
              "Language version has extra components; expected major.minor, not major.minor.patch.",
              invalid_version_span);
      }
    } else {
      error("MASL-E2002", "Expected a version number after 'masl'.",
            current().span);
    }
    static_cast<void>(expect(TokenKind::Semicolon,
                             "Expected ';' after language version."));
  }

  static_cast<void>(expect(TokenKind::KwModel, "Expected one 'model' declaration."));
  program.model.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace,
                           "Expected '{' to begin model declaration."));

  while (!at_end() && !check(TokenKind::RightBrace)) {
    const std::size_t before = current_;
    auto member = parse_model_member();
    if (member.has_value()) {
      program.model.members.push_back(std::move(*member));
    }
    if (current_ == before) {
      error("MASL-E2001", "Parser made no progress while reading model member.",
            current().span);
      static_cast<void>(advance());
    }
  }

  static_cast<void>(expect(TokenKind::RightBrace,
                           "Expected '}' to close model declaration."));
  program.model.span = joined_span(start, previous().span);
  if (!at_end()) {
    error("MASL-E2001", "Unexpected tokens after the model declaration.",
          current().span);
  }
  return program;
}

std::optional<ast::ModelMember> Parser::parse_model_member() {
  if (match(TokenKind::KwClock)) return parse_clock();
  if (match(TokenKind::KwRecord)) return parse_record();
  if (match(TokenKind::KwEnum)) return parse_enum();
  if (match(TokenKind::KwQueue)) return parse_queue();
  if (match(TokenKind::KwResource)) return parse_resource();
  if (match(TokenKind::KwPipeline)) return parse_pipeline();
  if (match(TokenKind::KwComponent)) return parse_component();
  if (match(TokenKind::KwInstances)) return parse_instance();
  if (match(TokenKind::KwWorkload)) return parse_workload();
  if (match(TokenKind::KwExperiment)) return parse_experiment();
  if (match(TokenKind::KwFn)) return parse_function();

  error("MASL-E2001", "Unexpected token in model declaration: '" +
                            current().lexeme + "'.", current().span);
  synchronize();
  return std::nullopt;
}

ast::ClockDeclaration Parser::parse_clock() {
  const SourceSpan start = previous().span;
  ast::ClockDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Assign, "Expected '=' after clock name."));
  declaration.frequency = parse_expression();
  static_cast<void>(expect(TokenKind::Semicolon,
                           "Expected ';' after clock declaration."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::RecordDeclaration Parser::parse_record() {
  const SourceSpan start = previous().span;
  ast::RecordDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after record name."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    const std::size_t before = current_;
    const SourceSpan field_start = current().span;
    ast::FieldDeclaration field;
    field.name = parse_name();
    static_cast<void>(expect(TokenKind::Colon, "Expected ':' after record field name."));
    field.type_name = parse_type_name();
    static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after record field."));
    field.span = joined_span(field_start, previous().span);
    declaration.fields.push_back(std::move(field));
    if (before == current_) {
      static_cast<void>(advance());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after record fields."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::EnumDeclaration Parser::parse_enum() {
  const SourceSpan start = previous().span;
  ast::EnumDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after enum name."));
  if (!check(TokenKind::RightBrace)) {
    declaration.variants.push_back(parse_name());
    while (match(TokenKind::Comma)) {
      if (check(TokenKind::RightBrace)) break;
      declaration.variants.push_back(parse_name());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after enum variants."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::QueueDeclaration Parser::parse_queue() {
  const SourceSpan start = previous().span;
  ast::QueueDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Colon, "Expected ':' after queue name."));
  declaration.element_type = parse_type_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after queue type."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    if (match(TokenKind::KwCapacity)) {
      ++declaration.capacity_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after capacity."));
      declaration.capacity = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after capacity."));
    } else if (match(TokenKind::KwDiscipline)) {
      ++declaration.discipline_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after discipline."));
      if (match(TokenKind::KwFifo) || match(TokenKind::KwPriority) ||
          match(TokenKind::KwRoundRobin)) {
        declaration.discipline = previous().lexeme;
      } else {
        error("MASL-E2001", "Expected fifo, priority, or round_robin queue discipline.",
              current().span);
        if (!at_end()) static_cast<void>(advance());
      }
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after discipline."));
    } else if (match(TokenKind::KwOverflow)) {
      ++declaration.overflow_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after overflow."));
      if (match(TokenKind::KwBlock) || match(TokenKind::KwReject) ||
          match(TokenKind::KwDrop)) {
        declaration.overflow = previous().lexeme;
      } else {
        error("MASL-E2001", "Expected block, reject, or drop overflow policy.",
              current().span);
        if (!at_end()) static_cast<void>(advance());
      }
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after overflow policy."));
    } else {
      error("MASL-E2001", "Unexpected token in queue declaration.", current().span);
      synchronize();
      if (!at_end() && !check(TokenKind::RightBrace) &&
          current().kind != TokenKind::KwCapacity &&
          current().kind != TokenKind::KwDiscipline &&
          current().kind != TokenKind::KwOverflow) {
        static_cast<void>(advance());
      }
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after queue properties."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::ResourceDeclaration Parser::parse_resource() {
  const SourceSpan start = previous().span;
  ast::ResourceDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after resource name."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    if (match(TokenKind::KwCapacity)) {
      ++declaration.capacity_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after resource capacity."));
      declaration.capacity = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after resource capacity."));
    } else if (match(TokenKind::KwUnit)) {
      ++declaration.unit_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after resource unit."));
      if (match(TokenKind::KwSlot)) {
        declaration.unit = previous().lexeme;
      } else {
        declaration.unit = parse_name();
      }
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after resource unit."));
    } else {
      error("MASL-E2001", "Unexpected token in resource declaration.", current().span);
      synchronize();
      if (!at_end() && !check(TokenKind::RightBrace) &&
          !check(TokenKind::KwCapacity) && !check(TokenKind::KwUnit)) {
        static_cast<void>(advance());
      }
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after resource properties."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::PipelineDeclaration Parser::parse_pipeline() {
  const SourceSpan start = previous().span;
  ast::PipelineDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Colon, "Expected ':' after pipeline name."));
  declaration.input_type = parse_type_name();
  static_cast<void>(expect(TokenKind::Arrow, "Expected '->' between pipeline types."));
  declaration.output_type = parse_type_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after pipeline signature."));

  while (!at_end() && !check(TokenKind::RightBrace)) {
    if (match(TokenKind::KwInput)) {
      ++declaration.input_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after pipeline input."));
      declaration.input_queue = parse_name();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after pipeline input."));
    } else if (match(TokenKind::KwOutput)) {
      ++declaration.output_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after pipeline output."));
      declaration.output_queue = parse_name();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after pipeline output."));
    } else if (match(TokenKind::KwClock)) {
      ++declaration.clock_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after pipeline clock."));
      declaration.clock = parse_name();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after pipeline clock."));
    } else if (match(TokenKind::KwLatency)) {
      ++declaration.latency_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after latency."));
      declaration.latency = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after latency."));
    } else if (match(TokenKind::KwInitiationInterval)) {
      ++declaration.initiation_interval_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after initiation_interval."));
      declaration.initiation_interval = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon,
                               "Expected ';' after initiation_interval."));
    } else if (match(TokenKind::KwMaxInFlight)) {
      ++declaration.max_in_flight_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after max_in_flight."));
      declaration.max_in_flight = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after max_in_flight."));
    } else if (match(TokenKind::KwOnStart)) {
      ++declaration.on_start_count;
      static_cast<void>(expect(TokenKind::LeftParen, "Expected '(' after on_start."));
      declaration.handler_parameter = parse_name();
      static_cast<void>(expect(TokenKind::RightParen, "Expected ')' after handler parameter."));
      declaration.on_start = parse_block();
    } else {
      error("MASL-E2001", "Unexpected token in pipeline declaration.", current().span);
      synchronize();
      if (!at_end() && !check(TokenKind::RightBrace)) static_cast<void>(advance());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after pipeline declaration."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::ComponentDeclaration Parser::parse_component() {
  const SourceSpan start = previous().span;
  ast::ComponentDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after component name."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    const std::size_t before = current_;
    auto member = parse_component_member();
    if (member.has_value()) {
      declaration.members.push_back(std::move(*member));
    }
    if (before == current_) {
      error("MASL-E2001", "Parser made no progress while reading component member.",
            current().span);
      static_cast<void>(advance());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after component."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

std::optional<ast::ComponentMember> Parser::parse_component_member() {
  if (match(TokenKind::KwParam)) return parse_parameter();
  if (match(TokenKind::KwInput)) return parse_port(true);
  if (match(TokenKind::KwOutput)) return parse_port(false);
  if (match(TokenKind::KwQueue)) return parse_queue();
  if (match(TokenKind::KwResource)) return parse_resource();
  if (match(TokenKind::KwPipeline)) return parse_pipeline();
  if (match(TokenKind::KwInstances)) return parse_instance();
  if (match(TokenKind::KwConnect)) return parse_connection();
  error("MASL-E2001", "Unexpected token in component declaration.", current().span);
  synchronize();
  return std::nullopt;
}

ast::ParameterDeclaration Parser::parse_parameter() {
  const SourceSpan start = previous().span;
  ast::ParameterDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Colon, "Expected ':' after parameter name."));
  declaration.type_name = parse_type_name();
  static_cast<void>(expect(TokenKind::Assign, "Expected '=' before parameter default."));
  declaration.default_value = parse_expression();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after parameter declaration."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::PortDeclaration Parser::parse_port(bool is_input) {
  const SourceSpan start = previous().span;
  ast::PortDeclaration declaration;
  declaration.is_input = is_input;
  static_cast<void>(expect(TokenKind::KwPort, "Expected 'port' after input/output."));
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Colon, "Expected ':' after port name."));
  declaration.type_name = parse_type_name();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after port declaration."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::ConnectionDeclaration Parser::parse_connection() {
  const SourceSpan start = previous().span;
  ast::ConnectionDeclaration declaration;
  declaration.source = parse_endpoint();
  static_cast<void>(expect(TokenKind::Arrow, "Expected '->' in connection."));
  declaration.target = parse_endpoint();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after connection."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::InstanceDeclaration Parser::parse_instance() {
  const SourceSpan start = previous().span;
  ast::InstanceDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::Colon, "Expected ':' after instance-array name."));
  declaration.component_type = parse_type_name();
  static_cast<void>(expect(TokenKind::LeftBracket, "Expected '[' before instance count."));
  declaration.count = parse_expression();
  static_cast<void>(expect(TokenKind::RightBracket, "Expected ']' after instance count."));
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' before instance parameters."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    declaration.parameters.push_back(parse_assignment());
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after instance parameters."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::WorkloadDeclaration Parser::parse_workload() {
  const SourceSpan start = previous().span;
  ast::WorkloadDeclaration declaration;
  declaration.name = parse_name();
  declaration.body = parse_block();
  declaration.span = joined_span(start, declaration.body.span);
  return declaration;
}

ast::ExperimentDeclaration Parser::parse_experiment() {
  const SourceSpan start = previous().span;
  ast::ExperimentDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' after experiment name."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    if (match(TokenKind::KwWorkload)) {
      ++declaration.workload_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after workload property."));
      declaration.workload = parse_name();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after workload property."));
    } else if (match(TokenKind::KwMode)) {
      ++declaration.mode_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after mode property."));
      if (match(TokenKind::KwCycle) || match(TokenKind::KwEvent)) {
        declaration.mode = previous().lexeme;
      } else {
        error("MASL-E2001", "Expected cycle or event simulation mode.", current().span);
        if (!at_end()) static_cast<void>(advance());
      }
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after mode property."));
    } else if (match(TokenKind::KwStopWhen)) {
      ++declaration.stop_when_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after stop_when."));
      if (match(TokenKind::KwIdle) || match(TokenKind::KwMaxCycles)) {
        declaration.stop_when = previous().lexeme;
      } else {
        error("MASL-E2001", "Expected idle or max_cycles stop condition.", current().span);
        if (!at_end()) static_cast<void>(advance());
      }
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after stop_when."));
    } else if (match(TokenKind::KwMaxCycles)) {
      ++declaration.max_cycles_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after max_cycles."));
      declaration.max_cycles = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after max_cycles."));
    } else if (match(TokenKind::KwMaxEvents)) {
      ++declaration.max_events_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after max_events."));
      declaration.max_events = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after max_events."));
    } else if (match(TokenKind::KwSeed)) {
      ++declaration.seed_count;
      static_cast<void>(expect(TokenKind::Assign, "Expected '=' after seed."));
      declaration.seed = parse_expression();
      static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after seed."));
    } else if (match(TokenKind::KwExpect)) {
      declaration.expectations.push_back(parse_expectation());
    } else if (match(TokenKind::KwExport)) {
      declaration.exports.push_back(parse_export());
    } else {
      error("MASL-E2001", "Unexpected token in experiment declaration.", current().span);
      synchronize();
      if (!at_end() && !check(TokenKind::RightBrace)) static_cast<void>(advance());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after experiment."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::Expectation Parser::parse_expectation() {
  const SourceSpan start = previous().span;
  ast::Expectation expectation;
  // Keep comparison operators for the expectation delimiter; arithmetic
  // subexpressions on either side still use the ordinary Pratt precedence.
  expectation.left = parse_expression(5);
  if (is_comparison_operator()) {
    expectation.op = advance().kind;
  } else {
    error("MASL-E2001", "Expected a comparison operator in expectation.", current().span);
  }
  expectation.right = parse_expression();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after expectation."));
  expectation.span = joined_span(start, previous().span);
  return expectation;
}

ast::ExportDeclaration Parser::parse_export() {
  const SourceSpan start = previous().span;
  ast::ExportDeclaration declaration;
  if (match(TokenKind::KwTrace) || match(TokenKind::KwStats) ||
      match(TokenKind::KwCsv)) {
    declaration.kind = previous().lexeme;
  } else {
    error("MASL-E2001", "Expected trace, stats, or csv export kind.", current().span);
    if (!at_end()) static_cast<void>(advance());
  }
  static_cast<void>(expect(TokenKind::Assign, "Expected '=' after export kind."));
  if (check(TokenKind::String)) {
    declaration.path = decode_string_literal(advance().lexeme);
  } else {
    error("MASL-E2002", "Expected string path for export.", current().span);
  }
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after export declaration."));
  declaration.span = joined_span(start, previous().span);
  return declaration;
}

ast::FunctionDeclaration Parser::parse_function() {
  const SourceSpan start = previous().span;
  ast::FunctionDeclaration declaration;
  declaration.name = parse_name();
  static_cast<void>(expect(TokenKind::LeftParen, "Expected '(' after function name."));
  if (!check(TokenKind::RightParen)) {
    do {
      const SourceSpan parameter_start = current().span;
      ast::FunctionParameter parameter;
      parameter.name = parse_name();
      static_cast<void>(expect(TokenKind::Colon, "Expected ':' after parameter name."));
      parameter.type_name = parse_type_name();
      parameter.span = joined_span(parameter_start, previous().span);
      declaration.parameters.push_back(std::move(parameter));
    } while (match(TokenKind::Comma));
  }
  static_cast<void>(expect(TokenKind::RightParen, "Expected ')' after function parameters."));
  static_cast<void>(expect(TokenKind::Arrow, "Expected '->' before function return type."));
  declaration.return_type = parse_type_name();
  declaration.body = parse_block();
  declaration.span = joined_span(start, declaration.body.span);
  return declaration;
}

ast::Block Parser::parse_block() {
  const SourceSpan start = current().span;
  ast::Block block;
  static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' to begin block."));
  while (!at_end() && !check(TokenKind::RightBrace)) {
    const std::size_t before = current_;
    auto statement = parse_statement();
    if (statement != nullptr) {
      block.statements.push_back(std::move(statement));
    }
    if (before == current_) {
      error("MASL-E2001", "Parser made no progress in statement block.", current().span);
      static_cast<void>(advance());
    }
  }
  static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' to close block."));
  block.span = joined_span(start, previous().span);
  return block;
}

ast::StatementPtr Parser::parse_statement() {
  const SourceSpan start = current().span;
  if (match(TokenKind::KwLet)) {
    ast::LetStatement statement;
    statement.name = parse_name();
    static_cast<void>(expect(TokenKind::Colon, "Expected ':' after local name."));
    statement.type_name = parse_type_name();
    static_cast<void>(expect(TokenKind::Assign, "Expected '=' in let declaration."));
    statement.value = parse_expression();
    static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after let declaration."));
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  if (match(TokenKind::KwSet)) {
    ast::SetStatement statement;
    statement.assignment = parse_assignment();
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  if (match(TokenKind::KwIf)) {
    ast::IfStatement statement;
    statement.condition = parse_expression();
    statement.then_block = parse_block();
    if (match(TokenKind::KwElse)) {
      if (check(TokenKind::KwIf)) {
        auto nested = parse_statement();
        ast::Block else_block;
        else_block.span = nested != nullptr ? nested->span : previous().span;
        if (nested != nullptr) else_block.statements.push_back(std::move(nested));
        statement.else_block = std::move(else_block);
      } else {
        statement.else_block = parse_block();
      }
    }
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  if (match(TokenKind::KwRepeat)) {
    ast::RepeatStatement statement;
    statement.count = parse_expression();
    static_cast<void>(expect(TokenKind::KwAs, "Expected 'as' after repeat count."));
    statement.index_name = parse_name();
    statement.body = parse_block();
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, statement.body.span), std::move(statement)});
  }

  if (match(TokenKind::KwSubmit)) {
    ast::SubmitStatement statement;
    statement.queue_name = parse_name();
    static_cast<void>(expect(TokenKind::Comma, "Expected ',' after target queue."));
    statement.record_type = parse_type_name();
    static_cast<void>(expect(TokenKind::LeftBrace, "Expected '{' before submitted record fields."));
    while (!at_end() && !check(TokenKind::RightBrace)) {
      statement.fields.push_back(parse_assignment());
    }
    static_cast<void>(expect(TokenKind::RightBrace, "Expected '}' after submitted record fields."));
    static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after submit statement."));
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  if (match(TokenKind::KwAssert)) {
    ast::AssertStatement statement;
    statement.condition = parse_expression();
    static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after assert."));
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  if (match(TokenKind::KwReturn)) {
    ast::ReturnStatement statement;
    if (!check(TokenKind::Semicolon)) {
      statement.value = parse_expression();
    }
    static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after return."));
    return std::make_unique<ast::Statement>(ast::Statement{
        joined_span(start, previous().span), std::move(statement)});
  }

  ast::ExpressionStatement statement;
  statement.expression = parse_expression();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after expression."));
  return std::make_unique<ast::Statement>(ast::Statement{
      joined_span(start, previous().span), std::move(statement)});
}

ast::Assignment Parser::parse_assignment() {
  const SourceSpan start = current().span;
  ast::Assignment assignment;
  assignment.target = parse_name();
  while (match(TokenKind::Dot)) {
    assignment.target += "." + parse_name();
  }
  static_cast<void>(expect(TokenKind::Assign, "Expected '=' in assignment."));
  assignment.value = parse_expression();
  static_cast<void>(expect(TokenKind::Semicolon, "Expected ';' after assignment."));
  assignment.span = joined_span(start, previous().span);
  return assignment;
}

std::string Parser::parse_endpoint() {
  std::string endpoint = parse_name();
  while (match(TokenKind::Dot)) {
    endpoint += "." + parse_name();
  }
  return endpoint;
}

ast::ExprPtr Parser::parse_expression(int min_precedence) {
  ast::ExprPtr expression = parse_prefix();
  while (true) {
    if (check(TokenKind::LeftParen) && 9 >= min_precedence) {
      const SourceSpan start = expression->span;
      static_cast<void>(advance());
      ast::CallExpr call;
      call.callee = std::move(expression);
      if (!check(TokenKind::RightParen)) {
        do {
          call.arguments.push_back(parse_expression());
        } while (match(TokenKind::Comma));
      }
      static_cast<void>(expect(TokenKind::RightParen, "Expected ')' after call arguments."));
      expression = std::make_unique<ast::Expr>(ast::Expr{
          joined_span(start, previous().span), std::move(call)});
      continue;
    }

    if (match(TokenKind::Dot)) {
      const SourceSpan start = expression->span;
      const std::string field = parse_name();
      expression = std::make_unique<ast::Expr>(ast::Expr{
          joined_span(start, previous().span),
          ast::FieldExpr{std::move(expression), field}});
      continue;
    }

    const int op_precedence = precedence(current().kind);
    if (op_precedence < min_precedence || op_precedence == 0) {
      break;
    }
    const TokenKind op = advance().kind;
    ast::ExprPtr right = parse_expression(op_precedence + 1);
    const SourceSpan span = joined_span(expression->span, right->span);
    expression = std::make_unique<ast::Expr>(ast::Expr{
        span, ast::BinaryExpr{op, std::move(expression), std::move(right)}});
  }
  return expression;
}

ast::ExprPtr Parser::parse_prefix() {
  const lexer::Token token = current();
  if (match(TokenKind::Plus) || match(TokenKind::Minus) ||
      match(TokenKind::Bang)) {
    const TokenKind op = previous().kind;
    ast::ExprPtr operand = parse_expression(8);
    return std::make_unique<ast::Expr>(ast::Expr{
        joined_span(token.span, operand->span),
        ast::UnaryExpr{op, std::move(operand)}});
  }

  if (check(TokenKind::Number)) {
    return parse_unit_or_number();
  }
  if (match(TokenKind::String)) {
    return std::make_unique<ast::Expr>(ast::Expr{
        token.span, ast::LiteralExpr{ast::LiteralExpr::Kind::String,
                                     decode_string_literal(token.lexeme)}});
  }
  if (match(TokenKind::KwTrue) || match(TokenKind::KwFalse)) {
    return std::make_unique<ast::Expr>(ast::Expr{
        token.span, ast::LiteralExpr{ast::LiteralExpr::Kind::Boolean,
                                     token.lexeme}});
  }
  if (match(TokenKind::Identifier) || match(TokenKind::KwOutput) ||
      match(TokenKind::KwCompleted)) {
    return std::make_unique<ast::Expr>(ast::Expr{
        token.span, ast::NameExpr{token.lexeme}});
  }
  if (match(TokenKind::LeftParen)) {
    ast::ExprPtr expression = parse_expression();
    static_cast<void>(expect(TokenKind::RightParen, "Expected ')' after expression."));
    expression->span = joined_span(token.span, previous().span);
    return expression;
  }

  error("MASL-E2001", "Expected an expression; found '" + token.lexeme + "'.",
        token.span);
  if (!at_end() && !check(TokenKind::Semicolon) &&
      !check(TokenKind::RightParen) && !check(TokenKind::RightBrace) &&
      !check(TokenKind::Comma) && !check(TokenKind::KwAs)) {
    static_cast<void>(advance());
  }
  return make_error_expr(token.span);
}

ast::ExprPtr Parser::parse_unit_or_number() {
  const lexer::Token number = advance();
  if (!match(TokenKind::Unit)) {
    return std::make_unique<ast::Expr>(ast::Expr{
        number.span, ast::LiteralExpr{ast::LiteralExpr::Kind::Number,
                                      number.lexeme}});
  }

  const lexer::Token unit = previous();
  std::string clock_domain;
  if (unit.lexeme == "cycles" || unit.lexeme == "bytes_per_cycle") {
    static_cast<void>(expect(TokenKind::LeftParen,
                             "Expected '(' after clock-relative unit."));
    clock_domain = parse_name();
    static_cast<void>(expect(TokenKind::RightParen,
                             "Expected ')' after clock domain."));
  }
  return std::make_unique<ast::Expr>(ast::Expr{
      joined_span(number.span, previous().span),
      ast::UnitExpr{number.lexeme, unit.lexeme, std::move(clock_domain)}});
}

ast::ExprPtr Parser::make_error_expr(SourceSpan span) {
  return std::make_unique<ast::Expr>(ast::Expr{span, ast::ErrorExpr{}});
}

int Parser::precedence(TokenKind kind) const noexcept {
  switch (kind) {
    case TokenKind::OrOr: return 1;
    case TokenKind::AndAnd: return 2;
    case TokenKind::Equal:
    case TokenKind::NotEqual: return 3;
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual: return 4;
    case TokenKind::Plus:
    case TokenKind::Minus: return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent: return 6;
    default: return 0;
  }
}

}  // namespace masl::parser
