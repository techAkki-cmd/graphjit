#pragma once

#include "graphjit/opt/ast_rewriter.hpp"

#include <memory>

namespace graphjit::opt {

/// Replaces finite, constant scalar arithmetic subtrees with float literals.
class ConstantFoldingPass final : public ASTRewriter {
public:
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewrite(
        std::unique_ptr<ast::ASTNode> node) override;

private:
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewriteBinary(
        std::unique_ptr<ast::ASTNode> node,
        ast::BinaryOpNode& binary);
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewriteFunction(
        std::unique_ptr<ast::ASTNode> node,
        ast::FunctionDeclNode& function);
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewriteFusedKernel(
        std::unique_ptr<ast::ASTNode> node,
        ast::FusedKernelNode& fusedKernel);
};

} // namespace graphjit::opt
