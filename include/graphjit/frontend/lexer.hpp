#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace graphjit::frontend {

/// Every syntactic unit recognized by the GraphJIT phase-one lexer.
enum class TokenType {
    // Keywords.
    Def,
    Tensor,
    Return,

    // Operators.
    Plus,
    Minus,
    Star,
    Slash,
    MatrixMultiply,
    Equal,

    // Punctuation.
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Colon,

    // Primitives and stream terminator.
    Identifier,
    FloatLiteral,
    IntLiteral,
    EndOfFile,
};

/// Returns a stable, human-readable spelling for a token category.
[[nodiscard]] std::string_view tokenTypeName(TokenType type) noexcept;

/// A token owns its spelling so it remains valid independently of the Lexer.
struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

/// Describes a lexical failure at an exact source location.
class LexerError final : public std::runtime_error {
public:
    LexerError(std::string_view reason, char offendingCharacter, int line, int column);

    [[nodiscard]] char offendingCharacter() const noexcept;
    [[nodiscard]] int line() const noexcept;
    [[nodiscard]] int column() const noexcept;

private:
    char offendingCharacter_;
    int line_;
    int column_;
};

/// A single-pass, non-owning scanner for GraphJIT source code.
///
/// The source buffer must outlive this object. Scanning operates directly on the
/// supplied string_view; a token's lexeme is copied only when that token is
/// returned, as Token deliberately provides independent ownership.
class Lexer final {
public:
    explicit Lexer(std::string_view source) noexcept;

    /// Consumes and returns the next token, or throws LexerError on invalid input.
    /// Calling this method after exhaustion repeatedly returns the same EOF token.
    [[nodiscard]] Token nextToken();

private:
    [[nodiscard]] bool atEnd() const noexcept;
    [[nodiscard]] bool has(std::size_t offset = 0) const noexcept;
    [[nodiscard]] char peek(std::size_t offset = 0) const noexcept;
    char advance() noexcept;

    void skipIgnored() noexcept;
    [[nodiscard]] Token scanIdentifier(std::size_t start, int line, int column);
    [[nodiscard]] Token scanNumber(
        std::size_t start, int line, int column, bool startedWithDot);
    [[nodiscard]] Token makeToken(
        TokenType type, std::size_t start, int line, int column) const;

    std::string_view source_;
    std::size_t current_{0};
    int line_{1};
    int column_{1};
};

} // namespace graphjit::frontend
