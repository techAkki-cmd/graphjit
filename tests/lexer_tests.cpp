#include "lexer.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using graphjit::frontend::Lexer;
using graphjit::frontend::LexerError;
using graphjit::frontend::Token;
using graphjit::frontend::TokenType;

struct ExpectedToken {
    TokenType type;
    std::string_view lexeme;
    int line;
    int column;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expectToken(const Token& actual, const ExpectedToken& expected) {
    expect(actual.type == expected.type, "unexpected token type");
    expect(actual.lexeme == expected.lexeme, "unexpected token lexeme: " + actual.lexeme);
    expect(actual.line == expected.line, "unexpected token line");
    expect(actual.column == expected.column, "unexpected token column");
}

std::vector<Token> lexAll(const std::string_view source) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    do {
        tokens.push_back(lexer.nextToken());
    } while (tokens.back().type != TokenType::EndOfFile);
    return tokens;
}

void testSampleProgram() {
    constexpr std::string_view source =
        "def compute(A: tensor, B: tensor) { return A @ B + 2.0; }";
    const std::vector<ExpectedToken> expected{
        {TokenType::Def, "def", 1, 1},
        {TokenType::Identifier, "compute", 1, 5},
        {TokenType::LeftParen, "(", 1, 12},
        {TokenType::Identifier, "A", 1, 13},
        {TokenType::Colon, ":", 1, 14},
        {TokenType::Tensor, "tensor", 1, 16},
        {TokenType::Comma, ",", 1, 22},
        {TokenType::Identifier, "B", 1, 24},
        {TokenType::Colon, ":", 1, 25},
        {TokenType::Tensor, "tensor", 1, 27},
        {TokenType::RightParen, ")", 1, 33},
        {TokenType::LeftBrace, "{", 1, 35},
        {TokenType::Return, "return", 1, 37},
        {TokenType::Identifier, "A", 1, 44},
        {TokenType::MatrixMultiply, "@", 1, 46},
        {TokenType::Identifier, "B", 1, 48},
        {TokenType::Plus, "+", 1, 50},
        {TokenType::FloatLiteral, "2.0", 1, 52},
        {TokenType::Semicolon, ";", 1, 55},
        {TokenType::RightBrace, "}", 1, 57},
        {TokenType::EndOfFile, "", 1, 58},
    };

    const auto actual = lexAll(source);
    expect(actual.size() == expected.size(), "unexpected sample token count");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expectToken(actual[index], expected[index]);
    }
}

void testOperatorsAndPunctuation() {
    constexpr std::string_view source = "+ - * / @ = ( ) { } , ; :";
    const std::vector<TokenType> expected{
        TokenType::Plus,           TokenType::Minus,      TokenType::Star,
        TokenType::Slash,          TokenType::MatrixMultiply,
        TokenType::Equal,          TokenType::LeftParen,  TokenType::RightParen,
        TokenType::LeftBrace,      TokenType::RightBrace, TokenType::Comma,
        TokenType::Semicolon,      TokenType::Colon,      TokenType::EndOfFile,
    };

    const auto actual = lexAll(source);
    expect(actual.size() == expected.size(), "unexpected punctuation token count");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(actual[index].type == expected[index], "unexpected punctuation token type");
    }
}

void testIdentifiersAndKeywords() {
    const auto tokens = lexAll("def tensor return define tensor2 return_value _x A1");
    const std::vector<TokenType> expected{
        TokenType::Def,        TokenType::Tensor,     TokenType::Return,
        TokenType::Identifier, TokenType::Identifier, TokenType::Identifier,
        TokenType::Identifier, TokenType::Identifier, TokenType::EndOfFile,
    };
    expect(tokens.size() == expected.size(), "unexpected identifier token count");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(tokens[index].type == expected[index], "unexpected identifier token type");
    }
}

void testNumbersAndSeparateSigns() {
    const auto tokens = lexAll("0 42 2.0 1. .5 1e3 2.5E-4 -7 +8");
    const std::vector<std::pair<TokenType, std::string_view>> expected{
        {TokenType::IntLiteral, "0"},       {TokenType::IntLiteral, "42"},
        {TokenType::FloatLiteral, "2.0"},   {TokenType::FloatLiteral, "1."},
        {TokenType::FloatLiteral, ".5"},    {TokenType::FloatLiteral, "1e3"},
        {TokenType::FloatLiteral, "2.5E-4"}, {TokenType::Minus, "-"},
        {TokenType::IntLiteral, "7"},       {TokenType::Plus, "+"},
        {TokenType::IntLiteral, "8"},       {TokenType::EndOfFile, ""},
    };
    expect(tokens.size() == expected.size(), "unexpected numeric token count");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(tokens[index].type == expected[index].first, "unexpected numeric token type");
        expect(tokens[index].lexeme == expected[index].second, "unexpected numeric lexeme");
    }
}

void testWhitespaceCommentsAndCrLf() {
    Lexer lexer("def\tfoo # trailing\r\n\n  return#eof");
    expectToken(lexer.nextToken(), {TokenType::Def, "def", 1, 1});
    expectToken(lexer.nextToken(), {TokenType::Identifier, "foo", 1, 5});
    expectToken(lexer.nextToken(), {TokenType::Return, "return", 3, 3});
    expectToken(lexer.nextToken(), {TokenType::EndOfFile, "", 3, 13});
}

void expectError(
    const std::string_view source,
    const char offending,
    const int line,
    const int column) {
    try {
        static_cast<void>(lexAll(source));
        fail("expected LexerError");
    } catch (const LexerError& error) {
        expect(error.offendingCharacter() == offending, "unexpected offending character");
        expect(error.line() == line, "unexpected error line");
        expect(error.column() == column, "unexpected error column");
        expect(std::string_view(error.what()).find("line") != std::string_view::npos,
               "diagnostic lacks source context");
    }
}

void testErrors() {
    expectError("$", '$', 1, 1);
    expectError("\n .", '.', 2, 2);
    expectError("1e", 'e', 1, 2);
    expectError("2.5E+", 'E', 1, 4);

    const std::string nonAscii(1, static_cast<char>(0xC3));
    expectError(nonAscii, static_cast<char>(0xC3), 1, 1);
}

void testStableEndOfFile() {
    Lexer lexer("");
    const Token first = lexer.nextToken();
    const Token second = lexer.nextToken();
    expectToken(first, {TokenType::EndOfFile, "", 1, 1});
    expectToken(second, {TokenType::EndOfFile, "", 1, 1});
}

} // namespace

int main() {
    try {
        testSampleProgram();
        testOperatorsAndPunctuation();
        testIdentifiersAndKeywords();
        testNumbersAndSeparateSigns();
        testWhitespaceCommentsAndCrLf();
        testErrors();
        testStableEndOfFile();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All lexer tests passed.\n";
    return EXIT_SUCCESS;
}
