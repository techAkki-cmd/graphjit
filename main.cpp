#include "lexer.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

int main() {
    using graphjit::frontend::Lexer;
    using graphjit::frontend::LexerError;
    using graphjit::frontend::TokenType;
    using graphjit::frontend::tokenTypeName;

    constexpr std::string_view source =
        "def compute(A: tensor, B: tensor) { return A @ B + 2.0; }";

    try {
        Lexer lexer(source);
        for (;;) {
            const auto token = lexer.nextToken();
            std::cout << std::left << std::setw(16) << tokenTypeName(token.type)
                      << " lexeme=" << std::quoted(token.lexeme)
                      << " line=" << token.line << " column=" << token.column << '\n';

            if (token.type == TokenType::EndOfFile) {
                break;
            }
        }
    } catch (const LexerError& error) {
        std::cerr << "Lexer error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
