#include "compiler_driver.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using graphjit::driver::CompilerDriver;
using graphjit::driver::defaultBenchmarkSize;
using graphjit::driver::defaultRunSize;
using graphjit::driver::DriverOptions;
using graphjit::driver::parseCommandLine;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename Exception, typename Callable>
void expectException(Callable&& callable, const std::string_view detail) {
    try {
        std::forward<Callable>(callable)();
        fail("expected exception containing: " + std::string(detail));
    } catch (const Exception& error) {
        expect(std::string_view(error.what()).find(detail) !=
                   std::string_view::npos,
               "exception lacks expected detail: " + std::string(error.what()));
    }
}

[[nodiscard]] DriverOptions
parse(const std::initializer_list<std::string_view> arguments) {
    const std::vector<std::string_view> owned(arguments);
    return parseCommandLine(owned);
}

[[nodiscard]] std::string execute(DriverOptions options) {
    std::ostringstream output;
    CompilerDriver driver(std::move(options));
    driver.execute(output);
    return output.str();
}

void testCommandLineDefaultsAndCombinations() {
    const DriverOptions run = parse({"--run"});
    expect(run.run && run.source == "A + B" && run.size == defaultRunSize,
           "normal run defaults are incorrect");

    const DriverOptions benchmark = parse({"--benchmark"});
    expect(benchmark.benchmark && benchmark.size == defaultBenchmarkSize,
           "benchmark default size is incorrect");

    const DriverOptions combined =
        parse({"--emit-ast", "--emit-llvm", "--run", "--size=7", "X + Y"});
    expect(combined.emitAst && combined.emitLlvm && combined.run,
           "composable actions were not retained");
    expect(combined.size == 7 && combined.source == "X + Y",
           "explicit size or source was not retained");

    const DriverOptions help = parse({"--help"});
    expect(help.help, "help option was not retained");
}

void testCommandLineErrors() {
    expectException<std::invalid_argument>([] { static_cast<void>(parse({})); },
                                           "no action requested");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--unknown"})); }, "unknown option");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--run", "--run"})); },
        "duplicate option");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--run", "--size", "0"})); },
        "positive integer");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--run", "--size=abc"})); },
        "positive integer");
    expectException<std::invalid_argument>(
        [] {
            static_cast<void>(parse({"--run", "--size=18446744073709551615"}));
        },
        "addressable double storage");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--run", "--size", "3", "--size=4"})); },
        "duplicate option");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--run", "A + B", "C + D"})); },
        "only one source expression");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--benchmark", "--run"})); },
        "cannot be combined");
    expectException<std::invalid_argument>(
        [] { static_cast<void>(parse({"--benchmark", "A + B"})); },
        "does not accept source input");
}

void testAstAndIrEmission() {
    const std::string output =
        execute(parse({"--emit-ast", "--emit-llvm", "--size", "3", "A + B"}));
    expect(output.find("[Fused Kernel]") != std::string::npos,
           "optimized AST did not contain a fusion boundary");
    expect(output.find("BinaryOpNode: +") != std::string::npos,
           "optimized AST did not contain tensor addition");
    expect(output.find("define void @__tensor_expr") != std::string::npos,
           "tensor LLVM entry point was not emitted");

    const std::string scalar =
        execute(parse({"--emit-llvm", "2.0 + 3.0 * 4.0"}));
    expect(scalar.find("define double @__anon_expr") != std::string::npos,
           "scalar LLVM entry point was not emitted");
}

void testJitExecution() {
    const std::string output = execute(parse({"--run", "A + B"}));
    expect(output.find("Input A: [ 1.0, 2.0, 3.0 ]") != std::string::npos,
           "first deterministic input was not printed");
    expect(output.find("Input B: [ 10.0, 20.0, 30.0 ]") != std::string::npos,
           "second deterministic input was not printed");
    expect(output.find("Output:  [ 11.0, 22.0, 33.0 ]") != std::string::npos,
           "JIT tensor output was incorrect");

    const std::string scalar = execute(parse({"--run", "2.0 + 3.0 * 4.0"}));
    expect(scalar.find("[RESULT] 14") != std::string::npos,
           "JIT scalar result was incorrect");

    expectException<std::runtime_error>(
        [] { static_cast<void>(execute(parse({"--run", "A + A"}))); },
        "exactly two distinct input variables");
}

void testSmallBenchmark() {
    const std::string output =
        execute(parse({"--benchmark", "--size", "1024"}));
    expect(output.find("Best of 5") != std::string::npos,
           "benchmark did not report its timing policy");
    expect(output.find("Native C++:") != std::string::npos &&
               output.find("GraphJIT:") != std::string::npos &&
               output.find("GB/s") != std::string::npos,
           "benchmark timing or bandwidth output is missing");
    expect(output.find("[SUCCESS] Output verified") != std::string::npos,
           "benchmark did not verify its results");
}

} // namespace

int main() {
    try {
        testCommandLineDefaultsAndCombinations();
        testCommandLineErrors();
        testAstAndIrEmission();
        testJitExecution();
        testSmallBenchmark();
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "All compiler driver tests passed.\n";
    return EXIT_SUCCESS;
}
