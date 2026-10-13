#include "compiler_driver.hpp"

#include "ast.hpp"
#include "ast_printer.hpp"
#include "constant_folding.hpp"
#include "ir_generator.hpp"
#include "jit_engine.hpp"
#include "kernel_fusion.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "shape_inference.hpp"

#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graphjit::driver {
namespace {

using Clock = std::chrono::steady_clock;
using ScalarFunction = double (*)();
using TensorFunction = void (*)(double*, double*, double*);

class VariableCollector final : public ast::ASTVisitor {
public:
    [[nodiscard]] const std::vector<std::string>& names() const noexcept {
        return names_;
    }

    void visit(ast::FloatLiteralNode&) override {}

    void visit(ast::VariableNode& node) override {
        std::string name(node.name());
        if (seen_.insert(name).second) {
            names_.push_back(std::move(name));
        }
    }

    void visit(ast::BinaryOpNode& node) override {
        node.lhs().accept(*this);
        node.rhs().accept(*this);
    }

    void visit(ast::FunctionDeclNode& node) override {
        node.body().accept(*this);
    }

    void visit(ast::FusedKernelNode& node) override {
        node.fusedExpression().accept(*this);
    }

private:
    std::vector<std::string> names_;
    std::unordered_set<std::string> seen_;
};

struct PreparedPipeline {
    std::unique_ptr<ast::ASTNode> root;
    std::vector<std::string> inputNames;
    analysis::TensorShape rootShape;
    analysis::ShapeAnnotations annotations;
};

[[nodiscard]] PreparedPipeline preparePipeline(const std::string& source,
                                               const std::size_t tensorSize) {
    frontend::Parser parser{frontend::Lexer(source)};
    auto root = parser.parse();

    opt::ConstantFoldingPass constantFolding;
    root = constantFolding.rewrite(std::move(root));

    opt::KernelFusionPass kernelFusion;
    root = kernelFusion.rewrite(std::move(root));

    VariableCollector collector;
    root->accept(collector);
    std::vector<std::string> inputNames = collector.names();

    analysis::SymbolTable symbols;
    for (const std::string& name : inputNames) {
        symbols.emplace(name, analysis::TensorShape{{tensorSize}});
    }

    analysis::ShapeInferenceVisitor inference(std::move(symbols));
    analysis::TensorShape rootShape = inference.infer(*root);
    analysis::ShapeAnnotations annotations = inference.annotations();

    return PreparedPipeline{std::move(root), std::move(inputNames),
                            std::move(rootShape), std::move(annotations)};
}

[[nodiscard]] std::size_t parseSize(const std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument("--size requires a positive integer");
    }

    std::size_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error == std::errc::result_out_of_range) {
        throw std::invalid_argument("--size is outside the supported range: '" +
                                    std::string(text) + "'");
    }
    if (error != std::errc{} || position != end || value == 0) {
        throw std::invalid_argument(
            "--size requires a positive integer, received '" +
            std::string(text) + "'");
    }
    if (value > std::numeric_limits<std::size_t>::max() / sizeof(double)) {
        throw std::invalid_argument(
            "--size exceeds addressable double storage: '" + std::string(text) +
            "'");
    }
    return value;
}

void setFlag(bool& flag, const std::string_view spelling) {
    if (flag) {
        throw std::invalid_argument("duplicate option '" +
                                    std::string(spelling) + "'");
    }
    flag = true;
}

[[nodiscard]] std::string moduleText(const llvm::Module& module) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream, nullptr);
    stream.flush();
    return text;
}

void fillRunInputs(std::vector<double>& first, std::vector<double>& second) {
    for (std::size_t index = 0; index < first.size(); ++index) {
        const double ordinal = static_cast<double>(index + 1);
        first[index] = ordinal;
        second[index] = ordinal * 10.0;
    }
}

[[nodiscard]] double checksum(const std::vector<double>& values) {
    double result = 0.0;
    for (const double value : values) {
        result += value;
    }
    return result;
}

void printVector(std::ostream& output, const std::string_view label,
                 const std::vector<double>& values) {
    constexpr std::size_t completeLimit = 16;
    constexpr std::size_t sampleSize = 4;

    output << label << "[ ";
    const std::size_t displayed = values.size() <= completeLimit
                                      ? values.size()
                                      : std::min(sampleSize, values.size());
    for (std::size_t index = 0; index < displayed; ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << std::fixed << std::setprecision(1) << values[index];
    }
    if (displayed != values.size()) {
        output << ", ...";
    }
    output << " ]";
    if (displayed != values.size()) {
        output << " (" << values.size() << " elements, checksum "
               << std::setprecision(6) << checksum(values) << ')';
    }
    output << '\n';
}

template <typename Callable>
[[nodiscard]] double timeMilliseconds(Callable&& callable) {
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto start = Clock::now();
    std::forward<Callable>(callable)();
    const auto finish = Clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    return std::chrono::duration<double, std::milli>(finish - start).count();
}

void nativeAddition(const std::vector<double>& lhs,
                    const std::vector<double>& rhs,
                    std::vector<double>& output) {
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = lhs[index] + rhs[index];
    }
}

void verifyOutputs(const std::vector<double>& expected,
                   const std::vector<double>& actual) {
    constexpr double absoluteTolerance = 1.0e-12;
    constexpr double relativeTolerance = 1.0e-12;

    if (expected.size() != actual.size()) {
        throw std::runtime_error("benchmark output sizes do not match");
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const double difference = std::abs(expected[index] - actual[index]);
        const double scale = std::max(1.0, std::abs(expected[index]));
        if (difference > absoluteTolerance + relativeTolerance * scale) {
            throw std::runtime_error(
                "benchmark verification failed at index " +
                std::to_string(index) +
                ": native=" + std::to_string(expected[index]) +
                ", GraphJIT=" + std::to_string(actual[index]));
        }
    }
}

[[nodiscard]] double bandwidthGbPerSecond(const std::size_t elementCount,
                                          const double milliseconds) {
    const long double bytes = 3.0L * static_cast<long double>(elementCount) *
                              static_cast<long double>(sizeof(double));
    const long double seconds =
        static_cast<long double>(milliseconds) / 1000.0L;
    return static_cast<double>(bytes / seconds / 1'000'000'000.0L);
}

} // namespace

DriverOptions
parseCommandLine(const std::span<const std::string_view> arguments) {
    DriverOptions options;
    bool sourceSpecified = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--emit-ast") {
            setFlag(options.emitAst, argument);
        } else if (argument == "--emit-llvm") {
            setFlag(options.emitLlvm, argument);
        } else if (argument == "--run") {
            setFlag(options.run, argument);
        } else if (argument == "--benchmark") {
            setFlag(options.benchmark, argument);
        } else if (argument == "--help" || argument == "-h") {
            setFlag(options.help, "--help");
        } else if (argument == "--size") {
            if (options.sizeSpecified) {
                throw std::invalid_argument("duplicate option '--size'");
            }
            if (++index == arguments.size()) {
                throw std::invalid_argument(
                    "--size requires a positive integer");
            }
            options.size = parseSize(arguments[index]);
            options.sizeSpecified = true;
        } else if (argument.starts_with("--size=")) {
            if (options.sizeSpecified) {
                throw std::invalid_argument("duplicate option '--size'");
            }
            options.size = parseSize(argument.substr(7));
            options.sizeSpecified = true;
        } else if (argument.starts_with('-')) {
            throw std::invalid_argument("unknown option '" +
                                        std::string(argument) + "'");
        } else {
            if (sourceSpecified) {
                throw std::invalid_argument(
                    "only one source expression may be provided");
            }
            if (argument.empty()) {
                throw std::invalid_argument(
                    "source expression must not be empty");
            }
            options.source = std::string(argument);
            sourceSpecified = true;
        }
    }

    if (options.help) {
        return options;
    }
    if (!options.emitAst && !options.emitLlvm && !options.run &&
        !options.benchmark) {
        throw std::invalid_argument("no action requested; use --emit-ast, "
                                    "--emit-llvm, --run, or --benchmark");
    }
    if (options.benchmark && options.run) {
        throw std::invalid_argument(
            "--benchmark cannot be combined with --run");
    }
    if (options.benchmark && sourceSpecified) {
        throw std::invalid_argument(
            "--benchmark uses the fixed expression 'A + B' "
            "and does not accept source input");
    }
    if (options.benchmark && !options.sizeSpecified) {
        options.size = defaultBenchmarkSize;
    }

    return options;
}

void printUsage(std::ostream& output, const std::string_view programName) {
    output
        << "Usage: " << programName
        << " [options] [expression]\n\n"
           "Options:\n"
           "  --emit-ast       Print the optimized abstract syntax tree\n"
           "  --emit-llvm      Print generated LLVM IR\n"
           "  --run            JIT-compile and execute the expression\n"
           "  --benchmark      Benchmark the fixed expression A + B\n"
           "  --size N         Set the tensor element count\n"
           "  --help, -h       Show this help text\n\n"
           "If expression is omitted, A + B is used. Benchmark size defaults "
           "to 10,000,000; other modes default to 3.\n";
}

CompilerDriver::CompilerDriver(DriverOptions options)
    : options_(std::move(options)) {}

void CompilerDriver::execute(std::ostream& output) {
    if (options_.benchmark) {
        executeBenchmark(output);
        return;
    }
    executePipeline(output);
}

void CompilerDriver::executePipeline(std::ostream& output) {
    PreparedPipeline pipeline = preparePipeline(options_.source, options_.size);

    if (options_.emitAst) {
        output << "[AST]\n";
        ast::ASTPrinter printer(output);
        pipeline.root->accept(printer);
        output << '\n';
    }

    if (!options_.emitLlvm && !options_.run) {
        return;
    }

    backend::IRGenerator generator(std::move(pipeline.annotations));
    const bool scalar = pipeline.rootShape.isScalar();
    if (options_.run && !scalar && pipeline.inputNames.size() != 2) {
        throw std::runtime_error(
            "tensor execution currently requires exactly two distinct input "
            "variables; found " +
            std::to_string(pipeline.inputNames.size()));
    }
    if (scalar) {
        static_cast<void>(generator.compile(pipeline.root.get()));
    } else {
        static_cast<void>(
            generator.compileTensor(pipeline.root.get(), pipeline.inputNames));
    }

    if (options_.emitLlvm) {
        output << "[LLVM IR]\n" << moduleText(generator.module()) << '\n';
    }
    if (!options_.run) {
        return;
    }

    auto module = generator.takeModule();
    jit::JITEngine engine;
    engine.addModule(std::move(module.module), std::move(module.context));

    if (scalar) {
        const ScalarFunction function =
            engine.lookup("__anon_expr").toPtr<ScalarFunction>();
        output << "[RESULT] " << std::setprecision(17) << function() << '\n';
        return;
    }

    const TensorFunction function =
        engine.lookup("__tensor_expr").toPtr<TensorFunction>();
    std::vector<double> first(options_.size);
    std::vector<double> second(options_.size);
    std::vector<double> result(options_.size, 0.0);
    fillRunInputs(first, second);
    function(first.data(), second.data(), result.data());

    printVector(output, "Input " + pipeline.inputNames[0] + ": ", first);
    printVector(output, "Input " + pipeline.inputNames[1] + ": ", second);
    printVector(output, "Output:  ", result);
}

void CompilerDriver::executeBenchmark(std::ostream& output) {
    constexpr std::size_t measuredIterations = 5;
    PreparedPipeline pipeline = preparePipeline("A + B", options_.size);

    if (options_.emitAst) {
        output << "[AST]\n";
        ast::ASTPrinter printer(output);
        pipeline.root->accept(printer);
        output << '\n';
    }

    backend::IRGenerator generator(std::move(pipeline.annotations));
    static_cast<void>(
        generator.compileTensor(pipeline.root.get(), pipeline.inputNames));
    if (options_.emitLlvm) {
        output << "[LLVM IR]\n" << moduleText(generator.module()) << '\n';
    }

    auto module = generator.takeModule();
    jit::JITEngine engine;
    engine.addModule(std::move(module.module), std::move(module.context));
    const TensorFunction function =
        engine.lookup("__tensor_expr").toPtr<TensorFunction>();

    const long double inputMegabytes = 2.0L *
                                       static_cast<long double>(options_.size) *
                                       sizeof(double) / 1'000'000.0L;
    const long double totalMegabytes = 4.0L *
                                       static_cast<long double>(options_.size) *
                                       sizeof(double) / 1'000'000.0L;
    output << "[INFO] Allocating " << options_.size << " elements ("
           << std::fixed << std::setprecision(1)
           << static_cast<double>(inputMegabytes) << " MB inputs, "
           << static_cast<double>(totalMegabytes) << " MB total buffers)...\n";
#ifndef NDEBUG
    output << "[WARNING] This is a debug build; benchmark results are not "
              "representative.\n";
#endif

    std::vector<double> first(options_.size);
    std::vector<double> second(options_.size);
    std::vector<double> nativeOutput(options_.size, 0.0);
    std::vector<double> jitOutput(options_.size, 0.0);

    std::mt19937_64 random(0x47524150484A4954ULL);
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    for (std::size_t index = 0; index < options_.size; ++index) {
        first[index] = distribution(random);
        second[index] = distribution(random);
    }

    // Warm both implementations before measuring. Compilation, allocation,
    // initialization, and these calls are deliberately outside timed regions.
    nativeAddition(first, second, nativeOutput);
    function(first.data(), second.data(), jitOutput.data());

    double nativeBest = std::numeric_limits<double>::infinity();
    double jitBest = std::numeric_limits<double>::infinity();
    for (std::size_t trial = 0; trial < measuredIterations; ++trial) {
        const auto timeNative = [&] {
            nativeBest =
                std::min(nativeBest, timeMilliseconds([&] {
                             nativeAddition(first, second, nativeOutput);
                         }));
        };
        const auto timeJit = [&] {
            jitBest = std::min(jitBest, timeMilliseconds([&] {
                                   function(first.data(), second.data(),
                                            jitOutput.data());
                               }));
        };
        if (trial % 2 == 0) {
            timeNative();
            timeJit();
        } else {
            timeJit();
            timeNative();
        }
    }

    verifyOutputs(nativeOutput, jitOutput);
    const double resultChecksum = checksum(jitOutput);
    const double nativeBandwidth =
        bandwidthGbPerSecond(options_.size, nativeBest);
    const double jitBandwidth = bandwidthGbPerSecond(options_.size, jitBest);
    const double relativePerformance = nativeBest / jitBest * 100.0;

    output << "\n[BENCHMARK] Best of " << measuredIterations
           << " measured runs after one warmup\n"
           << "Native C++: " << std::fixed << std::setprecision(3) << nativeBest
           << " ms (" << std::setprecision(2) << nativeBandwidth << " GB/s)\n"
           << "GraphJIT:   " << std::setprecision(3) << jitBest << " ms ("
           << std::setprecision(2) << jitBandwidth << " GB/s)\n"
           << "Checksum:   " << std::setprecision(12) << resultChecksum << '\n'
           << "\n[SUCCESS] Output verified. GraphJIT achieved "
           << std::setprecision(1) << relativePerformance
           << "% of native C++ performance.\n";
}

} // namespace graphjit::driver
