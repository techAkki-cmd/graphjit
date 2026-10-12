#include "ast_printer.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using graphjit::ast::ASTPrinter;
using graphjit::frontend::Lexer;
using graphjit::frontend::LexerError;
using graphjit::frontend::ParseException;
using graphjit::frontend::Parser;
using graphjit::frontend::TokenType;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] std::string parseAndPrint(const std::string_view source) {
    Parser parser{Lexer(source)};
    auto expression = parser.parse();
    std::ostringstream output;
    ASTPrinter printer(output);
    expression->accept(printer);
    return output.str();
}

void testDemoExpression() {
    constexpr std::string_view expected =
        "BinaryOpNode: +\n"
        "  Left: BinaryOpNode: @\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: BinaryOpNode: *\n"
        "    Left: FloatLiteralNode: 2.0\n"
        "    Right: BinaryOpNode: -\n"
        "      Left: VariableNode: C\n"
        "      Right: VariableNode: D\n";
    expect(parseAndPrint("A @ B + 2.0 * (C - D)") == expected,
           "demo expression produced the wrong tree");
}

void testPrecedence() {
    constexpr std::string_view expected =
        "BinaryOpNode: +\n"
        "  Left: VariableNode: A\n"
        "  Right: BinaryOpNode: *\n"
        "    Left: VariableNode: B\n"
        "    Right: BinaryOpNode: @\n"
        "      Left: VariableNode: C\n"
        "      Right: VariableNode: D\n";
    expect(parseAndPrint("A + B * C @ D") == expected,
           "operator precedence is incorrect");

    constexpr std::string_view parenthesized =
        "BinaryOpNode: *\n"
        "  Left: BinaryOpNode: +\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: VariableNode: C\n";
    expect(parseAndPrint("(A + B) * C") == parenthesized,
           "parentheses did not override precedence");

    constexpr std::string_view division =
        "BinaryOpNode: /\n"
        "  Left: BinaryOpNode: @\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: VariableNode: C\n";
    expect(parseAndPrint("A @ B / C") == division,
           "matrix multiplication should bind more tightly than division");
}

void testLeftAssociativity() {
    constexpr std::string_view expected =
        "BinaryOpNode: -\n"
        "  Left: BinaryOpNode: -\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: VariableNode: C\n";
    expect(parseAndPrint("A - B - C") == expected,
           "subtraction is not left associative");

    constexpr std::string_view matrixExpected =
        "BinaryOpNode: @\n"
        "  Left: BinaryOpNode: @\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: VariableNode: C\n";
    expect(parseAndPrint("A @ B @ C") == matrixExpected,
           "matrix multiplication is not left associative");
}

void testPrimaryExpressions() {
    expect(parseAndPrint("value") == "VariableNode: value\n",
           "identifier primary was not parsed");
    expect(parseAndPrint(".5") == "FloatLiteralNode: 0.5\n",
           "leading-dot float was not parsed");
    expect(parseAndPrint("1.") == "FloatLiteralNode: 1.0\n",
           "trailing-dot float was not parsed");
    expect(parseAndPrint("1e3") == "FloatLiteralNode: 1000.0\n",
           "exponent float was not parsed");
}

void expectParseError(
    const std::string_view source,
    const TokenType actualType,
    const int line,
    const int column,
    const std::vector<TokenType>& expectedTypes) {
    try {
        Parser parser{Lexer(source)};
        static_cast<void>(parser.parse());
        fail("expected ParseException");
    } catch (const ParseException& error) {
        expect(error.actualToken().type == actualType, "incorrect actual token type");
        expect(error.line() == line, "incorrect parse error line");
        expect(error.column() == column, "incorrect parse error column");
        expect(error.expectedTokenTypes() == expectedTypes,
               "incorrect expected-token set");
        expect(std::string_view(error.what()).find("expected") != std::string_view::npos,
               "parse diagnostic does not describe the expectation");
    }
}

void testSyntaxErrors() {
    const std::vector<TokenType> primaryExpected{
        TokenType::LeftParen, TokenType::FloatLiteral, TokenType::Identifier};

    expectParseError("", TokenType::EndOfFile, 1, 1, primaryExpected);
    expectParseError("A +", TokenType::EndOfFile, 1, 4, primaryExpected);
    expectParseError("()", TokenType::RightParen, 1, 2, primaryExpected);
    expectParseError("+ A", TokenType::Plus, 1, 1, primaryExpected);
    expectParseError("42", TokenType::IntLiteral, 1, 1, primaryExpected);
    expectParseError("A = B", TokenType::Equal, 1, 3, {TokenType::EndOfFile});
    expectParseError("A B", TokenType::Identifier, 1, 3, {TokenType::EndOfFile});
    expectParseError("\n(A + B", TokenType::EndOfFile, 2, 7,
                     {TokenType::RightParen});
}

void testOutOfRangeFloat() {
    try {
        Parser parser{Lexer("1e9999")};
        static_cast<void>(parser.parse());
        fail("expected out-of-range ParseException");
    } catch (const ParseException& error) {
        expect(error.actualToken().type == TokenType::FloatLiteral,
               "out-of-range error has the wrong token");
        expect(error.line() == 1 && error.column() == 1,
               "out-of-range error has the wrong position");
        expect(error.expectedTokenTypes().empty(),
               "conversion errors should not contain expected token kinds");
        expect(std::string_view(error.what()).find("outside the range") !=
                   std::string_view::npos,
               "out-of-range diagnostic is unclear");
    }
}

void testLexerErrorsPropagate() {
    try {
        Parser parser{Lexer("A + $")};
        static_cast<void>(parser.parse());
        fail("expected LexerError");
    } catch (const LexerError& error) {
        expect(error.line() == 1 && error.column() == 5,
               "lexer error position changed during parsing");
    }
}

void testCommentsAndWhitespace() {
    constexpr std::string_view expected =
        "BinaryOpNode: +\n"
        "  Left: VariableNode: A\n"
        "  Right: VariableNode: B\n";
    expect(parseAndPrint(" A # first operand\r\n + B # done") == expected,
           "lexer/parser whitespace integration failed");
}

} // namespace

int main() {
    try {
        testDemoExpression();
        testPrecedence();
        testLeftAssociativity();
        testPrimaryExpressions();
        testSyntaxErrors();
        testOutOfRangeFloat();
        testLexerErrorsPropagate();
        testCommentsAndWhitespace();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All parser tests passed.\n";
    return EXIT_SUCCESS;
}
