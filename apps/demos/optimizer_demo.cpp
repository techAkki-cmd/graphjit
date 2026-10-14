#include "graphjit/ast/ast_printer.hpp"
#include "graphjit/opt/constant_folding.hpp"
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
    using graphjit::opt::ConstantFoldingPass;

    constexpr std::string_view source =
        "A @ B + (2.0 * 3.0) - (10.0 / 2.0)";

    try {
        Parser parser{Lexer(source)};
        auto expression = parser.parse();
        ASTPrinter printer;

        std::cout << "[INFO] Original AST:\n\n";
        expression->accept(printer);

        std::cout << "\n[INFO] Running Constant Folding Pass...\n\n";
        ConstantFoldingPass folding;
        expression = folding.rewrite(std::move(expression));

        std::cout << "[INFO] Optimized AST:\n\n";
        expression->accept(printer);
    } catch (const std::exception& error) {
        std::cerr << "Constant folding failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
