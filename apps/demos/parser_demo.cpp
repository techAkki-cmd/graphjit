#include "graphjit/ast/ast_printer.hpp"
#include "graphjit/frontend/lexer.hpp"
#include "graphjit/frontend/parser.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main() {
    using graphjit::ast::ASTPrinter;
    using graphjit::frontend::Lexer;
    using graphjit::frontend::Parser;

    constexpr std::string_view source = "A @ B + 2.0 * (C - D)";

    try {
        Parser parser{Lexer(source)};
        auto expression = parser.parse();

        ASTPrinter printer;
        expression->accept(printer);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
