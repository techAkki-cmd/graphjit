#include "kernel_fusion.hpp"

#include <stdexcept>
#include <utility>

namespace graphjit::opt {

std::unique_ptr<ast::ASTNode> KernelFusionPass::rewrite(
    std::unique_ptr<ast::ASTNode> node) {
    return rewriteImpl(std::move(node), false);
}

bool KernelFusionPass::isElementWiseOp(const ast::ASTNode* const node) noexcept {
    const auto* const binary = dynamic_cast<const ast::BinaryOpNode*>(node);
    if (binary == nullptr) {
        return false;
    }

    switch (binary->operatorType()) {
    case frontend::TokenType::Plus:
    case frontend::TokenType::Minus:
    case frontend::TokenType::Star:
    case frontend::TokenType::Slash:
        return true;
    default:
        return false;
    }
}

std::unique_ptr<ast::ASTNode> KernelFusionPass::rewriteImpl(
    std::unique_ptr<ast::ASTNode> node,
    const bool parentIsElementWise) {
    if (!node) {
        throw std::invalid_argument("cannot fuse a null AST node");
    }

    if (auto* const fusedKernel = dynamic_cast<ast::FusedKernelNode*>(node.get())) {
        // An existing fusion boundary is authoritative. Treat its root as part
        // of the existing region so a second pass cannot add another wrapper.
        fusedKernel->setFusedExpression(rewriteImpl(
            fusedKernel->takeFusedExpression(), true));
        return node;
    }

    if (auto* const function = dynamic_cast<ast::FunctionDeclNode*>(node.get())) {
        function->setBody(rewriteImpl(function->takeBody(), false));
        return node;
    }

    auto* const binary = dynamic_cast<ast::BinaryOpNode*>(node.get());
    if (binary == nullptr) {
        return node;
    }

    const bool elementWise = isElementWiseOp(binary);
    binary->setLhs(rewriteImpl(binary->takeLhs(), elementWise));
    binary->setRhs(rewriteImpl(binary->takeRhs(), elementWise));

    if (elementWise && !parentIsElementWise) {
        // A fused chain can load each tensor element once, evaluate all of its
        // arithmetic in registers, and store once. Avoiding an intermediate
        // tensor per operator removes allocation traffic and multiple full
        // memory passes. Matrix multiplication cannot join that loop: each
        // result consumes full rows and columns instead of matching flat
        // element indices. Its result must therefore be materialized before
        // an enclosing element-wise kernel reads it. MatMul and other
        // non-element-wise parents pass false above, deliberately terminating
        // the chain at their boundaries.
        return std::make_unique<ast::FusedKernelNode>(std::move(node));
    }

    return node;
}

} // namespace graphjit::opt
