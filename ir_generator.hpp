// CMake (Homebrew LLVM):
//   cmake -S . -B build -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm"
// Generic LLVM installation:
//   cmake -S . -B build -DLLVM_DIR="$(llvm-config --cmakedir)"
//   cmake --build build --target graphjit_ir_demo graphjit_ir_tests
// Direct compiler/linker flags are available from:
//   llvm-config --cxxflags --ldflags --system-libs --libs core support

#pragma once

#include "ast.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace graphjit::backend {

/// Lowers one scalar expression AST into a verified LLVM function.
class IRGenerator final : public ast::ASTVisitor {
public:
    IRGenerator();

    IRGenerator(const IRGenerator&) = delete;
    IRGenerator& operator=(const IRGenerator&) = delete;
    IRGenerator(IRGenerator&&) = delete;
    IRGenerator& operator=(IRGenerator&&) = delete;

    /// Creates `double @__anon_expr()` for root. The generator is one-shot after
    /// a successful compile; failed attempts are removed and may be retried.
    [[nodiscard]] llvm::Function* compile(ast::ASTNode* root);

    [[nodiscard]] llvm::Module& module() noexcept;
    [[nodiscard]] const llvm::Module& module() const noexcept;

    void visit(ast::FloatLiteralNode& node) override;
    void visit(ast::VariableNode& node) override;
    void visit(ast::BinaryOpNode& node) override;
    void visit(ast::FunctionDeclNode& node) override;

private:
    std::unique_ptr<llvm::LLVMContext> context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    llvm::Value* currentValue_{nullptr};
    std::unordered_map<std::string, llvm::Value*> namedValues_;
};

} // namespace graphjit::backend
