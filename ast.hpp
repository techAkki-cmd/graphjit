#pragma once

#include "lexer.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace graphjit::ast {

class FloatLiteralNode;
class VariableNode;
class BinaryOpNode;
class FunctionDeclNode;
class FusedKernelNode;

/// Double-dispatch interface for operations over the AST.
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(FloatLiteralNode& node) = 0;
    virtual void visit(VariableNode& node) = 0;
    virtual void visit(BinaryOpNode& node) = 0;
    virtual void visit(FunctionDeclNode& node) = 0;
    virtual void visit(FusedKernelNode& node) = 0;
};

/// Polymorphic base for all GraphJIT syntax tree nodes.
class ASTNode {
public:
    ASTNode() = default;
    virtual ~ASTNode() = default;

    ASTNode(const ASTNode&) = delete;
    ASTNode& operator=(const ASTNode&) = delete;
    ASTNode(ASTNode&&) noexcept = default;
    ASTNode& operator=(ASTNode&&) noexcept = default;

    virtual void accept(ASTVisitor& visitor) = 0;
};

class FloatLiteralNode final : public ASTNode {
public:
    explicit FloatLiteralNode(double value) noexcept;

    [[nodiscard]] double value() const noexcept;
    void accept(ASTVisitor& visitor) override;

private:
    double value_;
};

class VariableNode final : public ASTNode {
public:
    explicit VariableNode(std::string name);

    [[nodiscard]] std::string_view name() const noexcept;
    void accept(ASTVisitor& visitor) override;

private:
    std::string name_;
};

class BinaryOpNode final : public ASTNode {
public:
    BinaryOpNode(
        frontend::TokenType operatorType,
        std::unique_ptr<ASTNode> lhs,
        std::unique_ptr<ASTNode> rhs);

    [[nodiscard]] frontend::TokenType operatorType() const noexcept;
    [[nodiscard]] ASTNode& lhs() noexcept;
    [[nodiscard]] const ASTNode& lhs() const noexcept;
    [[nodiscard]] ASTNode& rhs() noexcept;
    [[nodiscard]] const ASTNode& rhs() const noexcept;

    /// Transfers child ownership to an AST rewriter. The corresponding setter
    /// must restore the child before the node is observed again.
    [[nodiscard]] std::unique_ptr<ASTNode> takeLhs() noexcept;
    [[nodiscard]] std::unique_ptr<ASTNode> takeRhs() noexcept;
    void setLhs(std::unique_ptr<ASTNode> lhs);
    void setRhs(std::unique_ptr<ASTNode> rhs);

    void accept(ASTVisitor& visitor) override;

private:
    frontend::TokenType operatorType_;
    std::unique_ptr<ASTNode> lhs_;
    std::unique_ptr<ASTNode> rhs_;
};

class FunctionDeclNode final : public ASTNode {
public:
    FunctionDeclNode(
        std::string name,
        std::vector<std::string> arguments,
        std::unique_ptr<ASTNode> body);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const std::vector<std::string>& arguments() const noexcept;
    [[nodiscard]] ASTNode& body() noexcept;
    [[nodiscard]] const ASTNode& body() const noexcept;
    [[nodiscard]] std::unique_ptr<ASTNode> takeBody() noexcept;
    void setBody(std::unique_ptr<ASTNode> body);
    void accept(ASTVisitor& visitor) override;

private:
    std::string name_;
    std::vector<std::string> arguments_;
    std::unique_ptr<ASTNode> body_;
};

/// Marks a maximal element-wise expression region for fused code generation.
class FusedKernelNode final : public ASTNode {
public:
    explicit FusedKernelNode(std::unique_ptr<ASTNode> fusedExpression);

    [[nodiscard]] ASTNode& fusedExpression() noexcept;
    [[nodiscard]] const ASTNode& fusedExpression() const noexcept;
    [[nodiscard]] std::unique_ptr<ASTNode> takeFusedExpression() noexcept;
    void setFusedExpression(std::unique_ptr<ASTNode> fusedExpression);
    void accept(ASTVisitor& visitor) override;

private:
    std::unique_ptr<ASTNode> fusedExpression_;
};

/// Returns the source spelling for a valid binary operator token.
/// Throws std::invalid_argument when passed a non-operator token kind.
[[nodiscard]] std::string_view binaryOperatorSpelling(frontend::TokenType type);

} // namespace graphjit::ast
