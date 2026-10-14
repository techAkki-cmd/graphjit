#include "graphjit/ast/ast.hpp"

#include <stdexcept>
#include <utility>

namespace graphjit::ast {

std::string_view binaryOperatorSpelling(const frontend::TokenType type) {
    switch (type) {
    case frontend::TokenType::Plus:
        return "+";
    case frontend::TokenType::Minus:
        return "-";
    case frontend::TokenType::Star:
        return "*";
    case frontend::TokenType::Slash:
        return "/";
    case frontend::TokenType::MatrixMultiply:
        return "@";
    case frontend::TokenType::Equal:
        return "=";
    default:
        throw std::invalid_argument("token type is not a binary operator");
    }
}

FloatLiteralNode::FloatLiteralNode(const double value) noexcept : value_(value) {}

double FloatLiteralNode::value() const noexcept {
    return value_;
}

void FloatLiteralNode::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

VariableNode::VariableNode(std::string name) : name_(std::move(name)) {}

std::string_view VariableNode::name() const noexcept {
    return name_;
}

void VariableNode::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

BinaryOpNode::BinaryOpNode(
    const frontend::TokenType operatorType,
    std::unique_ptr<ASTNode> lhs,
    std::unique_ptr<ASTNode> rhs)
    : operatorType_(operatorType), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {
    // Validate the operator eagerly so malformed trees cannot be constructed.
    static_cast<void>(binaryOperatorSpelling(operatorType_));
    if (!lhs_ || !rhs_) {
        throw std::invalid_argument("binary operator requires both child nodes");
    }
}

frontend::TokenType BinaryOpNode::operatorType() const noexcept {
    return operatorType_;
}

ASTNode& BinaryOpNode::lhs() noexcept {
    return *lhs_;
}

const ASTNode& BinaryOpNode::lhs() const noexcept {
    return *lhs_;
}

ASTNode& BinaryOpNode::rhs() noexcept {
    return *rhs_;
}

const ASTNode& BinaryOpNode::rhs() const noexcept {
    return *rhs_;
}

std::unique_ptr<ASTNode> BinaryOpNode::takeLhs() noexcept {
    return std::move(lhs_);
}

std::unique_ptr<ASTNode> BinaryOpNode::takeRhs() noexcept {
    return std::move(rhs_);
}

void BinaryOpNode::setLhs(std::unique_ptr<ASTNode> lhs) {
    if (!lhs) {
        throw std::invalid_argument("binary operator requires a left child");
    }
    lhs_ = std::move(lhs);
}

void BinaryOpNode::setRhs(std::unique_ptr<ASTNode> rhs) {
    if (!rhs) {
        throw std::invalid_argument("binary operator requires a right child");
    }
    rhs_ = std::move(rhs);
}

void BinaryOpNode::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

FunctionDeclNode::FunctionDeclNode(
    std::string name,
    std::vector<std::string> arguments,
    std::unique_ptr<ASTNode> body)
    : name_(std::move(name)),
      arguments_(std::move(arguments)),
      body_(std::move(body)) {
    if (name_.empty()) {
        throw std::invalid_argument("function name cannot be empty");
    }
    if (!body_) {
        throw std::invalid_argument("function declaration requires a body");
    }
}

std::string_view FunctionDeclNode::name() const noexcept {
    return name_;
}

const std::vector<std::string>& FunctionDeclNode::arguments() const noexcept {
    return arguments_;
}

ASTNode& FunctionDeclNode::body() noexcept {
    return *body_;
}

const ASTNode& FunctionDeclNode::body() const noexcept {
    return *body_;
}

std::unique_ptr<ASTNode> FunctionDeclNode::takeBody() noexcept {
    return std::move(body_);
}

void FunctionDeclNode::setBody(std::unique_ptr<ASTNode> body) {
    if (!body) {
        throw std::invalid_argument("function declaration requires a body");
    }
    body_ = std::move(body);
}

void FunctionDeclNode::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

FusedKernelNode::FusedKernelNode(std::unique_ptr<ASTNode> fusedExpression)
    : fusedExpression_(std::move(fusedExpression)) {
    if (!fusedExpression_) {
        throw std::invalid_argument("fused kernel requires an expression");
    }
}

ASTNode& FusedKernelNode::fusedExpression() noexcept {
    return *fusedExpression_;
}

const ASTNode& FusedKernelNode::fusedExpression() const noexcept {
    return *fusedExpression_;
}

std::unique_ptr<ASTNode> FusedKernelNode::takeFusedExpression() noexcept {
    return std::move(fusedExpression_);
}

void FusedKernelNode::setFusedExpression(
    std::unique_ptr<ASTNode> fusedExpression) {
    if (!fusedExpression) {
        throw std::invalid_argument("fused kernel requires an expression");
    }
    fusedExpression_ = std::move(fusedExpression);
}

void FusedKernelNode::accept(ASTVisitor& visitor) {
    visitor.visit(*this);
}

} // namespace graphjit::ast
