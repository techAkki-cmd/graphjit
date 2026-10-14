// CMake (Homebrew LLVM):
//   cmake -S . -B build -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
// Generic LLVM installation:
//   cmake -S . -B build -DLLVM_DIR="$(llvm-config --cmakedir)"
//   cmake --build build --target graphjit_ir_demo graphjit_ir_tests
//   cmake --build build --target graphjit_tensor_ir_tests
// Direct compiler/linker flags are available from:
//   llvm-config --cxxflags --ldflags --system-libs --libs core support

#pragma once

#include "graphjit/ast/ast.hpp"
#include "graphjit/analysis/shape_inference.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace graphjit::backend {

/// Owns an LLVM module and the context that all of its IR references.
/// Field order ensures the module is destroyed before its context.
struct OwnedLLVMModule {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
};

/// Lowers scalar expressions and fixed-shape tensor expressions to verified IR.
class IRGenerator final : public ast::ASTVisitor {
  public:
    IRGenerator();
    explicit IRGenerator(analysis::ShapeAnnotations shapeAnnotations);

    IRGenerator(const IRGenerator&) = delete;
    IRGenerator& operator=(const IRGenerator&) = delete;
    IRGenerator(IRGenerator&&) = delete;
    IRGenerator& operator=(IRGenerator&&) = delete;

    /// Creates `double @__anon_expr()` for root. The generator is one-shot
    /// after a successful compile; failed attempts are removed and may be
    /// retried.
    [[nodiscard]] llvm::Function* compile(ast::ASTNode* root);

    /// Creates a row-major tensor kernel with one pointer argument per input
    /// and a final caller-owned output pointer.
    [[nodiscard]] llvm::Function*
    compileTensor(ast::ASTNode* root, std::vector<std::string> inputNames);

    [[nodiscard]] llvm::Module& module();
    [[nodiscard]] const llvm::Module& module() const;

    /// Atomically transfers the finished module and its context. The generator
    /// is permanently consumed after this call.
    [[nodiscard]] OwnedLLVMModule takeModule();

    void visit(ast::FloatLiteralNode& node) override;
    void visit(ast::VariableNode& node) override;
    void visit(ast::BinaryOpNode& node) override;
    void visit(ast::FunctionDeclNode& node) override;
    void visit(ast::FusedKernelNode& node) override;

  private:
    void createLoop(llvm::Value* start, llvm::Value* end,
                    const std::string& varName, llvm::BasicBlock*& preheader,
                    llvm::BasicBlock*& loopBody, llvm::BasicBlock*& afterLoop);

    [[nodiscard]] const analysis::TensorShape&
    shapeOf(const ast::ASTNode& node) const;
    [[nodiscard]] static std::size_t
    flattenedSize(const analysis::TensorShape& shape);
    [[nodiscard]] llvm::Value*
    createTemporaryBuffer(const analysis::TensorShape& shape,
                          const std::string& name);
    [[nodiscard]] llvm::PHINode* loopIndex(const std::string& varName) const;
    [[nodiscard]] llvm::BasicBlock* loopLatch(const std::string& varName) const;
    void lowerTensorAddition(ast::BinaryOpNode& node, llvm::Value* lhs,
                             llvm::Value* rhs, llvm::Value* destination);
    void lowerMatrixMultiply(ast::BinaryOpNode& node, llvm::Value* lhs,
                             llvm::Value* rhs, llvm::Value* destination);
    void resetTransientState() noexcept;

    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    llvm::Value* currentValue_{nullptr};
    std::unordered_map<std::string, llvm::Value*> namedValues_;
    analysis::ShapeAnnotations shapeAnnotations_;
    llvm::Function* activeFunction_{nullptr};
    llvm::Value* requestedDestination_{nullptr};
    bool tensorMode_{false};
    std::unordered_map<std::string, llvm::PHINode*> loopIndices_;
    std::unordered_map<std::string, llvm::BasicBlock*> loopLatches_;
};

} // namespace graphjit::backend
