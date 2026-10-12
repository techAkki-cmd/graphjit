#include "ir_generator.hpp"

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace graphjit::backend {
namespace {

constexpr const char* moduleName = "GraphJIT_JIT_Module";
constexpr const char* expressionFunctionName = "__anon_expr";

} // namespace

IRGenerator::IRGenerator()
    : context_(std::make_unique<llvm::LLVMContext>()),
      module_(std::make_unique<llvm::Module>(moduleName, *context_)),
      builder_(std::make_unique<llvm::IRBuilder<>>(*context_)) {
    module_->setSourceFileName(moduleName);
}

llvm::Function* IRGenerator::compile(ast::ASTNode* const root) {
    if (root == nullptr) {
        throw std::invalid_argument("cannot generate LLVM IR for a null AST root");
    }
    if (module_->getFunction(expressionFunctionName) != nullptr) {
        throw std::logic_error(
            "IRGenerator has already compiled an anonymous expression");
    }

    llvm::Type* const doubleType = llvm::Type::getDoubleTy(*context_);
    llvm::FunctionType* const functionType =
        llvm::FunctionType::get(doubleType, false);
    llvm::Function* const function = llvm::Function::Create(
        functionType,
        llvm::Function::ExternalLinkage,
        expressionFunctionName,
        module_.get());
    llvm::BasicBlock* const entry =
        llvm::BasicBlock::Create(*context_, "entry", function);

    builder_->SetInsertPoint(entry);
    currentValue_ = nullptr;
    namedValues_.clear();

    try {
        root->accept(*this);
        if (currentValue_ == nullptr) {
            throw std::runtime_error(
                "IR generation completed without producing a scalar value");
        }
        if (!currentValue_->getType()->isDoubleTy()) {
            throw std::runtime_error(
                "anonymous expressions must lower to an LLVM double value");
        }

        builder_->CreateRet(currentValue_);

        std::string verifierDiagnostics;
        llvm::raw_string_ostream diagnosticsStream(verifierDiagnostics);
        if (llvm::verifyFunction(*function, &diagnosticsStream)) {
            diagnosticsStream.flush();
            throw std::runtime_error(
                "LLVM verification failed for @__anon_expr:\n" + verifierDiagnostics);
        }

        builder_->ClearInsertionPoint();
        namedValues_.clear();
        return function;
    } catch (...) {
        builder_->ClearInsertionPoint();
        currentValue_ = nullptr;
        namedValues_.clear();
        function->eraseFromParent();
        throw;
    }
}

llvm::Module& IRGenerator::module() noexcept {
    return *module_;
}

const llvm::Module& IRGenerator::module() const noexcept {
    return *module_;
}

void IRGenerator::visit(ast::FloatLiteralNode& node) {
    currentValue_ = llvm::ConstantFP::get(*context_, llvm::APFloat(node.value()));
}

void IRGenerator::visit(ast::VariableNode& node) {
    const std::string name(node.name());
    const auto value = namedValues_.find(name);
    if (value == namedValues_.end()) {
        throw std::runtime_error(
            "LLVM IR generation failed: undefined scalar variable '" + name + "'");
    }
    currentValue_ = value->second;
}

void IRGenerator::visit(ast::BinaryOpNode& node) {
    node.lhs().accept(*this);
    llvm::Value* const lhs = currentValue_;

    node.rhs().accept(*this);
    llvm::Value* const rhs = currentValue_;

    if (lhs == nullptr || rhs == nullptr) {
        throw std::runtime_error(
            "binary expression operand did not produce an LLVM value");
    }
    if (!lhs->getType()->isDoubleTy() || !rhs->getType()->isDoubleTy()) {
        throw std::runtime_error(
            "scalar binary operators require two LLVM double operands");
    }

    switch (node.operatorType()) {
    case frontend::TokenType::Plus:
        currentValue_ = builder_->CreateFAdd(lhs, rhs, "addtmp");
        return;
    case frontend::TokenType::Minus:
        currentValue_ = builder_->CreateFSub(lhs, rhs, "subtmp");
        return;
    case frontend::TokenType::Star:
        currentValue_ = builder_->CreateFMul(lhs, rhs, "multmp");
        return;
    case frontend::TokenType::Slash:
        currentValue_ = builder_->CreateFDiv(lhs, rhs, "divtmp");
        return;
    case frontend::TokenType::MatrixMultiply:
        throw std::runtime_error(
            "Tensor matrix multiplication IR generation is deferred to Phase 6");
    case frontend::TokenType::Equal:
        throw std::runtime_error(
            "Assignment IR generation is not supported for anonymous expressions");
    default:
        throw std::runtime_error("Unsupported binary operator during LLVM IR generation");
    }
}

void IRGenerator::visit(ast::FunctionDeclNode&) {
    throw std::runtime_error(
        "Function declaration IR generation is not supported in Phase 5");
}

} // namespace graphjit::backend
