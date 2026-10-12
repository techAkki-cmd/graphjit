#pragma once

#include "ast.hpp"

#include <memory>

namespace graphjit::opt {

/// Ownership-aware interface for transformations that may replace AST nodes.
class ASTRewriter {
public:
    virtual ~ASTRewriter() = default;

    [[nodiscard]] virtual std::unique_ptr<ast::ASTNode> rewrite(
        std::unique_ptr<ast::ASTNode> node) = 0;
};

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
};

} // namespace graphjit::opt
