// CMake:
//   cmake -S . -B build -DLLVM_DIR="$(llvm-config --cmakedir)"
//   cmake --build build --target graphjit_jit_demo graphjit_jit_tests
// Equivalent direct linker flags:
//   llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native

#pragma once

#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>

#include <memory>
#include <string>

namespace graphjit::jit {

/// Owns an in-process LLVM ORC JIT configured for the native host target.
class JITEngine final {
public:
    JITEngine();

    JITEngine(const JITEngine&) = delete;
    JITEngine& operator=(const JITEngine&) = delete;
    JITEngine(JITEngine&&) = delete;
    JITEngine& operator=(JITEngine&&) = delete;

    /// Permanently transfers a module and its matching context into ORC.
    void addModule(
        std::unique_ptr<llvm::Module> module,
        std::unique_ptr<llvm::LLVMContext> context);

    /// Resolves an unmangled LLVM IR symbol name in the main JITDylib.
    [[nodiscard]] llvm::orc::ExecutorAddr lookup(const std::string& name);

private:
    static void initializeNativeTarget();

    std::unique_ptr<llvm::orc::LLJIT> jit_;
};

} // namespace graphjit::jit
