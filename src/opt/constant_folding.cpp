#include "graphjit/opt/constant_folding.hpp"

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace graphjit::opt {
namespace {

[[nodiscard]] std::optional<double> evaluate(
    const frontend::TokenType operatorType,
    const double lhs,
    const double rhs) {
    switch (operatorType) {
    case frontend::TokenType::Plus:
        return lhs + rhs;
    case frontend::TokenType::Minus:
        return lhs - rhs;
    case frontend::TokenType::Star:
        return lhs * rhs;
    case frontend::TokenType::Slash:
        return lhs / rhs;
    default:
        return std::nullopt;
    }
}

} // namespace

std::unique_ptr<ast::ASTNode> ConstantFoldingPass::rewrite(
    std::unique_ptr<ast::ASTNode> node) {
    if (!node) {
        throw std::invalid_argument("cannot rewrite a null AST node");
    }

    if (auto* const binary = dynamic_cast<ast::BinaryOpNode*>(node.get())) {
        return rewriteBinary(std::move(node), *binary);
    }
    if (auto* const function = dynamic_cast<ast::FunctionDeclNode*>(node.get())) {
        return rewriteFunction(std::move(node), *function);
    }
    if (auto* const fusedKernel = dynamic_cast<ast::FusedKernelNode*>(node.get())) {
        return rewriteFusedKernel(std::move(node), *fusedKernel);
    }

    // Literal and variable leaves have no children and retain their identity.
    return node;
}

std::unique_ptr<ast::ASTNode> ConstantFoldingPass::rewriteBinary(
    std::unique_ptr<ast::ASTNode> node,
    ast::BinaryOpNode& binary) {
    // Rewrite both operands before inspecting this node. This bottom-up order
    // allows a parent to fold after its constant descendants have collapsed.
    auto lhs = rewrite(binary.takeLhs());
    auto rhs = rewrite(binary.takeRhs());

    const auto* const lhsLiteral = dynamic_cast<const ast::FloatLiteralNode*>(lhs.get());
    const auto* const rhsLiteral = dynamic_cast<const ast::FloatLiteralNode*>(rhs.get());
    if (lhsLiteral != nullptr && rhsLiteral != nullptr) {
        const std::optional<double> result = evaluate(
            binary.operatorType(), lhsLiteral->value(), rhsLiteral->value());
        if (result.has_value() && std::isfinite(*result)) {
            return std::make_unique<ast::FloatLiteralNode>(*result);
        }
    }

    binary.setLhs(std::move(lhs));
    binary.setRhs(std::move(rhs));
    return node;
}

std::unique_ptr<ast::ASTNode> ConstantFoldingPass::rewriteFunction(
    std::unique_ptr<ast::ASTNode> node,
    ast::FunctionDeclNode& function) {
    function.setBody(rewrite(function.takeBody()));
    return node;
}

std::unique_ptr<ast::ASTNode> ConstantFoldingPass::rewriteFusedKernel(
    std::unique_ptr<ast::ASTNode> node,
    ast::FusedKernelNode& fusedKernel) {
    fusedKernel.setFusedExpression(rewrite(fusedKernel.takeFusedExpression()));
    return node;
}

} // namespace graphjit::opt
