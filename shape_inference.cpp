#include "shape_inference.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace graphjit::analysis {

bool TensorShape::isScalar() const noexcept {
    return dims.empty() || (dims.size() == 1 && dims.front() == 1);
}

std::string TensorShape::toString() const {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < dims.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << dims[index];
    }
    output << ']';
    return output.str();
}

ShapeMismatchException::ShapeMismatchException(std::string message)
    : std::runtime_error(std::move(message)) {}

ShapeInferenceVisitor::ShapeInferenceVisitor(SymbolTable symbols)
    : symbols_(std::move(symbols)) {}

TensorShape ShapeInferenceVisitor::infer(ast::ASTNode& root) {
    annotations_.clear();
    try {
        root.accept(*this);
        return shapeOf(root);
    } catch (...) {
        annotations_.clear();
        throw;
    }
}

const TensorShape& ShapeInferenceVisitor::shapeOf(const ast::ASTNode& node) const {
    const auto shape = annotations_.find(&node);
    if (shape == annotations_.end()) {
        throw std::logic_error(
            "shape is unavailable because this AST node has not been inferred");
    }
    return shape->second;
}

const ShapeAnnotations& ShapeInferenceVisitor::annotations() const noexcept {
    return annotations_;
}

void ShapeInferenceVisitor::annotate(const ast::ASTNode& node, TensorShape shape) {
    annotations_.insert_or_assign(&node, std::move(shape));
}

TensorShape ShapeInferenceVisitor::inferElementwise(
    const frontend::TokenType operatorType,
    const TensorShape& lhs,
    const TensorShape& rhs) {
    if (lhs.isScalar() && rhs.isScalar()) {
        return TensorShape{};
    }
    if (lhs.isScalar()) {
        return rhs;
    }
    if (rhs.isScalar()) {
        return lhs;
    }
    if (lhs == rhs) {
        return lhs;
    }

    std::ostringstream message;
    message << "Cannot apply element-wise operator '"
            << ast::binaryOperatorSpelling(operatorType) << "' to tensors of shape "
            << lhs.toString() << " and " << rhs.toString()
            << ". Operands must have identical shapes unless one operand is scalar.";
    throw ShapeMismatchException(message.str());
}

TensorShape ShapeInferenceVisitor::inferMatrixMultiply(
    const TensorShape& lhs, const TensorShape& rhs) {
    if (lhs.dims.size() != 2 || rhs.dims.size() != 2) {
        std::ostringstream message;
        message << "Cannot multiply tensors of shape " << lhs.toString() << " (rank "
                << lhs.dims.size() << ") and " << rhs.toString() << " (rank "
                << rhs.dims.size()
                << "). Matrix multiplication requires two rank-2 operands.";
        throw ShapeMismatchException(message.str());
    }

    const std::size_t lhsInner = lhs.dims[1];
    const std::size_t rhsInner = rhs.dims[0];
    if (lhsInner != rhsInner) {
        std::ostringstream message;
        message << "Cannot multiply matrices of shape " << lhs.toString() << " and "
                << rhs.toString() << ". Inner dimensions " << lhsInner << " and "
                << rhsInner << " must match.";
        throw ShapeMismatchException(message.str());
    }

    return TensorShape{{lhs.dims[0], rhs.dims[1]}};
}

void ShapeInferenceVisitor::visit(ast::FloatLiteralNode& node) {
    annotate(node, TensorShape{});
}

void ShapeInferenceVisitor::visit(ast::VariableNode& node) {
    const std::string variableName(node.name());
    const auto symbol = symbols_.find(variableName);
    if (symbol == symbols_.end()) {
        std::ostringstream message;
        message << "Shape inference failed for variable '" << variableName
                << "': no input shape is defined. Add '" << variableName
                << "' to the symbol table before running inference.";
        throw std::runtime_error(message.str());
    }
    annotate(node, symbol->second);
}

void ShapeInferenceVisitor::visit(ast::BinaryOpNode& node) {
    node.lhs().accept(*this);
    node.rhs().accept(*this);

    const TensorShape& lhs = shapeOf(node.lhs());
    const TensorShape& rhs = shapeOf(node.rhs());

    TensorShape result;
    switch (node.operatorType()) {
    case frontend::TokenType::Plus:
    case frontend::TokenType::Minus:
    case frontend::TokenType::Star:
    case frontend::TokenType::Slash:
        result = inferElementwise(node.operatorType(), lhs, rhs);
        break;
    case frontend::TokenType::MatrixMultiply:
        result = inferMatrixMultiply(lhs, rhs);
        break;
    default: {
        std::ostringstream message;
        message << "Shape inference does not support binary operator '"
                << ast::binaryOperatorSpelling(node.operatorType()) << "'.";
        throw std::runtime_error(message.str());
    }
    }

    annotate(node, std::move(result));
}

void ShapeInferenceVisitor::visit(ast::FunctionDeclNode& node) {
    node.body().accept(*this);
    annotate(node, shapeOf(node.body()));
}

void ShapeInferenceVisitor::visit(ast::FusedKernelNode& node) {
    node.fusedExpression().accept(*this);
    annotate(node, shapeOf(node.fusedExpression()));
}

} // namespace graphjit::analysis
