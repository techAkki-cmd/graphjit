#include "graphjit/ast/ast.hpp"
#include "graphjit/ast/ast_printer.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using graphjit::ast::ASTNode;
using graphjit::ast::ASTPrinter;
using graphjit::ast::ASTVisitor;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FunctionDeclNode;
using graphjit::ast::FusedKernelNode;
using graphjit::ast::VariableNode;
using graphjit::ast::binaryOperatorSpelling;
using graphjit::frontend::TokenType;

static_assert(!std::is_copy_constructible_v<FloatLiteralNode>);
static_assert(!std::is_copy_constructible_v<VariableNode>);
static_assert(!std::is_copy_constructible_v<BinaryOpNode>);
static_assert(!std::is_copy_constructible_v<FunctionDeclNode>);
static_assert(!std::is_copy_constructible_v<FusedKernelNode>);
static_assert(std::is_move_constructible_v<BinaryOpNode>);
static_assert(std::is_move_constructible_v<FunctionDeclNode>);
static_assert(std::is_move_constructible_v<FusedKernelNode>);

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Callable>
void expectInvalidArgument(Callable&& callable) {
    try {
        std::forward<Callable>(callable)();
        fail("expected std::invalid_argument");
    } catch (const std::invalid_argument&) {
    }
}

[[nodiscard]] std::unique_ptr<ASTNode> makeSampleExpression() {
    auto matrixMultiply = std::make_unique<BinaryOpNode>(
        TokenType::MatrixMultiply,
        std::make_unique<VariableNode>("A"),
        std::make_unique<VariableNode>("B"));
    return std::make_unique<BinaryOpNode>(
        TokenType::Plus,
        std::move(matrixMultiply),
        std::make_unique<FloatLiteralNode>(2.0));
}

void testSamplePrinterOutput() {
    std::ostringstream output;
    ASTPrinter printer(output);
    auto expression = makeSampleExpression();
    expression->accept(printer);

    constexpr std::string_view expected =
        "BinaryOpNode: +\n"
        "  Left: BinaryOpNode: @\n"
        "    Left: VariableNode: A\n"
        "    Right: VariableNode: B\n"
        "  Right: FloatLiteralNode: 2.0\n";
    expect(output.str() == expected, "sample tree output does not match");
}

class RecordingVisitor final : public ASTVisitor {
public:
    void visit(FloatLiteralNode&) override {
        ++floatVisits;
    }

    void visit(VariableNode&) override {
        ++variableVisits;
    }

    void visit(BinaryOpNode&) override {
        ++binaryVisits;
    }

    void visit(FunctionDeclNode&) override {
        ++functionVisits;
    }

    void visit(FusedKernelNode&) override {
        ++fusedKernelVisits;
    }

    int floatVisits{0};
    int variableVisits{0};
    int binaryVisits{0};
    int functionVisits{0};
    int fusedKernelVisits{0};
};

void testDoubleDispatch() {
    FloatLiteralNode literal(1.5);
    VariableNode variable("value");
    BinaryOpNode binary(
        TokenType::Star,
        std::make_unique<VariableNode>("lhs"),
        std::make_unique<VariableNode>("rhs"));
    FunctionDeclNode function(
        "compute", {"A", "B"}, std::make_unique<FloatLiteralNode>(0.0));
    FusedKernelNode fusedKernel(std::make_unique<FloatLiteralNode>(1.0));

    RecordingVisitor visitor;
    literal.accept(visitor);
    variable.accept(visitor);
    binary.accept(visitor);
    function.accept(visitor);
    fusedKernel.accept(visitor);

    expect(visitor.floatVisits == 1, "float visitor overload was not selected");
    expect(visitor.variableVisits == 1, "variable visitor overload was not selected");
    expect(visitor.binaryVisits == 1, "binary visitor overload was not selected");
    expect(visitor.functionVisits == 1, "function visitor overload was not selected");
    expect(visitor.fusedKernelVisits == 1,
           "fused-kernel visitor overload was not selected");
}

void testAccessorsAndOwnership() {
    FloatLiteralNode literal(3.25);
    VariableNode variable("input");
    expect(literal.value() == 3.25, "incorrect float value");
    expect(variable.name() == "input", "incorrect variable name");

    auto lhs = std::make_unique<VariableNode>("A");
    auto rhs = std::make_unique<VariableNode>("B");
    const ASTNode* const lhsAddress = lhs.get();
    const ASTNode* const rhsAddress = rhs.get();
    BinaryOpNode binary(TokenType::Slash, std::move(lhs), std::move(rhs));
    expect(binary.operatorType() == TokenType::Slash, "incorrect binary operator");
    expect(&binary.lhs() == lhsAddress, "left child ownership was not transferred");
    expect(&binary.rhs() == rhsAddress, "right child ownership was not transferred");

    auto takenLhs = binary.takeLhs();
    auto takenRhs = binary.takeRhs();
    expect(takenLhs.get() == lhsAddress, "left child could not be taken for rewriting");
    expect(takenRhs.get() == rhsAddress, "right child could not be taken for rewriting");
    expectInvalidArgument([&] { binary.setLhs(nullptr); });
    expectInvalidArgument([&] { binary.setRhs(nullptr); });
    binary.setLhs(std::move(takenLhs));
    binary.setRhs(std::move(takenRhs));
    expect(&binary.lhs() == lhsAddress, "left child was not restored");
    expect(&binary.rhs() == rhsAddress, "right child was not restored");

    auto body = std::make_unique<VariableNode>("A");
    const ASTNode* const bodyAddress = body.get();
    FunctionDeclNode function("compute", {"A", "B"}, std::move(body));
    expect(function.name() == "compute", "incorrect function name");
    expect(function.arguments() == std::vector<std::string>({"A", "B"}),
           "incorrect function arguments");
    expect(&function.body() == bodyAddress, "function body ownership was not transferred");

    auto takenBody = function.takeBody();
    expect(takenBody.get() == bodyAddress, "function body could not be taken for rewriting");
    expectInvalidArgument([&] { function.setBody(nullptr); });
    function.setBody(std::move(takenBody));
    expect(&function.body() == bodyAddress, "function body was not restored");

    auto fusedExpression = std::make_unique<VariableNode>("A");
    const ASTNode* const fusedExpressionAddress = fusedExpression.get();
    FusedKernelNode fusedKernel(std::move(fusedExpression));
    expect(&fusedKernel.fusedExpression() == fusedExpressionAddress,
           "fused expression ownership was not transferred");
    auto takenExpression = fusedKernel.takeFusedExpression();
    expect(takenExpression.get() == fusedExpressionAddress,
           "fused expression could not be taken for rewriting");
    expectInvalidArgument([&] { fusedKernel.setFusedExpression(nullptr); });
    fusedKernel.setFusedExpression(std::move(takenExpression));
    expect(&fusedKernel.fusedExpression() == fusedExpressionAddress,
           "fused expression was not restored");
}

void testFunctionPrinterOutput() {
    FunctionDeclNode function("compute", {"A", "B"}, makeSampleExpression());
    std::ostringstream output;
    ASTPrinter printer(output);
    function.accept(printer);

    constexpr std::string_view expected =
        "FunctionDeclNode: compute(A, B)\n"
        "  Body: BinaryOpNode: +\n"
        "    Left: BinaryOpNode: @\n"
        "      Left: VariableNode: A\n"
        "      Right: VariableNode: B\n"
        "    Right: FloatLiteralNode: 2.0\n";
    expect(output.str() == expected, "function tree output does not match");
}

void testFusedKernelPrinterOutput() {
    FusedKernelNode fusedKernel(makeSampleExpression());
    std::ostringstream output;
    ASTPrinter printer(output);
    fusedKernel.accept(printer);

    constexpr std::string_view expected =
        "[Fused Kernel]\n"
        "  BinaryOpNode: +\n"
        "    Left: BinaryOpNode: @\n"
        "      Left: VariableNode: A\n"
        "      Right: VariableNode: B\n"
        "    Right: FloatLiteralNode: 2.0\n";
    expect(output.str() == expected, "fused-kernel tree output does not match");
}

void testOperatorSpellings() {
    expect(binaryOperatorSpelling(TokenType::Plus) == "+", "incorrect plus spelling");
    expect(binaryOperatorSpelling(TokenType::Minus) == "-", "incorrect minus spelling");
    expect(binaryOperatorSpelling(TokenType::Star) == "*", "incorrect star spelling");
    expect(binaryOperatorSpelling(TokenType::Slash) == "/", "incorrect slash spelling");
    expect(binaryOperatorSpelling(TokenType::MatrixMultiply) == "@",
           "incorrect matrix multiply spelling");
    expect(binaryOperatorSpelling(TokenType::Equal) == "=", "incorrect equal spelling");
}

void testInvalidTrees() {
    expectInvalidArgument([] {
        BinaryOpNode invalid(
            TokenType::Def,
            std::make_unique<FloatLiteralNode>(1.0),
            std::make_unique<FloatLiteralNode>(2.0));
    });
    expectInvalidArgument([] {
        BinaryOpNode invalid(
            TokenType::Plus, nullptr, std::make_unique<FloatLiteralNode>(2.0));
    });
    expectInvalidArgument([] {
        BinaryOpNode invalid(
            TokenType::Plus, std::make_unique<FloatLiteralNode>(1.0), nullptr);
    });
    expectInvalidArgument([] {
        FunctionDeclNode invalid("", {}, std::make_unique<FloatLiteralNode>(1.0));
    });
    expectInvalidArgument([] {
        FunctionDeclNode invalid("compute", {}, nullptr);
    });
    expectInvalidArgument([] {
        FusedKernelNode invalid(nullptr);
    });
    expectInvalidArgument([] {
        static_cast<void>(binaryOperatorSpelling(TokenType::Identifier));
    });
}

} // namespace

int main() {
    try {
        testSamplePrinterOutput();
        testDoubleDispatch();
        testAccessorsAndOwnership();
        testFunctionPrinterOutput();
        testFusedKernelPrinterOutput();
        testOperatorSpellings();
        testInvalidTrees();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All AST tests passed.\n";
    return EXIT_SUCCESS;
}
