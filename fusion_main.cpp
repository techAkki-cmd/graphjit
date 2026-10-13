#include "ast_printer.hpp"
#include "kernel_fusion.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

int main() {
    using graphjit::ast::ASTPrinter;
    using graphjit::frontend::Lexer;
    using graphjit::frontend::Parser;
    using graphjit::opt::KernelFusionPass;

    constexpr std::string_view source = "(A + B) * C + (D @ E)";

    try {
        Parser parser{Lexer(source)};
        auto expression = parser.parse();

        std::cout << "[INFO] Running Kernel Fusion Pass...\n\n";
        KernelFusionPass fusion;
        expression = fusion.rewrite(std::move(expression));

        ASTPrinter printer;
        expression->accept(printer);
    } catch (const std::exception& error) {
        std::cerr << "Kernel fusion failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
