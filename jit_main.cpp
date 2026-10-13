#include "ast.hpp"
#include "ir_generator.hpp"
#include "jit_engine.hpp"
#include "shape_inference.hpp"

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void printVector(const std::string_view label, const std::vector<double>& values) {
    std::cout << label << "[ ";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << std::fixed << std::setprecision(1) << values[index];
    }
    std::cout << " ]\n";
}

} // namespace

int main() {
    using TensorFunc = void (*)(double*, double*, double*);
    using graphjit::analysis::ShapeInferenceVisitor;
    using graphjit::analysis::SymbolTable;
    using graphjit::analysis::TensorShape;
    using graphjit::ast::BinaryOpNode;
    using graphjit::ast::VariableNode;
    using graphjit::backend::IRGenerator;
    using graphjit::frontend::TokenType;
    using graphjit::jit::JITEngine;

    try {
        auto expression = std::make_unique<BinaryOpNode>(
            TokenType::Plus,
            std::make_unique<VariableNode>("A"),
            std::make_unique<VariableNode>("B"));

        ShapeInferenceVisitor inference(SymbolTable{
            {"A", TensorShape{{3}}},
            {"B", TensorShape{{3}}},
        });
        static_cast<void>(inference.infer(*expression));

        IRGenerator generator(inference.annotations());
        static_cast<void>(generator.compileTensor(expression.get(), {"A", "B"}));
        auto generatedModule = generator.takeModule();

        std::cout << "[INFO] Initializing LLVM Native Target...\n";
        JITEngine jit;
        std::cout << "[INFO] Compiling LLVM Module to memory via ORC JIT...\n";
        jit.addModule(
            std::move(generatedModule.module),
            std::move(generatedModule.context));

        std::cout << "[INFO] Executing JIT function __tensor_expr...\n\n";
        const TensorFunc function = jit.lookup("__tensor_expr").toPtr<TensorFunc>();

        std::vector<double> inputA{1.0, 2.0, 3.0};
        std::vector<double> inputB{10.0, 20.0, 30.0};
        std::vector<double> output(3, 0.0);
        function(inputA.data(), inputB.data(), output.data());

        printVector("Input A: ", inputA);
        printVector("Input B: ", inputB);
        printVector("Output:  ", output);

        const std::vector<double> expected{11.0, 22.0, 33.0};
        if (output != expected) {
            std::cerr << "\n[ERROR] JIT output did not match the expected result.\n";
            return EXIT_FAILURE;
        }

        std::cout << "\n[SUCCESS] JIT execution modified host memory correctly.\n";
    } catch (const std::exception& error) {
        std::cerr << "JIT execution failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
