#pragma once

#include "graphjit/opt/ast_rewriter.hpp"

#include <memory>

namespace graphjit::opt {

/// Marks maximal element-wise expression regions as fused kernels.
class KernelFusionPass final : public ASTRewriter {
public:
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewrite(
        std::unique_ptr<ast::ASTNode> node) override;

private:
    [[nodiscard]] static bool isElementWiseOp(
        const ast::ASTNode* node) noexcept;
    [[nodiscard]] std::unique_ptr<ast::ASTNode> rewriteImpl(
        std::unique_ptr<ast::ASTNode> node,
        bool parentIsElementWise);
};

} // namespace graphjit::opt
