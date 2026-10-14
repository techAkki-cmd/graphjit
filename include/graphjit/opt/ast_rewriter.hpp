#pragma once

#include "graphjit/ast/ast.hpp"

#include <memory>

namespace graphjit::opt {

/// Ownership-aware interface for transformations that may replace AST nodes.
class ASTRewriter {
public:
    virtual ~ASTRewriter() = default;

    [[nodiscard]] virtual std::unique_ptr<ast::ASTNode> rewrite(
        std::unique_ptr<ast::ASTNode> node) = 0;
};

} // namespace graphjit::opt
