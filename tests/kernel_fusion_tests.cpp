#include "graphjit/ast/ast.hpp"
#include "graphjit/ast/ast_printer.hpp"
#include "graphjit/opt/constant_folding.hpp"
#include "graphjit/opt/kernel_fusion.hpp"
#include "graphjit/frontend/lexer.hpp"
#include "graphjit/frontend/parser.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using graphjit::analysis::ShapeInferenceVisitor;
using graphjit::analysis::SymbolTable;
using graphjit::analysis::TensorShape;
using graphjit::ast::ASTNode;
using graphjit::ast::ASTPrinter;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FunctionDeclNode;
using graphjit::ast::FusedKernelNode;
using graphjit::ast::VariableNode;
using graphjit::frontend::Lexer;
using graphjit::frontend::Parser;
using graphjit::frontend::TokenType;
using graphjit::opt::ConstantFoldingPass;
using graphjit::opt::KernelFusionPass;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Exception, typename Callable>
void expectException(Callable&& callable, const std::string_view expectedMessage) {
    try {
        std::forward<Callable>(callable)();
        fail("expected exception");
    } catch (const Exception& error) {
        expect(std::string_view(error.what()).find(expectedMessage) != std::string_view::npos,
               "exception message lacks expected detail");
    }
}

[[nodiscard]] std::unique_ptr<ASTNode> parse(const std::string_view source) {
    Parser parser{Lexer(source)};
    return parser.parse();
}

[[nodiscard]] std::unique_ptr<ASTNode> fuse(std::unique_ptr<ASTNode> root) {
    KernelFusionPass pass;
    return pass.rewrite(std::move(root));
}

[[nodiscard]] std::string print(ASTNode& root) {
    std::ostringstream output;
    ASTPrinter printer(output);
    root.accept(printer);
    return output.str();
}

[[nodiscard]] std::size_t countFusedKernels(const ASTNode& node) {
    if (const auto* const fused = dynamic_cast<const FusedKernelNode*>(&node)) {
        return 1 + countFusedKernels(fused->fusedExpression());
    }
    if (const auto* const binary = dynamic_cast<const BinaryOpNode*>(&node)) {
        return countFusedKernels(binary->lhs()) + countFusedKernels(binary->rhs());
    }
    if (const auto* const function = dynamic_cast<const FunctionDeclNode*>(&node)) {
        return countFusedKernels(function->body());
    }
    return 0;
}

void testRequestedExpression() {
    auto root = fuse(parse("(A + B) * C + (D @ E)"));
    constexpr std::string_view expected =
        "[Fused Kernel]\n"
        "  BinaryOpNode: +\n"
        "    Left: BinaryOpNode: *\n"
        "      Left: BinaryOpNode: +\n"
        "        Left: VariableNode: A\n"
        "        Right: VariableNode: B\n"
        "      Right: VariableNode: C\n"
        "    Right: BinaryOpNode: @\n"
        "      Left: VariableNode: D\n"
        "      Right: VariableNode: E\n";
    expect(print(*root) == expected, "requested fusion tree does not match");
    expect(countFusedKernels(*root) == 1,
           "element-wise chain should have exactly one fusion boundary");
}

void testMatrixMultiplyBarrier() {
    auto root = fuse(parse("(A + B) @ (C * D)"));
    auto* const matrix = dynamic_cast<BinaryOpNode*>(root.get());
    expect(matrix != nullptr && matrix->operatorType() == TokenType::MatrixMultiply,
           "matrix multiply root was incorrectly fused");
    expect(dynamic_cast<FusedKernelNode*>(&matrix->lhs()) != nullptr,
           "left element-wise operand was not independently fused");
    expect(dynamic_cast<FusedKernelNode*>(&matrix->rhs()) != nullptr,
           "right element-wise operand was not independently fused");
    expect(countFusedKernels(*root) == 2,
           "matrix barrier should produce two fusion regions");
}

void testElementWiseOperatorsAndAssignmentBarrier() {
    auto chain = fuse(parse("A + B - C * D / E"));
    expect(dynamic_cast<FusedKernelNode*>(chain.get()) != nullptr,
           "element-wise operator chain was not fused");
    expect(countFusedKernels(*chain) == 1,
           "one contiguous chain should produce one fused kernel");

    auto assignment = std::make_unique<BinaryOpNode>(
        TokenType::Equal, parse("A + B"), parse("C - D"));
    auto rewritten = fuse(std::move(assignment));
    auto* const equal = dynamic_cast<BinaryOpNode*>(rewritten.get());
    expect(equal != nullptr && equal->operatorType() == TokenType::Equal,
           "assignment barrier was incorrectly fused");
    expect(dynamic_cast<FusedKernelNode*>(&equal->lhs()) != nullptr,
           "assignment left side was not independently fused");
    expect(dynamic_cast<FusedKernelNode*>(&equal->rhs()) != nullptr,
           "assignment right side was not independently fused");
}

void testFunctionAndIdempotence() {
    auto function = std::make_unique<FunctionDeclNode>(
        "compute", std::vector<std::string>{"A", "B"}, parse("A + B"));
    auto root = fuse(std::move(function));
    auto* const declaration = dynamic_cast<FunctionDeclNode*>(root.get());
    expect(declaration != nullptr, "function declaration was replaced");
    expect(dynamic_cast<FusedKernelNode*>(&declaration->body()) != nullptr,
           "function body was not fused");

    const std::string once = print(*root);
    root = fuse(std::move(root));
    expect(print(*root) == once, "second fusion pass changed the tree");
    expect(countFusedKernels(*root) == 1,
           "second fusion pass nested another wrapper");
}

void testLeavesAndNullInput() {
    auto variable = std::make_unique<VariableNode>("A");
    ASTNode* const address = variable.get();
    auto rewritten = fuse(std::move(variable));
    expect(rewritten.get() == address, "leaf identity changed during fusion");

    KernelFusionPass pass;
    expectException<std::invalid_argument>(
        [&] { static_cast<void>(pass.rewrite(nullptr)); }, "null AST node");
}

void testOtherPassesTraverseFusedKernel() {
    auto constants = std::make_unique<FusedKernelNode>(parse("2.0 * 3.0"));
    ConstantFoldingPass folding;
    auto folded = folding.rewrite(std::move(constants));
    auto* const fused = dynamic_cast<FusedKernelNode*>(folded.get());
    expect(fused != nullptr, "constant folding removed the fusion boundary");
    const auto* const literal =
        dynamic_cast<const FloatLiteralNode*>(&fused->fusedExpression());
    expect(literal != nullptr && literal->value() == 6.0,
           "constant folding did not traverse the fused expression");

    auto tensor = fuse(parse("A + B"));
    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{4, 8}}},
        {"B", TensorShape{{4, 8}}},
    });
    expect(inference.infer(*tensor) == TensorShape{{4, 8}},
           "shape inference did not propagate through the fused kernel");
    expect(inference.shapeOf(*tensor) == TensorShape{{4, 8}},
           "fused kernel was not annotated");
}

} // namespace

int main() {
    try {
        testRequestedExpression();
        testMatrixMultiplyBarrier();
        testElementWiseOperatorsAndAssignmentBarrier();
        testFunctionAndIdempotence();
        testLeavesAndNullInput();
        testOtherPassesTraverseFusedKernel();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All kernel fusion tests passed.\n";
    return EXIT_SUCCESS;
}
