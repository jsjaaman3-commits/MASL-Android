#include "masl/lexer/lexer.hpp"
#include "masl/parser/parser.hpp"
#include "masl/semantic/analyzer.hpp"
#include "masl/ir/ir.hpp"
#include "masl/bytecode/bytecode.hpp"
#include "masl/version.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::string read_source(std::ifstream& input) {
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

bool build_bytecode_file(const std::string& source_path, const std::string& output_path) {
  std::ifstream input(source_path, std::ios::binary);
  if (!input) { std::cerr << "MASL-E9001: cannot open source file '" << source_path << "'.\n"; return false; }
  const std::string source = read_source(input);
  auto lexed = masl::lexer::Lexer(source).scan();
  if (!lexed.ok()) {
    for (const auto& item : lexed.diagnostics)
      std::cerr << item.code << ": " << source_path << ':' << item.span.start.line << ':' << item.span.start.column << ": " << item.message << '\n';
    return false;
  }
  auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
  if (!parsed.ok()) {
    for (const auto& item : parsed.diagnostics)
      std::cerr << item.code << ": " << source_path << ':' << item.span.start.line << ':' << item.span.start.column << ": " << item.message << '\n';
    return false;
  }
  const auto checked = masl::semantic::analyze(*parsed.program);
  if (!checked.ok()) {
    for (const auto& item : checked.diagnostics)
      std::cerr << item.code << ": " << source_path << ':' << item.span.start.line << ':' << item.span.start.column << ": " << item.message << '\n';
    return false;
  }
  auto lowered = masl::ir::lower(*parsed.program, checked);
  if (!lowered.ok()) {
    for (const auto& item : lowered.diagnostics) std::cerr << item.code << ": " << item.message << '\n';
    return false;
  }
  auto compiled = masl::bytecode::compile(*lowered.module);
  if (!compiled.ok()) {
    for (const auto& item : compiled.diagnostics) std::cerr << item.code << ": " << item.message << '\n';
    return false;
  }
  std::vector<masl::bytecode::Diagnostic> diagnostics;
  auto bytes = masl::bytecode::serialize(*compiled.module, diagnostics);
  if (!diagnostics.empty()) {
    for (const auto& item : diagnostics) std::cerr << item.code << ": " << item.message << '\n';
    return false;
  }
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) { std::cerr << "MASL-E9002: cannot create output file '" << output_path << "'.\n"; return false; }
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!output) { std::cerr << "MASL-E9003: failed while writing MASB output.\n"; return false; }
  std::cout << "MASB compiled: " << output_path << " (" << bytes.size() << " bytes, "
            << compiled.module->functions.size() << " function(s)).\n";
  return true;
}

void print_help() {
  std::cout
      << "Usage: maslc <command> [options]\n"
      << "MASL source compiler (foundation build " << masl::core::version_string()
      << ").\n\n"
      << "Available commands:\n"
      << "  --help       Show this help.\n"
      << "  --version    Show compiler version.\n"
      << "  lex <file>   Tokenize a MASL source file and print tokens.\n"
      << "  parse <file> Parse source syntax and print the model summary.\n"
      << "  check <file> Parse and semantically validate a MASL source file.\n"
      << "  ir <file>    Lower a valid source file to normalized Typed IR.\n"
      << "  compile <source> -o <file.masb>  Compile source to verified MASB bytecode.\n"
      << "  disasm <file.masb>  Verify and disassemble MASB bytecode.\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 5 && std::string_view(argv[1]) == "compile" && std::string_view(argv[3]) == "-o") {
    return build_bytecode_file(argv[2], argv[4]) ? 0 : 1;
  }
  if (argc == 3 && std::string_view(argv[1]) == "disasm") {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(argv[2], error);
    if (error || file_size > masl::bytecode::kMaximumFileSize) {
      std::cerr << "MASL-E9004: bytecode file is unavailable or exceeds the 64 MiB limit.\n"; return 2;
    }
    std::ifstream input(argv[2], std::ios::binary);
    if (!input) { std::cerr << "MASL-E9001: cannot open bytecode file.\n"; return 2; }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto loaded = masl::bytecode::load(bytes);
    if (!loaded.ok()) {
      for (const auto& item : loaded.diagnostics) std::cerr << item.code << ": " << item.message << '\n';
      return 1;
    }
    std::cout << masl::bytecode::disassemble(*loaded.module);
    return 0;
  }
  if (argc == 3 && std::string_view(argv[1]) == "ir") {
    const std::string path(argv[2]);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::cerr << "MASL-E9001: cannot open source file '" << path << "'.\n";
      return 2;
    }
    const std::string source = read_source(input);
    auto lexed = masl::lexer::Lexer(source).scan();
    if (!lexed.ok()) {
      for (const auto& diagnostic : lexed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
    if (!parsed.ok()) {
      for (const auto& diagnostic : parsed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    const auto checked = masl::semantic::analyze(*parsed.program);
    if (!checked.ok()) {
      for (const auto& diagnostic : checked.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    auto lowered = masl::ir::lower(*parsed.program, checked);
    if (!lowered.ok()) {
      for (const auto& diagnostic : lowered.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
      return 1;
    }
    std::cout << masl::ir::dump(*lowered.module);
    return 0;
  }
  if (argc == 3 && std::string_view(argv[1]) == "check") {
    const std::string path(argv[2]);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::cerr << "MASL-E9001: cannot open source file '" << path << "'.\n";
      return 2;
    }
    const std::string source = read_source(input);
    auto lexed = masl::lexer::Lexer(source).scan();
    if (!lexed.ok()) {
      for (const auto& diagnostic : lexed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
    if (!parsed.ok()) {
      for (const auto& diagnostic : parsed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    const auto checked = masl::semantic::analyze(*parsed.program);
    if (!checked.ok()) {
      for (const auto& diagnostic : checked.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    std::cout << "Semantic check OK: model " << parsed.program->model.name
              << " (" << parsed.program->model.members.size()
              << " top-level declaration(s)).\n";
    return 0;
  }
  if (argc == 3 && std::string_view(argv[1]) == "parse") {
    const std::string path(argv[2]);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::cerr << "MASL-E9001: cannot open source file '" << path << "'.\n";
      return 2;
    }
    const std::string source = read_source(input);
    auto lexed = masl::lexer::Lexer(source).scan();
    if (!lexed.ok()) {
      for (const auto& diagnostic : lexed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    auto parsed = masl::parser::Parser(std::move(lexed.tokens)).parse();
    if (!parsed.ok()) {
      for (const auto& diagnostic : parsed.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    std::cout << "Syntax OK: model " << parsed.program->model.name << " ("
              << parsed.program->model.members.size()
              << " top-level declaration(s)).\n";
    return 0;
  }

  if (argc == 3 && std::string_view(argv[1]) == "lex") {
    const std::string path(argv[2]);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::cerr << "MASL-E9001: cannot open source file '" << path << "'.\n";
      return 2;
    }
    const std::string source = read_source(input);
    const auto result = masl::lexer::Lexer(source).scan();
    if (!result.ok()) {
      for (const auto& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << path << ':'
                  << diagnostic.span.start.line << ':'
                  << diagnostic.span.start.column << ": "
                  << diagnostic.message << '\n';
      }
      return 1;
    }
    for (const auto& token : result.tokens) {
      if (token.kind == masl::lexer::TokenKind::EndOfFile) {
        continue;
      }
      std::cout << token.span.start.line << ':' << token.span.start.column
                << "  " << masl::lexer::token_kind_name(token.kind)
                << "  `" << token.lexeme << "`\n";
    }
    std::cout << "Lexed " << result.tokens.size() - 1U
              << " token(s) successfully.\n";
    return 0;
  }

  if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
    print_help();
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "MASL Compiler " << masl::core::version_string()
              << " (MASB compiler enabled)\n";
    return 0;
  }

  std::cerr << "maslc: requested command is not implemented in this build; "
               "see --help.\n";
  return 2;
}
