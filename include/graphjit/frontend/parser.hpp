#pragma once

#include "graphjit/ast/ast.hpp"
#include "graphjit/frontend/lexer.hpp"

#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace graphjit::frontend {

/// Reports a syntactic error together with the token that caused it.
class ParseException final : public std::runtime_error {
public:
    ParseException(Token actualToken, std::vector<TokenType> expectedTokenTypes);
    ParseException(Token actualToken, std::string_view detail);

    [[nodiscard]] const Token& actualToken() const noexcept;
    [[nodiscard]] const std::vector<TokenType>& expectedTokenTypes() const noexcept;
    [[nodiscard]] int line() const noexcept;
    [[nodiscard]] int column() const noexcept;

private:
    Token actualToken_;
    std::vector<TokenType> expectedTokenTypes_;
};

/// Parses one complete GraphJIT expression from a lexer token stream.
///
/// Lexer is non-owning, so the source buffer used to construct it must remain
/// valid until parsing has completed.
class Parser final {
public:
    explicit Parser(Lexer lexer);

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) noexcept = default;
    Parser& operator=(Parser&&) noexcept = default;

    [[nodiscard]] std::unique_ptr<ast::ASTNode> parse();

private:
    void advance();
    void consume(TokenType expected);
    [[noreturn]] void throwUnexpected(
        std::initializer_list<TokenType> expectedTokenTypes) const;

    [[nodiscard]] std::unique_ptr<ast::ASTNode> parseExpression();
    [[nodiscard]] std::unique_ptr<ast::ASTNode> parseTerm();
    [[nodiscard]] std::unique_ptr<ast::ASTNode> parseMatMul();
    [[nodiscard]] std::unique_ptr<ast::ASTNode> parseFactor();
    [[nodiscard]] std::unique_ptr<ast::ASTNode> parsePrimary();

    Lexer lexer_;
    Token currentToken_;
};

} // namespace graphjit::frontend
