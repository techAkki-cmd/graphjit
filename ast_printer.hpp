#pragma once

#include "ast.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

namespace graphjit::ast {

/// Prints an AST as a labeled, indented tree.
class ASTPrinter final : public ASTVisitor {
public:
    ASTPrinter() noexcept;
    explicit ASTPrinter(std::ostream& output) noexcept;

    void visit(FloatLiteralNode& node) override;
    void visit(VariableNode& node) override;
    void visit(BinaryOpNode& node) override;
    void visit(FunctionDeclNode& node) override;

private:
    void printIndent();
    void printChild(std::string_view label, ASTNode& child);
    [[nodiscard]] static std::string formatFloat(double value);

    std::ostream& output_;
    std::size_t indentationLevel_{0};
};

} // namespace graphjit::ast
