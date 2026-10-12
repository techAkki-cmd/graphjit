#include "ir_generator.hpp"

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace graphjit::backend {
namespace {

constexpr const char* moduleName = "GraphJIT_JIT_Module";
constexpr const char* expressionFunctionName = "__anon_expr";
constexpr const char* tensorFunctionName = "__tensor_expr";

[[nodiscard]] llvm::ConstantInt* indexConstant(llvm::LLVMContext& context,
                                               const std::size_t value) {
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context),
                                  static_cast<std::uint64_t>(value));
}

} // namespace

IRGenerator::IRGenerator()
    : context_(std::make_unique<llvm::LLVMContext>()),
      module_(std::make_unique<llvm::Module>(moduleName, *context_)),
      builder_(std::make_unique<llvm::IRBuilder<>>(*context_)) {
    module_->setSourceFileName(moduleName);
}

IRGenerator::IRGenerator(analysis::ShapeAnnotations shapeAnnotations)
    : IRGenerator() {
    shapeAnnotations_ = std::move(shapeAnnotations);
}

llvm::Function* IRGenerator::compile(ast::ASTNode* const root) {
    if (root == nullptr) {
        throw std::invalid_argument(
            "cannot generate LLVM IR for a null AST root");
    }
    if (module_->getFunction(expressionFunctionName) != nullptr) {
        throw std::logic_error(
            "IRGenerator has already compiled an anonymous expression");
    }

    llvm::Type* const doubleType = llvm::Type::getDoubleTy(*context_);
    llvm::FunctionType* const functionType =
        llvm::FunctionType::get(doubleType, false);
    llvm::Function* const function =
        llvm::Function::Create(functionType, llvm::Function::ExternalLinkage,
                               expressionFunctionName, module_.get());
    llvm::BasicBlock* const entry =
        llvm::BasicBlock::Create(*context_, "entry", function);

    resetTransientState();
    activeFunction_ = function;
    builder_->SetInsertPoint(entry);

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
                "LLVM verification failed for @__anon_expr:\n" +
                verifierDiagnostics);
        }

        resetTransientState();
        return function;
    } catch (...) {
        resetTransientState();
        function->eraseFromParent();
        throw;
    }
}

llvm::Function*
IRGenerator::compileTensor(ast::ASTNode* const root,
                           std::vector<std::string> inputNames) {
    if (root == nullptr) {
        throw std::invalid_argument(
            "cannot generate tensor LLVM IR for a null AST root");
    }
    if (module_->getFunction(tensorFunctionName) != nullptr) {
        throw std::logic_error(
            "IRGenerator has already compiled a tensor expression");
    }
    const analysis::TensorShape& rootShape = shapeOf(*root);
    if (rootShape.isScalar()) {
        throw std::invalid_argument("compileTensor requires a non-scalar root "
                                    "shape; use compile for scalars");
    }

    std::unordered_set<std::string> uniqueNames;
    for (const std::string& name : inputNames) {
        if (name.empty()) {
            throw std::invalid_argument("tensor input names must not be empty");
        }
        if (!uniqueNames.insert(name).second) {
            throw std::invalid_argument("duplicate tensor input name '" + name +
                                        "'");
        }
    }

    llvm::Type* const pointerType = llvm::PointerType::getUnqual(*context_);
    std::vector<llvm::Type*> parameterTypes(inputNames.size() + 1, pointerType);
    llvm::FunctionType* const functionType = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context_), parameterTypes, false);
    llvm::Function* const function =
        llvm::Function::Create(functionType, llvm::Function::ExternalLinkage,
                               tensorFunctionName, module_.get());

    resetTransientState();
    tensorMode_ = true;
    activeFunction_ = function;

    auto argument = function->arg_begin();
    for (const std::string& name : inputNames) {
        argument->setName(name + "_ptr");
        namedValues_.emplace(name, &*argument);
        ++argument;
    }
    llvm::Argument* const output = &*argument;
    output->setName("Out_ptr");
    requestedDestination_ = output;

    llvm::BasicBlock* const entry =
        llvm::BasicBlock::Create(*context_, "entry", function);
    builder_->SetInsertPoint(entry);

    try {
        root->accept(*this);
        if (currentValue_ != output) {
            throw std::runtime_error("tensor root did not lower into the "
                                     "caller-owned output buffer");
        }
        llvm::BasicBlock* const exitBlock = builder_->GetInsertBlock();
        if (exitBlock == nullptr || exitBlock->hasTerminator()) {
            throw std::runtime_error(
                "tensor lowering left an invalid LLVM insertion point");
        }
        builder_->CreateRetVoid();

        std::string verifierDiagnostics;
        llvm::raw_string_ostream diagnosticsStream(verifierDiagnostics);
        if (llvm::verifyFunction(*function, &diagnosticsStream)) {
            diagnosticsStream.flush();
            throw std::runtime_error(
                "LLVM verification failed for @__tensor_expr:\n" +
                verifierDiagnostics);
        }

        resetTransientState();
        return function;
    } catch (...) {
        resetTransientState();
        function->eraseFromParent();
        throw;
    }
}

llvm::Module& IRGenerator::module() noexcept { return *module_; }

const llvm::Module& IRGenerator::module() const noexcept { return *module_; }

void IRGenerator::visit(ast::FloatLiteralNode& node) {
    if (tensorMode_) {
        const analysis::TensorShape& shape = shapeOf(node);
        if (!shape.isScalar()) {
            throw std::runtime_error("float literal has non-scalar shape " +
                                     shape.toString());
        }
    }
    currentValue_ =
        llvm::ConstantFP::get(*context_, llvm::APFloat(node.value()));
}

void IRGenerator::visit(ast::VariableNode& node) {
    const std::string name(node.name());
    if (tensorMode_) {
        static_cast<void>(shapeOf(node));
    }
    const auto value = namedValues_.find(name);
    if (value == namedValues_.end()) {
        throw std::runtime_error(
            "LLVM IR generation failed: undefined " +
            std::string(tensorMode_ ? "tensor" : "scalar") + " variable '" +
            name + "'");
    }
    currentValue_ = value->second;
}

void IRGenerator::visit(ast::BinaryOpNode& node) {
    if (tensorMode_ && !shapeOf(node).isScalar()) {
        llvm::Value* const destinationRequest = requestedDestination_;

        requestedDestination_ = nullptr;
        node.lhs().accept(*this);
        llvm::Value* const lhs = currentValue_;

        requestedDestination_ = nullptr;
        node.rhs().accept(*this);
        llvm::Value* const rhs = currentValue_;

        if (lhs == nullptr || rhs == nullptr) {
            throw std::runtime_error("tensor binary expression operand did not "
                                     "produce an LLVM value");
        }

        llvm::Value* const destination =
            destinationRequest != nullptr
                ? destinationRequest
                : createTemporaryBuffer(shapeOf(node), "tensor.tmp");

        switch (node.operatorType()) {
        case frontend::TokenType::Plus:
            lowerTensorAddition(node, lhs, rhs, destination);
            break;
        case frontend::TokenType::MatrixMultiply:
            lowerMatrixMultiply(node, lhs, rhs, destination);
            break;
        case frontend::TokenType::Minus:
        case frontend::TokenType::Star:
        case frontend::TokenType::Slash:
            throw std::runtime_error(
                "tensor operator '" +
                std::string(ast::binaryOperatorSpelling(node.operatorType())) +
                "' lowering is not supported in Phase 6");
        case frontend::TokenType::Equal:
            throw std::runtime_error(
                "tensor assignment lowering is not supported in Phase 6");
        default:
            throw std::runtime_error(
                "unsupported tensor binary operator during LLVM IR generation");
        }

        requestedDestination_ = nullptr;
        currentValue_ = destination;
        return;
    }

    requestedDestination_ = nullptr;
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
        throw std::runtime_error("matrix multiplication requires tensor "
                                 "operands and shape annotations");
    case frontend::TokenType::Equal:
        throw std::runtime_error("Assignment IR generation is not supported "
                                 "for anonymous expressions");
    default:
        throw std::runtime_error(
            "Unsupported binary operator during LLVM IR generation");
    }
}

void IRGenerator::visit(ast::FunctionDeclNode&) {
    throw std::runtime_error(
        "Function declaration IR generation is not supported in Phase 6");
}

void IRGenerator::createLoop(llvm::Value* const start, llvm::Value* const end,
                             const std::string& varName,
                             llvm::BasicBlock*& preheader,
                             llvm::BasicBlock*& loopBody,
                             llvm::BasicBlock*& afterLoop) {
    if (activeFunction_ == nullptr || builder_->GetInsertBlock() == nullptr) {
        throw std::logic_error(
            "cannot create an LLVM loop without an active function");
    }
    if (start == nullptr || end == nullptr ||
        !start->getType()->isIntegerTy(64) ||
        !end->getType()->isIntegerTy(64)) {
        throw std::invalid_argument("loop bounds must be non-null i64 values");
    }
    if (loopIndices_.contains(varName)) {
        throw std::logic_error("duplicate active loop name '" + varName + "'");
    }

    preheader = builder_->GetInsertBlock();
    if (preheader->hasTerminator()) {
        throw std::logic_error("loop preheader already has a terminator");
    }

    // Build the loop skeleton first. The caller fills loopBody and branches to
    // the latch when its work is complete; the builder is left in loopBody.
    llvm::BasicBlock* const condition = llvm::BasicBlock::Create(
        *context_, "loop.cond." + varName, activeFunction_);
    loopBody = llvm::BasicBlock::Create(*context_, "loop.body." + varName,
                                        activeFunction_);
    llvm::BasicBlock* const increment = llvm::BasicBlock::Create(
        *context_, "loop.inc." + varName, activeFunction_);
    afterLoop = llvm::BasicBlock::Create(*context_, "loop.end." + varName,
                                         activeFunction_);

    builder_->CreateBr(condition);
    builder_->SetInsertPoint(condition);
    llvm::PHINode* const index =
        builder_->CreatePHI(llvm::Type::getInt64Ty(*context_), 2, varName);
    index->addIncoming(start, preheader);
    llvm::Value* const keepRunning =
        builder_->CreateICmpSLT(index, end, varName + ".cond");
    builder_->CreateCondBr(keepRunning, loopBody, afterLoop);

    builder_->SetInsertPoint(increment);
    llvm::Value* const next = builder_->CreateAdd(
        index, indexConstant(*context_, 1), varName + ".next");
    builder_->CreateBr(condition);
    index->addIncoming(next, increment);

    loopIndices_.emplace(varName, index);
    loopLatches_.emplace(varName, increment);
    builder_->SetInsertPoint(loopBody);
}

const analysis::TensorShape&
IRGenerator::shapeOf(const ast::ASTNode& node) const {
    const auto shape = shapeAnnotations_.find(&node);
    if (shape == shapeAnnotations_.end()) {
        throw std::runtime_error("LLVM tensor lowering requires a shape "
                                 "annotation for every AST node");
    }
    return shape->second;
}

std::size_t IRGenerator::flattenedSize(const analysis::TensorShape& shape) {
    std::size_t size = 1;
    for (const std::size_t dimension : shape.dims) {
        if (dimension != 0 &&
            size > std::numeric_limits<std::size_t>::max() / dimension) {
            throw std::overflow_error("tensor shape " + shape.toString() +
                                      " exceeds addressable storage");
        }
        size *= dimension;
    }
    return size;
}

llvm::Value*
IRGenerator::createTemporaryBuffer(const analysis::TensorShape& shape,
                                   const std::string& name) {
    if (activeFunction_ == nullptr || activeFunction_->empty()) {
        throw std::logic_error(
            "cannot allocate a tensor temporary outside a function");
    }

    // Temporaries must dominate every use, even when they are requested while
    // lowering a deeply nested expression in a later basic block.
    llvm::IRBuilder<> entryBuilder(*context_);
    llvm::BasicBlock& entry = activeFunction_->getEntryBlock();
    entryBuilder.SetInsertPoint(&entry, entry.begin());
    return entryBuilder.CreateAlloca(
        llvm::Type::getDoubleTy(*context_),
        indexConstant(*context_, flattenedSize(shape)), name);
}

llvm::PHINode* IRGenerator::loopIndex(const std::string& varName) const {
    const auto index = loopIndices_.find(varName);
    if (index == loopIndices_.end()) {
        throw std::logic_error("unknown loop induction variable '" + varName +
                               "'");
    }
    return index->second;
}

llvm::BasicBlock* IRGenerator::loopLatch(const std::string& varName) const {
    const auto latch = loopLatches_.find(varName);
    if (latch == loopLatches_.end()) {
        throw std::logic_error("unknown loop latch '" + varName + "'");
    }
    return latch->second;
}

void IRGenerator::lowerTensorAddition(ast::BinaryOpNode& node,
                                      llvm::Value* const lhs,
                                      llvm::Value* const rhs,
                                      llvm::Value* const destination) {
    const analysis::TensorShape& lhsShape = shapeOf(node.lhs());
    const analysis::TensorShape& rhsShape = shapeOf(node.rhs());
    const analysis::TensorShape& resultShape = shapeOf(node);
    if (lhsShape.isScalar() || rhsShape.isScalar()) {
        throw std::runtime_error("scalar/tensor mixed addition lowering is not "
                                 "supported in Phase 6");
    }
    if (lhsShape != rhsShape || resultShape != lhsShape) {
        throw std::runtime_error("tensor addition requires identical input and "
                                 "result shapes, received " +
                                 lhsShape.toString() + ", " +
                                 rhsShape.toString() + ", and " +
                                 resultShape.toString());
    }
    if (!lhs->getType()->isPointerTy() || !rhs->getType()->isPointerTy() ||
        !destination->getType()->isPointerTy()) {
        throw std::runtime_error(
            "tensor addition requires pointer operands and destination");
    }

    llvm::BasicBlock* preheader = nullptr;
    llvm::BasicBlock* body = nullptr;
    llvm::BasicBlock* after = nullptr;
    createLoop(indexConstant(*context_, 0),
               indexConstant(*context_, flattenedSize(resultShape)), "element",
               preheader, body, after);

    llvm::Value* const index = loopIndex("element");
    llvm::Type* const doubleType = llvm::Type::getDoubleTy(*context_);
    llvm::Value* const lhsAddress =
        builder_->CreateGEP(doubleType, lhs, index, "lhs.gep");
    llvm::Value* const rhsAddress =
        builder_->CreateGEP(doubleType, rhs, index, "rhs.gep");
    llvm::Value* const outputAddress =
        builder_->CreateGEP(doubleType, destination, index, "out.gep");
    llvm::Value* const lhsValue =
        builder_->CreateLoad(doubleType, lhsAddress, "lhs.value");
    llvm::Value* const rhsValue =
        builder_->CreateLoad(doubleType, rhsAddress, "rhs.value");
    llvm::Value* const sum =
        builder_->CreateFAdd(lhsValue, rhsValue, "tensor.add");
    builder_->CreateStore(sum, outputAddress);
    builder_->CreateBr(loopLatch("element"));
    builder_->SetInsertPoint(after);
    loopIndices_.erase("element");
    loopLatches_.erase("element");
}

void IRGenerator::lowerMatrixMultiply(ast::BinaryOpNode& node,
                                      llvm::Value* const lhs,
                                      llvm::Value* const rhs,
                                      llvm::Value* const destination) {
    const analysis::TensorShape& lhsShape = shapeOf(node.lhs());
    const analysis::TensorShape& rhsShape = shapeOf(node.rhs());
    const analysis::TensorShape& resultShape = shapeOf(node);
    if (lhsShape.dims.size() != 2 || rhsShape.dims.size() != 2 ||
        resultShape.dims.size() != 2) {
        throw std::runtime_error(
            "matrix multiplication lowering requires rank-2 "
            "input and result shapes");
    }

    const std::size_t rows = lhsShape.dims[0];
    const std::size_t inner = lhsShape.dims[1];
    const std::size_t rhsInner = rhsShape.dims[0];
    const std::size_t columns = rhsShape.dims[1];
    if (inner != rhsInner || resultShape.dims[0] != rows ||
        resultShape.dims[1] != columns) {
        throw std::runtime_error(
            "invalid matrix multiplication shapes " + lhsShape.toString() +
            " @ " + rhsShape.toString() + " -> " + resultShape.toString());
    }
    static_cast<void>(flattenedSize(lhsShape));
    static_cast<void>(flattenedSize(rhsShape));
    static_cast<void>(flattenedSize(resultShape));
    if (!lhs->getType()->isPointerTy() || !rhs->getType()->isPointerTy() ||
        !destination->getType()->isPointerTy()) {
        throw std::runtime_error(
            "matrix multiplication requires pointer operands and destination");
    }

    llvm::BasicBlock* iPreheader = nullptr;
    llvm::BasicBlock* iBody = nullptr;
    llvm::BasicBlock* iAfter = nullptr;
    createLoop(indexConstant(*context_, 0), indexConstant(*context_, rows), "i",
               iPreheader, iBody, iAfter);

    llvm::BasicBlock* jPreheader = nullptr;
    llvm::BasicBlock* jBody = nullptr;
    llvm::BasicBlock* jAfter = nullptr;
    createLoop(indexConstant(*context_, 0), indexConstant(*context_, columns),
               "j", jPreheader, jBody, jAfter);

    llvm::Value* const i = loopIndex("i");
    llvm::Value* const j = loopIndex("j");
    // C is row-major, so C[i, j] lives at i * P + j. Initialize it once before
    // entering k, then update the same slot for every dot-product term.
    llvm::Value* const outputRow =
        builder_->CreateMul(i, indexConstant(*context_, columns), "C.row");
    llvm::Value* const outputIndex =
        builder_->CreateAdd(outputRow, j, "C.index");
    llvm::Type* const doubleType = llvm::Type::getDoubleTy(*context_);
    llvm::Value* const outputAddress =
        builder_->CreateGEP(doubleType, destination, outputIndex, "C.gep");
    builder_->CreateStore(llvm::ConstantFP::get(doubleType, 0.0),
                          outputAddress);

    llvm::BasicBlock* kPreheader = nullptr;
    llvm::BasicBlock* kBody = nullptr;
    llvm::BasicBlock* kAfter = nullptr;
    createLoop(indexConstant(*context_, 0), indexConstant(*context_, inner),
               "k", kPreheader, kBody, kAfter);

    llvm::Value* const k = loopIndex("k");
    // A[i, k] is i * N + k and B[k, j] is k * P + j.
    llvm::Value* const lhsRow =
        builder_->CreateMul(i, indexConstant(*context_, inner), "A.row");
    llvm::Value* const lhsIndex = builder_->CreateAdd(lhsRow, k, "A.index");
    llvm::Value* const rhsRow =
        builder_->CreateMul(k, indexConstant(*context_, columns), "B.row");
    llvm::Value* const rhsIndex = builder_->CreateAdd(rhsRow, j, "B.index");
    llvm::Value* const lhsAddress =
        builder_->CreateGEP(doubleType, lhs, lhsIndex, "A.gep");
    llvm::Value* const rhsAddress =
        builder_->CreateGEP(doubleType, rhs, rhsIndex, "B.gep");
    llvm::Value* const lhsValue =
        builder_->CreateLoad(doubleType, lhsAddress, "A.value");
    llvm::Value* const rhsValue =
        builder_->CreateLoad(doubleType, rhsAddress, "B.value");
    llvm::Value* const product =
        builder_->CreateFMul(lhsValue, rhsValue, "product");
    llvm::Value* const accumulator =
        builder_->CreateLoad(doubleType, outputAddress, "accumulator");
    llvm::Value* const updated =
        builder_->CreateFAdd(accumulator, product, "accumulator.next");
    builder_->CreateStore(updated, outputAddress);
    builder_->CreateBr(loopLatch("k"));

    builder_->SetInsertPoint(kAfter);
    builder_->CreateBr(loopLatch("j"));
    builder_->SetInsertPoint(jAfter);
    builder_->CreateBr(loopLatch("i"));
    builder_->SetInsertPoint(iAfter);
    loopIndices_.erase("i");
    loopIndices_.erase("j");
    loopIndices_.erase("k");
    loopLatches_.erase("i");
    loopLatches_.erase("j");
    loopLatches_.erase("k");
}

void IRGenerator::resetTransientState() noexcept {
    builder_->ClearInsertionPoint();
    currentValue_ = nullptr;
    namedValues_.clear();
    activeFunction_ = nullptr;
    requestedDestination_ = nullptr;
    tensorMode_ = false;
    loopIndices_.clear();
    loopLatches_.clear();
}

} // namespace graphjit::backend
