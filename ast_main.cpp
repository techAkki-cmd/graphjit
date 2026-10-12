#include "ast.hpp"
#include "ast_printer.hpp"

#include <memory>
#include <utility>

int main() {
    using graphjit::ast::ASTPrinter;
    using graphjit::ast::BinaryOpNode;
    using graphjit::ast::FloatLiteralNode;
    using graphjit::ast::VariableNode;
    using graphjit::frontend::TokenType;

    auto matrixMultiply = std::make_unique<BinaryOpNode>(
        TokenType::MatrixMultiply,
        std::make_unique<VariableNode>("A"),
        std::make_unique<VariableNode>("B"));

    BinaryOpNode expression(
        TokenType::Plus,
        std::move(matrixMultiply),
        std::make_unique<FloatLiteralNode>(2.0));

    ASTPrinter printer;
    expression.accept(printer);
}
