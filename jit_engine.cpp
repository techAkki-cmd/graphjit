#include "jit_engine.hpp"

#include <llvm/Support/Error.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace graphjit::jit {
namespace {

[[nodiscard]] std::runtime_error llvmError(
    const std::string& operation,
    llvm::Error error) {
    return std::runtime_error(operation + ": " + llvm::toString(std::move(error)));
}

} // namespace

void JITEngine::initializeNativeTarget() {
    static std::once_flag initialization;
    std::call_once(initialization, [] {
        if (llvm::InitializeNativeTarget()) {
            throw std::runtime_error("LLVM failed to initialize the native target");
        }
        if (llvm::InitializeNativeTargetAsmPrinter()) {
            throw std::runtime_error(
                "LLVM failed to initialize the native assembly printer");
        }
    });
}

JITEngine::JITEngine() {
    initializeNativeTarget();

    auto expectedJIT = llvm::orc::LLJITBuilder().create();
    if (!expectedJIT) {
        throw llvmError("failed to create LLVM ORC LLJIT", expectedJIT.takeError());
    }
    jit_ = std::move(*expectedJIT);
}

void JITEngine::addModule(
    std::unique_ptr<llvm::Module> module,
    std::unique_ptr<llvm::LLVMContext> context) {
    if (!module) {
        throw std::invalid_argument("cannot add a null LLVM module to the JIT");
    }
    if (!context) {
        throw std::invalid_argument("cannot add an LLVM module with a null context");
    }
    if (&module->getContext() != context.get()) {
        throw std::invalid_argument(
            "LLVM module and context do not refer to the same ownership domain");
    }

    module->setDataLayout(jit_->getDataLayout());
    module->setTargetTriple(jit_->getTargetTriple());

    llvm::orc::ThreadSafeModule threadSafeModule(
        std::move(module), std::move(context));
    if (llvm::Error error = jit_->addIRModule(
            jit_->getMainJITDylib(), std::move(threadSafeModule))) {
        throw llvmError("failed to add LLVM module to ORC JIT", std::move(error));
    }
}

llvm::orc::ExecutorAddr JITEngine::lookup(const std::string& name) {
    if (name.empty()) {
        throw std::invalid_argument("cannot look up an empty JIT symbol name");
    }

    auto address = jit_->lookup(name);
    if (!address) {
        throw llvmError("failed to look up JIT symbol '" + name + "'",
                        address.takeError());
    }
    return *address;
}

} // namespace graphjit::jit
