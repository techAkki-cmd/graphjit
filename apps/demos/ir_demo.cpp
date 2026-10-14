#include "graphjit/backend/ir_generator.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

int main() {
    using graphjit::analysis::ShapeInferenceVisitor;
    using graphjit::analysis::SymbolTable;
    using graphjit::analysis::TensorShape;
    using graphjit::ast::BinaryOpNode;
    using graphjit::ast::VariableNode;
    using graphjit::backend::IRGenerator;
    using graphjit::frontend::TokenType;

    try {
        auto expression = std::make_unique<BinaryOpNode>(
            TokenType::MatrixMultiply, std::make_unique<VariableNode>("A"),
            std::make_unique<VariableNode>("B"));

        ShapeInferenceVisitor inference(SymbolTable{
            {"A", TensorShape{{2, 3}}},
            {"B", TensorShape{{3, 2}}},
        });
        static_cast<void>(inference.infer(*expression));

        IRGenerator generator(inference.annotations());
        static_cast<void>(
            generator.compileTensor(expression.get(), {"A", "B"}));
        generator.module().print(llvm::outs(), nullptr);
        llvm::outs().flush();
    } catch (const std::exception& error) {
        std::cerr << "IR generation failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
