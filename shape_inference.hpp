#pragma once

#include "ast.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphjit::analysis {

struct TensorShape {
    std::vector<std::size_t> dims;

    [[nodiscard]] bool isScalar() const noexcept;
    [[nodiscard]] std::string toString() const;

    bool operator==(const TensorShape&) const = default;
};

class ShapeMismatchException final : public std::runtime_error {
public:
    explicit ShapeMismatchException(std::string message);
};

using SymbolTable = std::unordered_map<std::string, TensorShape>;
using ShapeAnnotations = std::unordered_map<const ast::ASTNode*, TensorShape>;

/// Computes tensor shapes without changing the AST itself.
class ShapeInferenceVisitor final : public ast::ASTVisitor {
public:
    explicit ShapeInferenceVisitor(SymbolTable symbols);

    /// Runs a fresh inference pass. Failed runs leave no partial annotations.
    [[nodiscard]] TensorShape infer(ast::ASTNode& root);

    [[nodiscard]] const TensorShape& shapeOf(const ast::ASTNode& node) const;
    [[nodiscard]] const ShapeAnnotations& annotations() const noexcept;

    void visit(ast::FloatLiteralNode& node) override;
    void visit(ast::VariableNode& node) override;
    void visit(ast::BinaryOpNode& node) override;
    void visit(ast::FunctionDeclNode& node) override;

private:
    void annotate(const ast::ASTNode& node, TensorShape shape);
    [[nodiscard]] static TensorShape inferElementwise(
        frontend::TokenType operatorType,
        const TensorShape& lhs,
        const TensorShape& rhs);
    [[nodiscard]] static TensorShape inferMatrixMultiply(
        const TensorShape& lhs, const TensorShape& rhs);

    SymbolTable symbols_;
    ShapeAnnotations annotations_;
};

} // namespace graphjit::analysis
