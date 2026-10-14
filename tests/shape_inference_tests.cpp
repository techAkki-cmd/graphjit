#include "graphjit/ast/ast.hpp"
#include "graphjit/frontend/lexer.hpp"
#include "graphjit/frontend/parser.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using graphjit::analysis::ShapeInferenceVisitor;
using graphjit::analysis::ShapeMismatchException;
using graphjit::analysis::SymbolTable;
using graphjit::analysis::TensorShape;
using graphjit::ast::ASTNode;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FunctionDeclNode;
using graphjit::ast::VariableNode;
using graphjit::frontend::Lexer;
using graphjit::frontend::Parser;
using graphjit::frontend::TokenType;

static_assert(std::is_base_of_v<std::runtime_error, ShapeMismatchException>);

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Exception, typename Callable>
void expectException(Callable&& callable, const std::string_view messageFragment) {
    try {
        std::forward<Callable>(callable)();
        fail("expected exception");
    } catch (const Exception& error) {
        expect(std::string_view(error.what()).find(messageFragment) != std::string_view::npos,
               "exception message lacks expected detail");
    }
}

[[nodiscard]] std::unique_ptr<ASTNode> parseExpression(const std::string_view source) {
    Parser parser{Lexer(source)};
    return parser.parse();
}

void testTensorShapeHelpers() {
    expect(TensorShape{}.isScalar(), "empty shape should be scalar");
    expect(TensorShape{{1}}.isScalar(), "[1] should be scalar");
    expect(!TensorShape{{1, 1}}.isScalar(), "rank-2 [1, 1] should not be scalar");
    expect(TensorShape{}.toString() == "[]", "empty shape formatting is incorrect");
    expect(TensorShape{{1}}.toString() == "[1]", "scalar shape formatting is incorrect");
    expect(TensorShape{{128, 256}}.toString() == "[128, 256]",
           "matrix shape formatting is incorrect");
    expect(TensorShape{{2, 3}} == TensorShape{{2, 3}}, "shape equality is incorrect");
}

void testSampleExpressionAnnotations() {
    auto a = std::make_unique<VariableNode>("A");
    const ASTNode* const aNode = a.get();
    auto b = std::make_unique<VariableNode>("B");
    const ASTNode* const bNode = b.get();
    auto matrixMultiply = std::make_unique<BinaryOpNode>(
        TokenType::MatrixMultiply, std::move(a), std::move(b));
    const ASTNode* const matrixNode = matrixMultiply.get();

    auto literal = std::make_unique<FloatLiteralNode>(2.0);
    const ASTNode* const literalNode = literal.get();
    auto c = std::make_unique<VariableNode>("C");
    const ASTNode* const cNode = c.get();
    auto d = std::make_unique<VariableNode>("D");
    const ASTNode* const dNode = d.get();
    auto subtraction = std::make_unique<BinaryOpNode>(
        TokenType::Minus, std::move(c), std::move(d));
    const ASTNode* const subtractionNode = subtraction.get();
    auto scaling = std::make_unique<BinaryOpNode>(
        TokenType::Star, std::move(literal), std::move(subtraction));
    const ASTNode* const scalingNode = scaling.get();
    BinaryOpNode root(TokenType::Plus, std::move(matrixMultiply), std::move(scaling));

    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{128, 64}}},
        {"B", TensorShape{{64, 256}}},
        {"C", TensorShape{{128, 256}}},
        {"D", TensorShape{{128, 256}}},
    });
    const TensorShape result = inference.infer(root);

    const TensorShape matrix{{128, 256}};
    expect(result == matrix, "sample root shape is incorrect");
    expect(inference.annotations().size() == 9, "not every sample node was annotated");
    expect(inference.shapeOf(*aNode) == TensorShape{{128, 64}}, "A shape is incorrect");
    expect(inference.shapeOf(*bNode) == TensorShape{{64, 256}}, "B shape is incorrect");
    expect(inference.shapeOf(*matrixNode) == matrix, "matmul shape is incorrect");
    expect(inference.shapeOf(*literalNode) == TensorShape{}, "literal is not scalar");
    expect(inference.shapeOf(*cNode) == matrix, "C shape is incorrect");
    expect(inference.shapeOf(*dNode) == matrix, "D shape is incorrect");
    expect(inference.shapeOf(*subtractionNode) == matrix, "subtraction shape is incorrect");
    expect(inference.shapeOf(*scalingNode) == matrix, "scaling shape is incorrect");
}

void testElementwiseOperations() {
    constexpr std::array operators{
        TokenType::Plus, TokenType::Minus, TokenType::Star, TokenType::Slash};

    for (const TokenType operatorType : operators) {
        BinaryOpNode expression(
            operatorType,
            std::make_unique<VariableNode>("L"),
            std::make_unique<VariableNode>("R"));
        ShapeInferenceVisitor inference(SymbolTable{
            {"L", TensorShape{{2, 3}}}, {"R", TensorShape{{2, 3}}}});
        expect(inference.infer(expression) == TensorShape{{2, 3}},
               "identical element-wise shapes failed");
    }

    BinaryOpNode leftScalar(
        TokenType::Plus,
        std::make_unique<VariableNode>("S"),
        std::make_unique<VariableNode>("T"));
    ShapeInferenceVisitor emptyScalar(SymbolTable{
        {"S", TensorShape{}}, {"T", TensorShape{{4, 5}}}});
    expect(emptyScalar.infer(leftScalar) == TensorShape{{4, 5}},
           "left scalar broadcasting failed");

    BinaryOpNode rightScalar(
        TokenType::Star,
        std::make_unique<VariableNode>("T"),
        std::make_unique<VariableNode>("S"));
    ShapeInferenceVisitor explicitScalar(SymbolTable{
        {"S", TensorShape{{1}}}, {"T", TensorShape{{4, 5}}}});
    expect(explicitScalar.infer(rightScalar) == TensorShape{{4, 5}},
           "right scalar broadcasting failed");

    BinaryOpNode scalarPair(
        TokenType::Slash,
        std::make_unique<VariableNode>("S1"),
        std::make_unique<VariableNode>("S2"));
    ShapeInferenceVisitor scalarInference(SymbolTable{
        {"S1", TensorShape{{1}}}, {"S2", TensorShape{}}});
    expect(scalarInference.infer(scalarPair) == TensorShape{},
           "scalar results should use canonical empty shape");

    BinaryOpNode mismatch(
        TokenType::Plus,
        std::make_unique<VariableNode>("L"),
        std::make_unique<VariableNode>("R"));
    ShapeInferenceVisitor mismatchInference(SymbolTable{
        {"L", TensorShape{{2, 3}}}, {"R", TensorShape{{3, 2}}}});
    expectException<ShapeMismatchException>(
        [&] { static_cast<void>(mismatchInference.infer(mismatch)); },
        "identical shapes");
    expect(mismatchInference.annotations().empty(),
           "failed element-wise inference left partial annotations");
}

void testMatrixMultiplication() {
    BinaryOpNode valid(
        TokenType::MatrixMultiply,
        std::make_unique<VariableNode>("L"),
        std::make_unique<VariableNode>("R"));
    ShapeInferenceVisitor validInference(SymbolTable{
        {"L", TensorShape{{0, 3}}}, {"R", TensorShape{{3, 0}}}});
    expect(validInference.infer(valid) == TensorShape{{0, 0}},
           "valid zero-extent matrix multiplication failed");

    BinaryOpNode innerMismatch(
        TokenType::MatrixMultiply,
        std::make_unique<VariableNode>("A"),
        std::make_unique<VariableNode>("C"));
    ShapeInferenceVisitor mismatchInference(SymbolTable{
        {"A", TensorShape{{128, 64}}}, {"C", TensorShape{{128, 256}}}});
    expectException<ShapeMismatchException>(
        [&] { static_cast<void>(mismatchInference.infer(innerMismatch)); },
        "Inner dimensions 64 and 128 must match");
    expect(mismatchInference.annotations().empty(),
           "failed matrix inference left partial annotations");

    BinaryOpNode rankMismatch(
        TokenType::MatrixMultiply,
        std::make_unique<VariableNode>("V"),
        std::make_unique<VariableNode>("M"));
    ShapeInferenceVisitor rankInference(SymbolTable{
        {"V", TensorShape{{3}}}, {"M", TensorShape{{3, 4}}}});
    expectException<ShapeMismatchException>(
        [&] { static_cast<void>(rankInference.infer(rankMismatch)); },
        "rank-2 operands");
}

void testFunctionAndFailureCases() {
    auto body = std::make_unique<VariableNode>("A");
    const ASTNode* const bodyNode = body.get();
    FunctionDeclNode function("compute", {"A"}, std::move(body));
    ShapeInferenceVisitor functionInference(
        SymbolTable{{"A", TensorShape{{8, 16}}}});
    expect(functionInference.infer(function) == TensorShape{{8, 16}},
           "function result shape is incorrect");
    expect(functionInference.shapeOf(*bodyNode) == TensorShape{{8, 16}},
           "function body was not annotated");

    VariableNode missing("missing");
    ShapeInferenceVisitor missingInference(SymbolTable{});
    expectException<std::runtime_error>(
        [&] { static_cast<void>(missingInference.infer(missing)); },
        "no input shape is defined");

    BinaryOpNode unsupported(
        TokenType::Equal,
        std::make_unique<FloatLiteralNode>(1.0),
        std::make_unique<FloatLiteralNode>(2.0));
    ShapeInferenceVisitor unsupportedInference(SymbolTable{});
    expectException<std::runtime_error>(
        [&] { static_cast<void>(unsupportedInference.infer(unsupported)); },
        "does not support binary operator '='");

    expectException<std::logic_error>(
        [&] { static_cast<void>(unsupportedInference.shapeOf(missing)); },
        "has not been inferred");
}

void testRepeatedInferenceClearsAnnotations() {
    ShapeInferenceVisitor inference(SymbolTable{{"A", TensorShape{{2, 2}}}});
    VariableNode first("A");
    expect(inference.infer(first) == TensorShape{{2, 2}}, "first inference failed");
    expect(inference.annotations().size() == 1, "first annotation is missing");

    FloatLiteralNode second(2.0);
    expect(inference.infer(second) == TensorShape{}, "second inference failed");
    expect(inference.annotations().size() == 1, "stale annotations were retained");
    expect(inference.annotations().contains(&second), "second node was not annotated");
    expect(!inference.annotations().contains(&first), "first node annotation is stale");

    VariableNode missing("missing");
    expectException<std::runtime_error>(
        [&] { static_cast<void>(inference.infer(missing)); }, "missing");
    expect(inference.annotations().empty(), "failed run did not clear annotations");
}

void testParserIntegration() {
    auto root = parseExpression("A @ B + 2.0 * (C - D)");
    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{128, 64}}},
        {"B", TensorShape{{64, 256}}},
        {"C", TensorShape{{128, 256}}},
        {"D", TensorShape{{128, 256}}},
    });
    expect(inference.infer(*root) == TensorShape{{128, 256}},
           "parser-to-shape-inference integration failed");
}

} // namespace

int main() {
    try {
        testTensorShapeHelpers();
        testSampleExpressionAnnotations();
        testElementwiseOperations();
        testMatrixMultiplication();
        testFunctionAndFailureCases();
        testRepeatedInferenceClearsAnnotations();
        testParserIntegration();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All shape inference tests passed.\n";
    return EXIT_SUCCESS;
}
