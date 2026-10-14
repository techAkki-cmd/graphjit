#include "graphjit/driver/compiler_driver.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(const int argc, const char* const argv[]) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        const graphjit::driver::DriverOptions options =
            graphjit::driver::parseCommandLine(arguments);
        if (options.help) {
            graphjit::driver::printUsage(
                std::cout, argc > 0 ? std::string_view(argv[0]) : "graphjit");
            return EXIT_SUCCESS;
        }

        graphjit::driver::CompilerDriver driver(options);
        driver.execute(std::cout);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "graphjit: error: " << error.what() << '\n';
        std::cerr << "Try 'graphjit --help' for usage.\n";
        return EXIT_FAILURE;
    }
}
