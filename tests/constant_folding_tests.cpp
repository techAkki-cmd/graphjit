#include "ast.hpp"
#include "ast_printer.hpp"
#include "constant_folding.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using graphjit::ast::ASTNode;
using graphjit::ast::ASTPrinter;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FunctionDeclNode;
using graphjit::ast::VariableNode;
using graphjit::frontend::Lexer;
using graphjit::frontend::Parser;
using graphjit::frontend::TokenType;
using graphjit::opt::ConstantFoldingPass;

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

[[nodiscard]] std::unique_ptr<ASTNode> fold(std::unique_ptr<ASTNode> root) {
    ConstantFoldingPass pass;
    return pass.rewrite(std::move(root));
}

[[nodiscard]] std::string print(ASTNode& root) {
    std::ostringstream output;
    ASTPrinter printer(output);
    root.accept(printer);
    return output.str();
}

[[nodiscard]] const FloatLiteralNode& requireLiteral(const ASTNode& node) {
    const auto* const literal = dynamic_cast<const FloatLiteralNode*>(&node);
    expect(literal != nullptr, "expected a float literal node");
    return *literal;
}

void testRequestedExpression() {
    auto root = fold(parse("A @ B + (2.0 * 3.0) - (10.0 / 2.0)"));
    constexpr std::string_view expected =
        "BinaryOpNode: -\n"
        "  Left: BinaryOpNode: +\n"
        "    Left: BinaryOpNode: @\n"
        "      Left: VariableNode: A\n"
        "      Right: VariableNode: B\n"
        "    Right: FloatLiteralNode: 6.0\n"
        "  Right: FloatLiteralNode: 5.0\n";
    expect(print(*root) == expected, "requested expression was not folded correctly");
}

void testScalarOperators() {
    struct Case {
        std::string_view source;
        double expected;
    };
    constexpr Case cases[] = {
        {"8.0 + 2.0", 10.0},
        {"8.0 - 2.0", 6.0},
        {"8.0 * 2.0", 16.0},
        {"8.0 / 2.0", 4.0},
    };

    for (const Case& testCase : cases) {
        auto root = fold(parse(testCase.source));
        expect(requireLiteral(*root).value() == testCase.expected,
               "scalar operator produced the wrong folded value");
    }
}

void testDeepFolding() {
    auto root = fold(parse("(1.0 + 2.0) * 3.0"));
    expect(requireLiteral(*root).value() == 9.0,
           "deep constant expression did not collapse to one literal");
}

void testMixedAndUnsupportedOperations() {
    auto mixed = fold(parse("A + (2.0 * 3.0)"));
    auto* const mixedRoot = dynamic_cast<BinaryOpNode*>(mixed.get());
    expect(mixedRoot != nullptr, "mixed expression root was unexpectedly replaced");
    expect(dynamic_cast<VariableNode*>(&mixedRoot->lhs()) != nullptr,
           "mixed expression lost its variable");
    expect(requireLiteral(mixedRoot->rhs()).value() == 6.0,
           "constant descendant of mixed expression was not folded");

    auto matrix = fold(parse("(1.0 + 2.0) @ (3.0 * 4.0)"));
    auto* const matrixRoot = dynamic_cast<BinaryOpNode*>(matrix.get());
    expect(matrixRoot != nullptr &&
               matrixRoot->operatorType() == TokenType::MatrixMultiply,
           "matrix multiplication was incorrectly folded");
    expect(requireLiteral(matrixRoot->lhs()).value() == 3.0,
           "matrix left descendant was not folded");
    expect(requireLiteral(matrixRoot->rhs()).value() == 12.0,
           "matrix right descendant was not folded");

    auto assignment = std::make_unique<BinaryOpNode>(
        TokenType::Equal,
        parse("1.0 + 2.0"),
        parse("3.0 * 4.0"));
    auto assignmentRoot = fold(std::move(assignment));
    auto* const equal = dynamic_cast<BinaryOpNode*>(assignmentRoot.get());
    expect(equal != nullptr && equal->operatorType() == TokenType::Equal,
           "assignment was incorrectly folded");
    expect(requireLiteral(equal->lhs()).value() == 3.0,
           "assignment left descendant was not folded");
    expect(requireLiteral(equal->rhs()).value() == 12.0,
           "assignment right descendant was not folded");
}

void testNonFiniteResultsRemainExpressions() {
    auto division = fold(parse("1.0 / 0.0"));
    auto* const divisionRoot = dynamic_cast<BinaryOpNode*>(division.get());
    expect(divisionRoot != nullptr && divisionRoot->operatorType() == TokenType::Slash,
           "division by zero should remain an expression");

    auto overflow = std::make_unique<BinaryOpNode>(
        TokenType::Star,
        std::make_unique<FloatLiteralNode>(std::numeric_limits<double>::max()),
        std::make_unique<FloatLiteralNode>(2.0));
    auto overflowRoot = fold(std::move(overflow));
    expect(dynamic_cast<BinaryOpNode*>(overflowRoot.get()) != nullptr,
           "non-finite multiplication result should not be folded");
}

void testFunctionBodyAndLeafIdentity() {
    auto function = std::make_unique<FunctionDeclNode>(
        "compute", std::vector<std::string>{"A"}, parse("(2.0 + 3.0) * 4.0"));
    ASTNode* const functionAddress = function.get();
    auto root = fold(std::move(function));

    expect(root.get() == functionAddress, "function declaration identity changed");
    auto* const rewrittenFunction = dynamic_cast<FunctionDeclNode*>(root.get());
    expect(rewrittenFunction != nullptr, "function declaration was replaced");
    expect(rewrittenFunction->name() == "compute", "function name changed");
    expect(rewrittenFunction->arguments() == std::vector<std::string>{"A"},
           "function arguments changed");
    expect(requireLiteral(rewrittenFunction->body()).value() == 20.0,
           "function body was not folded");

    auto variable = std::make_unique<VariableNode>("A");
    ASTNode* const variableAddress = variable.get();
    auto rewrittenVariable = fold(std::move(variable));
    expect(rewrittenVariable.get() == variableAddress,
           "leaf rewrite should preserve node identity");
}

void testNullInput() {
    ConstantFoldingPass pass;
    expectException<std::invalid_argument>(
        [&] { static_cast<void>(pass.rewrite(nullptr)); }, "null AST node");
}

} // namespace

int main() {
    try {
        testRequestedExpression();
        testScalarOperators();
        testDeepFolding();
        testMixedAndUnsupportedOperations();
        testNonFiniteResultsRemainExpressions();
        testFunctionBodyAndLeafIdentity();
        testNullInput();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All constant folding tests passed.\n";
    return EXIT_SUCCESS;
}
