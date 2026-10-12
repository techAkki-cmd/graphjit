#include "ast.hpp"
#include "ir_generator.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FunctionDeclNode;
using graphjit::backend::IRGenerator;
using graphjit::frontend::Lexer;
using graphjit::frontend::Parser;
using graphjit::frontend::TokenType;

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

[[nodiscard]] std::unique_ptr<graphjit::ast::ASTNode> parseExpression(
    const std::string_view source) {
    Parser parser{Lexer(source)};
    return parser.parse();
}

[[nodiscard]] double returnedConstant(llvm::Function& function) {
    expect(!function.empty(), "generated function has no basic block");
    const auto* returnInstruction =
        llvm::dyn_cast<llvm::ReturnInst>(function.getEntryBlock().getTerminator());
    expect(returnInstruction != nullptr, "entry block does not end in return");

    const auto* constant =
        llvm::dyn_cast<llvm::ConstantFP>(returnInstruction->getReturnValue());
    expect(constant != nullptr, "constant expression was not folded to ConstantFP");
    return constant->getValueAPF().convertToDouble();
}

void testDemoExpression() {
    auto root = parseExpression("2.0 + 3.0 * 4.0");
    IRGenerator generator;
    llvm::Function* const function = generator.compile(root.get());

    expect(function != nullptr, "compile returned a null function");
    expect(function->getName() == "__anon_expr", "function has the wrong name");
    expect(function->arg_empty(), "anonymous expression unexpectedly has arguments");
    expect(function->getReturnType()->isDoubleTy(), "function return type is not double");
    expect(function->size() == 1, "function should contain one basic block");
    expect(!llvm::verifyFunction(*function), "generated function failed verification");
    expect(returnedConstant(*function) == 14.0, "demo expression returned the wrong value");
    expect(generator.module().getModuleIdentifier() == "GraphJIT_JIT_Module",
           "module has the wrong identifier");

    std::string ir;
    llvm::raw_string_ostream output(ir);
    generator.module().print(output, nullptr);
    output.flush();
    expect(ir.find("define double @__anon_expr()") != std::string::npos,
           "printed module does not contain the anonymous function");
    expect(ir.find("ret double") != std::string::npos,
           "printed module does not contain a double return");
}

void testScalarOperators() {
    struct Case {
        std::string_view source;
        double expected;
    };
    constexpr std::array cases{
        Case{"8.0 + 2.0", 10.0},
        Case{"8.0 - 2.0", 6.0},
        Case{"8.0 * 2.0", 16.0},
        Case{"8.0 / 2.0", 4.0},
    };

    for (const Case& testCase : cases) {
        auto root = parseExpression(testCase.source);
        IRGenerator generator;
        llvm::Function* const function = generator.compile(root.get());
        expect(std::abs(returnedConstant(*function) - testCase.expected) < 1.0e-12,
               "scalar operator produced the wrong value");
    }
}

void testInvalidInputs() {
    IRGenerator nullGenerator;
    expectException<std::invalid_argument>(
        [&] { static_cast<void>(nullGenerator.compile(nullptr)); }, "null AST root");

    auto variable = parseExpression("missing");
    IRGenerator variableGenerator;
    expectException<std::runtime_error>(
        [&] { static_cast<void>(variableGenerator.compile(variable.get())); },
        "undefined scalar variable 'missing'");
    expect(variableGenerator.module().getFunction("__anon_expr") == nullptr,
           "failed variable compilation left a function behind");

    auto matrixMultiply = parseExpression("2.0 @ 3.0");
    IRGenerator matrixGenerator;
    expectException<std::runtime_error>(
        [&] { static_cast<void>(matrixGenerator.compile(matrixMultiply.get())); },
        "requires tensor operands");
    expect(matrixGenerator.module().getFunction("__anon_expr") == nullptr,
           "failed matrix compilation left a function behind");

    BinaryOpNode assignment(
        TokenType::Equal,
        std::make_unique<FloatLiteralNode>(1.0),
        std::make_unique<FloatLiteralNode>(2.0));
    IRGenerator assignmentGenerator;
    expectException<std::runtime_error>(
        [&] { static_cast<void>(assignmentGenerator.compile(&assignment)); },
        "Assignment IR generation is not supported");

    FunctionDeclNode function(
        "compute", {}, std::make_unique<FloatLiteralNode>(1.0));
    IRGenerator functionGenerator;
    expectException<std::runtime_error>(
        [&] { static_cast<void>(functionGenerator.compile(&function)); },
        "Function declaration IR generation is not supported");
}

void testCompileLifecycle() {
    auto invalid = parseExpression("2.0 @ 3.0");
    auto valid = parseExpression("2.0 + 3.0");
    IRGenerator generator;

    expectException<std::runtime_error>(
        [&] { static_cast<void>(generator.compile(invalid.get())); }, "tensor operands");
    llvm::Function* const function = generator.compile(valid.get());
    expect(returnedConstant(*function) == 5.0,
           "generator did not recover after a failed compile");

    expectException<std::logic_error>(
        [&] { static_cast<void>(generator.compile(valid.get())); },
        "already compiled an anonymous expression");
    expect(generator.module().getFunction("__anon_expr") == function,
           "second compile modified the successful function");
}

} // namespace

int main() {
    try {
        testDemoExpression();
        testScalarOperators();
        testInvalidInputs();
        testCompileLifecycle();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All LLVM IR generator tests passed.\n";
    return EXIT_SUCCESS;
}
