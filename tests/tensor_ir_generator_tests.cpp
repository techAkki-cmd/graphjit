#include "graphjit/ast/ast.hpp"
#include "graphjit/backend/ir_generator.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <llvm/IR/Function.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using graphjit::analysis::ShapeAnnotations;
using graphjit::analysis::ShapeInferenceVisitor;
using graphjit::analysis::SymbolTable;
using graphjit::analysis::TensorShape;
using graphjit::ast::ASTNode;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::FloatLiteralNode;
using graphjit::ast::FusedKernelNode;
using graphjit::ast::VariableNode;
using graphjit::backend::IRGenerator;
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
void expectException(Callable&& callable,
                     const std::string_view expectedMessage) {
    try {
        std::forward<Callable>(callable)();
        fail("expected exception");
    } catch (const Exception& error) {
        expect(std::string_view(error.what()).find(expectedMessage) !=
                   std::string_view::npos,
               "exception message lacks expected detail: " +
                   std::string(error.what()));
    }
}

[[nodiscard]] std::unique_ptr<BinaryOpNode>
binary(const TokenType operation, std::unique_ptr<ASTNode> lhs,
       std::unique_ptr<ASTNode> rhs) {
    return std::make_unique<BinaryOpNode>(operation, std::move(lhs),
                                          std::move(rhs));
}

[[nodiscard]] std::unique_ptr<BinaryOpNode> matrixExpression() {
    return binary(TokenType::MatrixMultiply,
                  std::make_unique<VariableNode>("A"),
                  std::make_unique<VariableNode>("B"));
}

[[nodiscard]] std::unique_ptr<BinaryOpNode> additionExpression() {
    return binary(TokenType::Plus, std::make_unique<VariableNode>("A"),
                  std::make_unique<VariableNode>("B"));
}

[[nodiscard]] ShapeAnnotations infer(ASTNode& root, SymbolTable symbols) {
    ShapeInferenceVisitor inference(std::move(symbols));
    static_cast<void>(inference.infer(root));
    return inference.annotations();
}

[[nodiscard]] std::string moduleText(const IRGenerator& generator) {
    std::string text;
    llvm::raw_string_ostream output(text);
    generator.module().print(output, nullptr);
    output.flush();
    return text;
}

[[nodiscard]] std::size_t occurrences(const std::string_view text,
                                      const std::string_view pattern) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(pattern, offset)) != std::string_view::npos) {
        ++count;
        offset += pattern.size();
    }
    return count;
}

void testMatrixMultiplication() {
    auto root = matrixExpression();
    IRGenerator generator(infer(*root, SymbolTable{
                                           {"A", TensorShape{{2, 3}}},
                                           {"B", TensorShape{{3, 2}}},
                                       }));
    llvm::Function* const function =
        generator.compileTensor(root.get(), {"A", "B"});

    expect(function != nullptr, "tensor compile returned null");
    expect(function->getName() == "__tensor_expr",
           "tensor function has wrong name");
    expect(function->getReturnType()->isVoidTy(),
           "tensor function must return void");
    expect(function->arg_size() == 3,
           "tensor function must have two inputs and output");
    expect(!llvm::verifyFunction(*function),
           "matrix multiplication IR failed verification");

    auto argument = function->arg_begin();
    expect(argument->getName() == "A_ptr", "first argument has wrong name");
    ++argument;
    expect(argument->getName() == "B_ptr", "second argument has wrong name");
    ++argument;
    expect(argument->getName() == "Out_ptr", "output argument has wrong name");

    const std::string ir = moduleText(generator);
    expect(ir.find("define void @__tensor_expr(ptr %A_ptr, ptr %B_ptr, ptr "
                   "%Out_ptr)") != std::string::npos,
           "tensor ABI is not visible in printed IR");
    expect(ir.find("loop.cond.i") != std::string::npos, "missing i loop");
    expect(ir.find("loop.cond.j") != std::string::npos, "missing j loop");
    expect(ir.find("loop.cond.k") != std::string::npos, "missing k loop");
    expect(occurrences(ir, "phi i64") == 3,
           "matmul should emit three i64 PHIs");
    expect(occurrences(ir, "icmp slt i64") == 3,
           "matmul should emit three signed bounds checks");
    expect(ir.find("getelementptr double") != std::string::npos,
           "missing flat-address GEP");
    expect(ir.find("fmul double") != std::string::npos,
           "missing dot-product multiply");
    expect(ir.find("fadd double") != std::string::npos,
           "missing dot-product accumulation");
    expect(ir.find("store double") != std::string::npos,
           "missing output stores");
}

void testTensorAddition() {
    auto root = additionExpression();
    IRGenerator generator(infer(*root, SymbolTable{
                                           {"A", TensorShape{{4, 8}}},
                                           {"B", TensorShape{{4, 8}}},
                                       }));
    llvm::Function* const function =
        generator.compileTensor(root.get(), {"A", "B"});

    expect(!llvm::verifyFunction(*function),
           "tensor addition IR failed verification");
    const std::string ir = moduleText(generator);
    expect(occurrences(ir, "phi i64") == 1, "addition should emit one loop");
    expect(ir.find("icmp slt i64 %element, 32") != std::string::npos,
           "addition loop has wrong flattened bound");
    expect(ir.find("fadd double") != std::string::npos, "addition lacks fadd");
    expect(ir.find("%Out_ptr") != std::string::npos,
           "addition does not use caller output");
    expect(ir.find("alloca double") == std::string::npos,
           "root addition should not allocate an intermediate");
}

void testNestedAdditionUsesTemporary() {
    auto root =
        binary(TokenType::Plus,
               binary(TokenType::Plus, std::make_unique<VariableNode>("A"),
                      std::make_unique<VariableNode>("B")),
               std::make_unique<VariableNode>("C"));
    IRGenerator generator(infer(*root, SymbolTable{
                                           {"A", TensorShape{{2, 2}}},
                                           {"B", TensorShape{{2, 2}}},
                                           {"C", TensorShape{{2, 2}}},
                                       }));
    llvm::Function* const function =
        generator.compileTensor(root.get(), {"A", "B", "C"});

    expect(!llvm::verifyFunction(*function),
           "nested addition IR failed verification");
    const std::string ir = moduleText(generator);
    expect(ir.find("alloca double, i64 4") != std::string::npos,
           "nested addition did not allocate a fixed-size temporary");
    expect(occurrences(ir, "phi i64") == 2,
           "nested addition should emit two loops");
    expect(occurrences(ir, "fadd double") == 2,
           "nested addition should emit two fadds");
}

void testFusedKernelPassthrough() {
    auto root = std::make_unique<FusedKernelNode>(additionExpression());
    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{2, 2}}},
        {"B", TensorShape{{2, 2}}},
    });
    static_cast<void>(inference.infer(*root));

    IRGenerator generator(inference.annotations());
    llvm::Function* const function =
        generator.compileTensor(root.get(), {"A", "B"});
    expect(!llvm::verifyFunction(*function),
           "tensor fused-kernel passthrough failed verification");
    expect(moduleText(generator).find("fadd double") != std::string::npos,
           "tensor fused kernel did not lower its inner addition");
}

void testInputAndShapeValidation() {
    auto root = additionExpression();
    IRGenerator noShapes;
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(noShapes.compileTensor(root.get(), {"A", "B"}));
        },
        "shape annotation");

    ShapeAnnotations annotations = infer(*root, SymbolTable{
                                                    {"A", TensorShape{{2, 2}}},
                                                    {"B", TensorShape{{2, 2}}},
                                                });
    IRGenerator duplicates(annotations);
    expectException<std::invalid_argument>(
        [&] {
            static_cast<void>(duplicates.compileTensor(root.get(), {"A", "A"}));
        },
        "duplicate tensor input");

    IRGenerator missingInput(annotations);
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(missingInput.compileTensor(root.get(), {"A"}));
        },
        "undefined tensor variable 'B'");
    expect(missingInput.module().getFunction("__tensor_expr") == nullptr,
           "failed compile left a partial tensor function");
}

void testInvalidShapesAndOperators() {
    auto invalidRank = matrixExpression();
    ShapeAnnotations rankAnnotations{
        {&invalidRank->lhs(), TensorShape{{6}}},
        {&invalidRank->rhs(), TensorShape{{3, 2}}},
        {invalidRank.get(), TensorShape{{2, 2}}},
    };
    IRGenerator rankGenerator(std::move(rankAnnotations));
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(
                rankGenerator.compileTensor(invalidRank.get(), {"A", "B"}));
        },
        "rank-2");

    auto incompatible = matrixExpression();
    ShapeAnnotations incompatibleAnnotations{
        {&incompatible->lhs(), TensorShape{{2, 3}}},
        {&incompatible->rhs(), TensorShape{{4, 2}}},
        {incompatible.get(), TensorShape{{2, 2}}},
    };
    IRGenerator incompatibleGenerator(std::move(incompatibleAnnotations));
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(incompatibleGenerator.compileTensor(
                incompatible.get(), {"A", "B"}));
        },
        "invalid matrix multiplication shapes");

    auto multiply = binary(TokenType::Star, std::make_unique<VariableNode>("A"),
                           std::make_unique<VariableNode>("B"));
    IRGenerator multiplyGenerator(
        infer(*multiply, SymbolTable{
                             {"A", TensorShape{{2, 2}}},
                             {"B", TensorShape{{2, 2}}},
                         }));
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(
                multiplyGenerator.compileTensor(multiply.get(), {"A", "B"}));
        },
        "not supported in Phase 6");

    auto broadcast = binary(TokenType::Plus,
                            std::make_unique<VariableNode>("A"),
                            std::make_unique<FloatLiteralNode>(2.0));
    IRGenerator broadcastGenerator(
        infer(*broadcast, SymbolTable{{"A", TensorShape{{2, 2}}}}));
    expectException<std::runtime_error>(
        [&] {
            static_cast<void>(
                broadcastGenerator.compileTensor(broadcast.get(), {"A"}));
        },
        "scalar/tensor mixed addition");

    auto overflow = additionExpression();
    const TensorShape huge{{std::numeric_limits<std::size_t>::max(), 2}};
    ShapeAnnotations overflowAnnotations{
        {&overflow->lhs(), huge},
        {&overflow->rhs(), huge},
        {overflow.get(), huge},
    };
    IRGenerator overflowGenerator(std::move(overflowAnnotations));
    expectException<std::overflow_error>(
        [&] {
            static_cast<void>(
                overflowGenerator.compileTensor(overflow.get(), {"A", "B"}));
        },
        "exceeds addressable storage");
}

void testFailedCompileCanBeRetried() {
    auto root = additionExpression();
    IRGenerator generator(infer(*root, SymbolTable{
                                           {"A", TensorShape{{2, 2}}},
                                           {"B", TensorShape{{2, 2}}},
                                       }));

    expectException<std::runtime_error>(
        [&] { static_cast<void>(generator.compileTensor(root.get(), {"A"})); },
        "undefined tensor variable");
    llvm::Function* const function =
        generator.compileTensor(root.get(), {"A", "B"});
    expect(!llvm::verifyFunction(*function),
           "retry after failure produced invalid IR");
    expectException<std::logic_error>(
        [&] {
            static_cast<void>(generator.compileTensor(root.get(), {"A", "B"}));
        },
        "already compiled a tensor expression");
}

} // namespace

int main() {
    try {
        testMatrixMultiplication();
        testTensorAddition();
        testNestedAdditionUsesTemporary();
        testFusedKernelPassthrough();
        testInputAndShapeValidation();
        testInvalidShapesAndOperators();
        testFailedCompileCanBeRetried();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All tensor LLVM IR generator tests passed.\n";
    return EXIT_SUCCESS;
}
