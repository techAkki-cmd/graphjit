#include "graphjit/ast/ast.hpp"
#include "graphjit/backend/ir_generator.hpp"
#include "graphjit/jit/jit_engine.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using TensorFunc = void (*)(double*, double*, double*);
using graphjit::analysis::ShapeInferenceVisitor;
using graphjit::analysis::SymbolTable;
using graphjit::analysis::TensorShape;
using graphjit::ast::BinaryOpNode;
using graphjit::ast::VariableNode;
using graphjit::backend::IRGenerator;
using graphjit::backend::OwnedLLVMModule;
using graphjit::frontend::TokenType;
using graphjit::jit::JITEngine;

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
               "exception message lacks expected detail: " + std::string(error.what()));
    }
}

[[nodiscard]] std::unique_ptr<BinaryOpNode> makeAddition() {
    return std::make_unique<BinaryOpNode>(
        TokenType::Plus,
        std::make_unique<VariableNode>("A"),
        std::make_unique<VariableNode>("B"));
}

[[nodiscard]] OwnedLLVMModule buildAdditionModule() {
    auto expression = makeAddition();
    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{3}}},
        {"B", TensorShape{{3}}},
    });
    static_cast<void>(inference.infer(*expression));

    IRGenerator generator(inference.annotations());
    static_cast<void>(generator.compileTensor(expression.get(), {"A", "B"}));
    return generator.takeModule();
}

void testNativeExecution() {
    auto module = buildAdditionModule();
    expect(module.module != nullptr && module.context != nullptr,
           "IR generator did not transfer both LLVM owners");
    expect(&module.module->getContext() == module.context.get(),
           "transferred module and context do not match");

    JITEngine jit;
    jit.addModule(std::move(module.module), std::move(module.context));
    const TensorFunc function = jit.lookup("__tensor_expr").toPtr<TensorFunc>();

    std::vector<double> inputA{1.0, 2.0, 3.0};
    std::vector<double> inputB{10.0, 20.0, 30.0};
    std::vector<double> output(3, 0.0);
    function(inputA.data(), inputB.data(), output.data());

    expect(output == std::vector<double>({11.0, 22.0, 33.0}),
           "JIT-compiled tensor addition produced the wrong output");
}

void testTargetInitializationIsRepeatable() {
    JITEngine first;
    JITEngine second;
    expectException<std::runtime_error>(
        [&] { static_cast<void>(first.lookup("missing_from_first")); },
        "missing_from_first");
    expectException<std::runtime_error>(
        [&] { static_cast<void>(second.lookup("missing_from_second")); },
        "missing_from_second");
}

void testModuleValidation() {
    JITEngine jit;
    expectException<std::invalid_argument>(
        [&] {
            jit.addModule(nullptr, std::make_unique<llvm::LLVMContext>());
        },
        "null LLVM module");

    auto moduleContext = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("null-context", *moduleContext);
    expectException<std::invalid_argument>(
        [&] { jit.addModule(std::move(module), nullptr); },
        "null context");

    auto firstContext = std::make_unique<llvm::LLVMContext>();
    auto mismatchedModule =
        std::make_unique<llvm::Module>("mismatched", *firstContext);
    auto secondContext = std::make_unique<llvm::LLVMContext>();
    expectException<std::invalid_argument>(
        [&] {
            jit.addModule(std::move(mismatchedModule), std::move(secondContext));
        },
        "same ownership domain");
}

void testLookupValidation() {
    JITEngine jit;
    expectException<std::invalid_argument>(
        [&] { static_cast<void>(jit.lookup("")); }, "empty JIT symbol");
    expectException<std::runtime_error>(
        [&] { static_cast<void>(jit.lookup("does_not_exist")); },
        "does_not_exist");
}

void testGeneratorTransferLifecycle() {
    auto expression = makeAddition();
    ShapeInferenceVisitor inference(SymbolTable{
        {"A", TensorShape{{3}}},
        {"B", TensorShape{{3}}},
    });
    static_cast<void>(inference.infer(*expression));

    IRGenerator generator(inference.annotations());
    static_cast<void>(generator.compileTensor(expression.get(), {"A", "B"}));
    auto module = generator.takeModule();
    expect(module.module != nullptr && module.context != nullptr,
           "module transfer returned an incomplete ownership bundle");

    expectException<std::logic_error>(
        [&] { static_cast<void>(generator.module()); }, "already been transferred");
    expectException<std::logic_error>(
        [&] { static_cast<void>(generator.takeModule()); }, "already been transferred");
    expectException<std::logic_error>(
        [&] {
            static_cast<void>(
                generator.compileTensor(expression.get(), {"A", "B"}));
        },
        "after module ownership was transferred");
}

} // namespace

int main() {
    try {
        testNativeExecution();
        testTargetInitializationIsRepeatable();
        testModuleValidation();
        testLookupValidation();
        testGeneratorTransferLifecycle();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All ORC JIT engine tests passed.\n";
    return EXIT_SUCCESS;
}
