#include "parser.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

namespace graphjit::frontend {
namespace {

[[nodiscard]] std::string describeActualToken(const Token& token) {
    std::string description(tokenTypeName(token.type));
    if (!token.lexeme.empty()) {
        description += " '";
        description += token.lexeme;
        description += '\'';
    }
    return description;
}

[[nodiscard]] std::string makeUnexpectedTokenMessage(
    const Token& actualToken, const std::vector<TokenType>& expectedTokenTypes) {
    std::ostringstream message;
    message << "Parse error at line " << actualToken.line << ", column "
            << actualToken.column << ": expected ";

    for (std::size_t index = 0; index < expectedTokenTypes.size(); ++index) {
        if (index != 0) {
            message << (index + 1 == expectedTokenTypes.size() ? " or " : ", ");
        }
        message << tokenTypeName(expectedTokenTypes[index]);
    }

    message << ", found " << describeActualToken(actualToken);
    return message.str();
}

[[nodiscard]] std::string makeDetailedMessage(
    const Token& actualToken, const std::string_view detail) {
    std::ostringstream message;
    message << "Parse error at line " << actualToken.line << ", column "
            << actualToken.column << ": " << detail << "; found "
            << describeActualToken(actualToken);
    return message.str();
}

} // namespace

ParseException::ParseException(
    Token actualToken, std::vector<TokenType> expectedTokenTypes)
    : std::runtime_error(makeUnexpectedTokenMessage(actualToken, expectedTokenTypes)),
      actualToken_(std::move(actualToken)),
      expectedTokenTypes_(std::move(expectedTokenTypes)) {}

ParseException::ParseException(Token actualToken, const std::string_view detail)
    : std::runtime_error(makeDetailedMessage(actualToken, detail)),
      actualToken_(std::move(actualToken)) {}

const Token& ParseException::actualToken() const noexcept {
    return actualToken_;
}

const std::vector<TokenType>& ParseException::expectedTokenTypes() const noexcept {
    return expectedTokenTypes_;
}

int ParseException::line() const noexcept {
    return actualToken_.line;
}

int ParseException::column() const noexcept {
    return actualToken_.column;
}

Parser::Parser(Lexer lexer)
    : lexer_(std::move(lexer)), currentToken_(lexer_.nextToken()) {}

void Parser::advance() {
    currentToken_ = lexer_.nextToken();
}

void Parser::consume(const TokenType expected) {
    if (currentToken_.type != expected) {
        throwUnexpected({expected});
    }
    advance();
}

void Parser::throwUnexpected(
    const std::initializer_list<TokenType> expectedTokenTypes) const {
    throw ParseException(
        currentToken_, std::vector<TokenType>(expectedTokenTypes));
}

std::unique_ptr<ast::ASTNode> Parser::parse() {
    auto expression = parseExpression();
    consume(TokenType::EndOfFile);
    return expression;
}

std::unique_ptr<ast::ASTNode> Parser::parseExpression() {
    auto lhs = parseTerm();

    while (currentToken_.type == TokenType::Plus ||
           currentToken_.type == TokenType::Minus) {
        const TokenType operatorType = currentToken_.type;
        advance();
        auto rhs = parseTerm();
        lhs = std::make_unique<ast::BinaryOpNode>(
            operatorType, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

std::unique_ptr<ast::ASTNode> Parser::parseTerm() {
    auto lhs = parseMatMul();

    while (currentToken_.type == TokenType::Star ||
           currentToken_.type == TokenType::Slash) {
        const TokenType operatorType = currentToken_.type;
        advance();
        auto rhs = parseMatMul();
        lhs = std::make_unique<ast::BinaryOpNode>(
            operatorType, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

std::unique_ptr<ast::ASTNode> Parser::parseMatMul() {
    auto lhs = parseFactor();

    while (currentToken_.type == TokenType::MatrixMultiply) {
        const TokenType operatorType = currentToken_.type;
        advance();
        auto rhs = parseFactor();
        lhs = std::make_unique<ast::BinaryOpNode>(
            operatorType, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

std::unique_ptr<ast::ASTNode> Parser::parseFactor() {
    if (currentToken_.type != TokenType::LeftParen) {
        return parsePrimary();
    }

    advance();
    auto expression = parseExpression();
    consume(TokenType::RightParen);
    return expression;
}

std::unique_ptr<ast::ASTNode> Parser::parsePrimary() {
    if (currentToken_.type == TokenType::Identifier) {
        std::string name = std::move(currentToken_.lexeme);
        advance();
        return std::make_unique<ast::VariableNode>(std::move(name));
    }

    if (currentToken_.type == TokenType::FloatLiteral) {
        // libc++ versions that otherwise support C++20 may not provide the
        // floating-point from_chars overload. A stream imbued with the classic
        // locale is portable and still guarantees '.' as the decimal separator.
        std::istringstream input(currentToken_.lexeme);
        input.imbue(std::locale::classic());
        double value = 0.0;
        input >> std::noskipws >> value;
        if (!input || input.peek() != std::char_traits<char>::eof()) {
            throw ParseException(
                currentToken_, "floating-point literal is outside the range of double");
        }

        advance();
        return std::make_unique<ast::FloatLiteralNode>(value);
    }

    // LeftParen is included because it is also valid at this grammar position;
    // parseFactor consumes it before delegating here.
    throwUnexpected(
        {TokenType::LeftParen, TokenType::FloatLiteral, TokenType::Identifier});
}

} // namespace graphjit::frontend
