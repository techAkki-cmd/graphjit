#include "ast_printer.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <system_error>

namespace graphjit::ast {
namespace {

class IndentationGuard final {
public:
    explicit IndentationGuard(std::size_t& level) noexcept : level_(level) {
        ++level_;
    }

    ~IndentationGuard() {
        --level_;
    }

    IndentationGuard(const IndentationGuard&) = delete;
    IndentationGuard& operator=(const IndentationGuard&) = delete;

private:
    std::size_t& level_;
};

} // namespace

ASTPrinter::ASTPrinter() noexcept : output_(std::cout) {}

ASTPrinter::ASTPrinter(std::ostream& output) noexcept : output_(output) {}

void ASTPrinter::printIndent() {
    constexpr std::string_view indentation = "  ";
    for (std::size_t level = 0; level < indentationLevel_; ++level) {
        output_ << indentation;
    }
}

void ASTPrinter::printChild(const std::string_view label, ASTNode& child) {
    printIndent();
    output_ << label << ": ";
    child.accept(*this);
}

std::string ASTPrinter::formatFloat(const double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::general);

    std::string formatted;
    if (result.ec == std::errc{}) {
        formatted.assign(buffer.data(), result.ptr);
    } else {
        // This should only be needed on standard libraries with incomplete
        // floating-point to_chars support.
        std::ostringstream fallback;
        fallback.precision(std::numeric_limits<double>::max_digits10);
        fallback << value;
        formatted = fallback.str();
    }

    if (std::isfinite(value) && formatted.find_first_of(".eE") == std::string::npos) {
        formatted += ".0";
    }
    return formatted;
}

void ASTPrinter::visit(FloatLiteralNode& node) {
    output_ << "FloatLiteralNode: " << formatFloat(node.value()) << '\n';
}

void ASTPrinter::visit(VariableNode& node) {
    output_ << "VariableNode: " << node.name() << '\n';
}

void ASTPrinter::visit(BinaryOpNode& node) {
    output_ << "BinaryOpNode: " << binaryOperatorSpelling(node.operatorType()) << '\n';
    const IndentationGuard indentation(indentationLevel_);
    printChild("Left", node.lhs());
    printChild("Right", node.rhs());
}

void ASTPrinter::visit(FunctionDeclNode& node) {
    output_ << "FunctionDeclNode: " << node.name() << '(';
    const auto& arguments = node.arguments();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            output_ << ", ";
        }
        output_ << arguments[index];
    }
    output_ << ")\n";

    const IndentationGuard indentation(indentationLevel_);
    printChild("Body", node.body());
}

} // namespace graphjit::ast
