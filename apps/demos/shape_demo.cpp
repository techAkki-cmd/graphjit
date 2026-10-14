#include "graphjit/frontend/lexer.hpp"
#include "graphjit/frontend/parser.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

namespace {

[[nodiscard]] std::unique_ptr<graphjit::ast::ASTNode> parseExpression(
    const std::string_view source) {
    graphjit::frontend::Parser parser{graphjit::frontend::Lexer(source)};
    return parser.parse();
}

} // namespace

int main() {
    using graphjit::analysis::ShapeInferenceVisitor;
    using graphjit::analysis::ShapeMismatchException;
    using graphjit::analysis::SymbolTable;
    using graphjit::analysis::TensorShape;

    SymbolTable symbols{
        {"A", TensorShape{{128, 64}}},
        {"B", TensorShape{{64, 256}}},
        {"C", TensorShape{{128, 256}}},
        {"D", TensorShape{{128, 256}}},
    };
    ShapeInferenceVisitor inference(std::move(symbols));

    try {
        constexpr std::string_view validExpression = "A @ B + 2.0 * (C - D)";
        std::cout << "[INFO] Running shape inference on: " << validExpression << '\n';
        auto validRoot = parseExpression(validExpression);
        const TensorShape rootShape = inference.infer(*validRoot);
        std::cout << "[INFO] Root node deduced shape: " << rootShape.toString() << "\n\n";

        constexpr std::string_view invalidExpression = "A @ C";
        std::cout << "[INFO] Running shape inference on: " << invalidExpression << '\n';
        auto invalidRoot = parseExpression(invalidExpression);
        try {
            static_cast<void>(inference.infer(*invalidRoot));
        } catch (const ShapeMismatchException& error) {
            std::cout << "[ERROR] ShapeMismatchException: " << error.what() << '\n';
            return EXIT_SUCCESS;
        }

        std::cerr << "[ERROR] Expected invalid matrix multiplication to fail.\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] Unexpected failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
