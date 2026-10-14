#pragma once

#include <cstddef>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace graphjit::driver {

inline constexpr std::size_t defaultRunSize = 3;
inline constexpr std::size_t defaultBenchmarkSize = 10'000'000;

struct DriverOptions {
    bool emitAst{false};
    bool emitLlvm{false};
    bool run{false};
    bool benchmark{false};
    bool help{false};
    bool sizeSpecified{false};
    std::size_t size{defaultRunSize};
    std::string source{"A + B"};
};

/// Parses command-line arguments excluding argv[0].
[[nodiscard]] DriverOptions
parseCommandLine(std::span<const std::string_view> arguments);

void printUsage(std::ostream& output, std::string_view programName);

/// Coordinates the GraphJIT frontend, optimization, analysis, backend, and JIT.
class CompilerDriver final {
public:
    explicit CompilerDriver(DriverOptions options);

    /// Runs the requested actions and writes user-facing output to output.
    void execute(std::ostream& output);

private:
    void executePipeline(std::ostream& output);
    void executeBenchmark(std::ostream& output);

    DriverOptions options_;
};

} // namespace graphjit::driver
