#include "graphjit/frontend/lexer.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace graphjit::frontend {
namespace {

[[nodiscard]] constexpr bool isDigit(const char value) noexcept {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr bool isIdentifierStart(const char value) noexcept {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') || value == '_';
}

[[nodiscard]] constexpr bool isIdentifierContinue(const char value) noexcept {
    return isIdentifierStart(value) || isDigit(value);
}

[[nodiscard]] constexpr bool isHorizontalWhitespace(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\v' || value == '\f';
}

[[nodiscard]] std::string describeCharacter(const char value) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte >= 0x20U && byte <= 0x7EU) {
        return "'" + std::string(1, value) + "'";
    }

    std::ostringstream output;
    output << "byte 0x" << std::uppercase << std::hex << std::setw(2)
           << std::setfill('0') << static_cast<unsigned int>(byte);
    return output.str();
}

[[nodiscard]] std::string makeErrorMessage(
    const std::string_view reason,
    const char offendingCharacter,
    const int line,
    const int column) {
    std::ostringstream output;
    output << reason << " at line " << line << ", column " << column
           << " (offending character " << describeCharacter(offendingCharacter) << ')';
    return output.str();
}

} // namespace

std::string_view tokenTypeName(const TokenType type) noexcept {
    switch (type) {
    case TokenType::Def:
        return "Def";
    case TokenType::Tensor:
        return "Tensor";
    case TokenType::Return:
        return "Return";
    case TokenType::Plus:
        return "Plus";
    case TokenType::Minus:
        return "Minus";
    case TokenType::Star:
        return "Star";
    case TokenType::Slash:
        return "Slash";
    case TokenType::MatrixMultiply:
        return "MatrixMultiply";
    case TokenType::Equal:
        return "Equal";
    case TokenType::LeftParen:
        return "LeftParen";
    case TokenType::RightParen:
        return "RightParen";
    case TokenType::LeftBrace:
        return "LeftBrace";
    case TokenType::RightBrace:
        return "RightBrace";
    case TokenType::Comma:
        return "Comma";
    case TokenType::Semicolon:
        return "Semicolon";
    case TokenType::Colon:
        return "Colon";
    case TokenType::Identifier:
        return "Identifier";
    case TokenType::FloatLiteral:
        return "FloatLiteral";
    case TokenType::IntLiteral:
        return "IntLiteral";
    case TokenType::EndOfFile:
        return "EndOfFile";
    }

    // Defensive fallback for values produced by unchecked casts.
    return "Unknown";
}

LexerError::LexerError(
    const std::string_view reason,
    const char offendingCharacter,
    const int line,
    const int column)
    : std::runtime_error(makeErrorMessage(reason, offendingCharacter, line, column)),
      offendingCharacter_(offendingCharacter),
      line_(line),
      column_(column) {}

char LexerError::offendingCharacter() const noexcept {
    return offendingCharacter_;
}

int LexerError::line() const noexcept {
    return line_;
}

int LexerError::column() const noexcept {
    return column_;
}

Lexer::Lexer(const std::string_view source) noexcept : source_(source) {}

bool Lexer::atEnd() const noexcept {
    return current_ >= source_.size();
}

bool Lexer::has(const std::size_t offset) const noexcept {
    // Written without current_ + offset to avoid unsigned overflow.
    return offset < source_.size() - current_;
}

char Lexer::peek(const std::size_t offset) const noexcept {
    return has(offset) ? source_[current_ + offset] : '\0';
}

char Lexer::advance() noexcept {
    const char value = source_[current_++];

    if (value == '\r') {
        // Treat CRLF as one logical newline while consuming both source bytes.
        if (current_ < source_.size() && source_[current_] == '\n') {
            ++current_;
        }
        ++line_;
        column_ = 1;
    } else if (value == '\n') {
        ++line_;
        column_ = 1;
    } else {
        // Columns intentionally count bytes; a tab therefore advances by one.
        ++column_;
    }

    return value;
}

void Lexer::skipIgnored() noexcept {
    for (;;) {
        while (!atEnd() &&
               (isHorizontalWhitespace(peek()) || peek() == '\r' || peek() == '\n')) {
            static_cast<void>(advance());
        }

        if (atEnd() || peek() != '#') {
            return;
        }

        // Leave newline handling to the whitespace loop so all line endings use
        // the same position-accounting path.
        while (!atEnd() && peek() != '\r' && peek() != '\n') {
            static_cast<void>(advance());
        }
    }
}

Token Lexer::makeToken(
    const TokenType type,
    const std::size_t start,
    const int line,
    const int column) const {
    return Token{type, std::string(source_.substr(start, current_ - start)), line, column};
}

Token Lexer::scanIdentifier(
    const std::size_t start, const int line, const int column) {
    while (!atEnd() && isIdentifierContinue(peek())) {
        static_cast<void>(advance());
    }

    const std::string_view spelling = source_.substr(start, current_ - start);
    TokenType type = TokenType::Identifier;
    if (spelling == "def") {
        type = TokenType::Def;
    } else if (spelling == "tensor") {
        type = TokenType::Tensor;
    } else if (spelling == "return") {
        type = TokenType::Return;
    }

    return makeToken(type, start, line, column);
}

Token Lexer::scanNumber(
    const std::size_t start,
    const int line,
    const int column,
    const bool startedWithDot) {
    bool isFloat = startedWithDot;

    while (!atEnd() && isDigit(peek())) {
        static_cast<void>(advance());
    }

    if (!startedWithDot && !atEnd() && peek() == '.') {
        isFloat = true;
        static_cast<void>(advance());
        while (!atEnd() && isDigit(peek())) {
            static_cast<void>(advance());
        }
    }

    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
        isFloat = true;
        const char exponentMarker = peek();
        const int exponentLine = line_;
        const int exponentColumn = column_;
        static_cast<void>(advance());

        if (!atEnd() && (peek() == '+' || peek() == '-')) {
            static_cast<void>(advance());
        }
        if (atEnd() || !isDigit(peek())) {
            throw LexerError(
                "Malformed numeric literal: expected a digit after exponent marker",
                exponentMarker,
                exponentLine,
                exponentColumn);
        }
        while (!atEnd() && isDigit(peek())) {
            static_cast<void>(advance());
        }
    }

    return makeToken(
        isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral,
        start,
        line,
        column);
}

Token Lexer::nextToken() {
    skipIgnored();

    if (atEnd()) {
        return Token{TokenType::EndOfFile, {}, line_, column_};
    }

    const std::size_t start = current_;
    const int tokenLine = line_;
    const int tokenColumn = column_;
    const char value = advance();

    switch (value) {
    case '+':
        return makeToken(TokenType::Plus, start, tokenLine, tokenColumn);
    case '-':
        return makeToken(TokenType::Minus, start, tokenLine, tokenColumn);
    case '*':
        return makeToken(TokenType::Star, start, tokenLine, tokenColumn);
    case '/':
        return makeToken(TokenType::Slash, start, tokenLine, tokenColumn);
    case '@':
        return makeToken(TokenType::MatrixMultiply, start, tokenLine, tokenColumn);
    case '=':
        return makeToken(TokenType::Equal, start, tokenLine, tokenColumn);
    case '(':
        return makeToken(TokenType::LeftParen, start, tokenLine, tokenColumn);
    case ')':
        return makeToken(TokenType::RightParen, start, tokenLine, tokenColumn);
    case '{':
        return makeToken(TokenType::LeftBrace, start, tokenLine, tokenColumn);
    case '}':
        return makeToken(TokenType::RightBrace, start, tokenLine, tokenColumn);
    case ',':
        return makeToken(TokenType::Comma, start, tokenLine, tokenColumn);
    case ';':
        return makeToken(TokenType::Semicolon, start, tokenLine, tokenColumn);
    case ':':
        return makeToken(TokenType::Colon, start, tokenLine, tokenColumn);
    default:
        break;
    }

    if (isIdentifierStart(value)) {
        return scanIdentifier(start, tokenLine, tokenColumn);
    }
    if (isDigit(value)) {
        return scanNumber(start, tokenLine, tokenColumn, false);
    }
    if (value == '.') {
        if (!atEnd() && isDigit(peek())) {
            return scanNumber(start, tokenLine, tokenColumn, true);
        }
        throw LexerError("Unexpected standalone decimal point", value, tokenLine, tokenColumn);
    }

    throw LexerError("Unknown character", value, tokenLine, tokenColumn);
}

} // namespace graphjit::frontend
