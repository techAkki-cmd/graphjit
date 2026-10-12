#include "ir_generator.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main() {
    using graphjit::backend::IRGenerator;
    using graphjit::frontend::Lexer;
    using graphjit::frontend::Parser;

    constexpr std::string_view source = "2.0 + 3.0 * 4.0";

    try {
        Parser parser{Lexer(source)};
        auto expression = parser.parse();

        IRGenerator generator;
        static_cast<void>(generator.compile(expression.get()));
        generator.module().print(llvm::outs(), nullptr);
        llvm::outs().flush();
    } catch (const std::exception& error) {
        std::cerr << "IR generation failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
