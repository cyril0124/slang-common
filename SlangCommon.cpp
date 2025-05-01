#include "SlangCommon.h"
#include "fmt/color.h"
#include "fmt/format.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/numeric/Time.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/util/LanguageVersion.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <boost/type_index.hpp>
#include <type_traits>

using namespace slang;
using namespace slang::parsing;
using namespace slang::syntax;
using namespace slang::ast;

namespace slang_common {
bool checkDiagsError(Diagnostics &diags) {
    for (auto &diag : diags) {
        if (diag.isError()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree) {
    auto newTree = SyntaxTree::fromFileInMemory(SyntaxPrinter::printFile(oldTree), SyntaxTree::getDefaultSourceManager());
    if (newTree->diagnostics().empty() == false) {
        auto diags = newTree->diagnostics();
        if (checkDiagsError(diags)) {
            auto ret = DiagnosticEngine::reportAll(SyntaxTree::getDefaultSourceManager(), diags);
            fmt::println("[rebuildSyntaxTree] SyntaxError: {}", ret);
            fflush(stdout);

            if (printTree) {
                fmt::println("[rebuildSyntaxTree] SyntaxError tree => {}", SyntaxPrinter::printFile(oldTree));
                fflush(stdout);
            }
            
            // Syntax error
            assert(false && "[rebuildSyntaxTree] Syntax error");
        }
    } else {
        Compilation compilation;
        compilation.addSyntaxTree(newTree);
        auto diags = compilation.getAllDiagnostics();
        if (diags.empty() == false) {
            if (checkDiagsError(diags)) {
                auto ret = DiagnosticEngine::reportAll(SyntaxTree::getDefaultSourceManager(), diags);
                fmt::println("[rebuildSyntaxTree] CompilationError: {}", ret);
                fflush(stdout);

                if (printTree) {
                    fmt::println("[rebuildSyntaxTree] CompilationError tree => {}", SyntaxPrinter::printFile(oldTree));
                    fflush(stdout);
                }

                // Compilation error
                assert(false && "[rebuildSyntaxTree] Compilation error");
            }
        }
    }

    return newTree;
}

class SynaxLister : public SyntaxVisitor<SynaxLister> {
  public:
    const uint64_t maxDepth;
    uint64_t depth = 0;
    uint64_t count = 0;
    std::vector<bool> lastChildStack;

    SynaxLister(uint64_t maxDepth = 10000) : maxDepth(maxDepth) {}

#define SYNTAX_NAME()                                                                                                                                                                                                                                                                                                                                                                                          \
    extra += "\tsynName: ";                                                                                                                                                                                                                                                                                                                                                                                    \
    auto name = boost::typeindex::type_id<decltype(syn)>().pretty_name();                                                                                                                                                                                                                                                                                                                                      \
    extra += std::string(name);                                                                                                                                                                                                                                                                                                                                                                                \
    extra += " ";

#define PREFIX_CODE()                                                                                                                                                                                                                                                                                                                                                                                          \
    if (depth > maxDepth)                                                                                                                                                                                                                                                                                                                                                                                      \
        return;                                                                                                                                                                                                                                                                                                                                                                                                \
    std::string prefix = createPrefix();                                                                                                                                                                                                                                                                                                                                                                       \
    std::string extra  = "";                                                                                                                                                                                                                                                                                                                                                                                   \
    SYNTAX_NAME();

#define PRINT_INFO_AND_VISIT()                                                                                                                                                                                                                                                                                                                                                                                 \
    do {                                                                                                                                                                                                                                                                                                                                                                                                       \
        fmt::println("{}[{}] depth: {}\tsynKind: {}\t{}", prefix, count, depth, toString(syn.kind), extra);                                                                                                                                                                                                                                                                                                    \
        count++;                                                                                                                                                                                                                                                                                                                                                                                               \
        lastChildStack.push_back(false);                                                                                                                                                                                                                                                                                                                                                                       \
        depth++;                                                                                                                                                                                                                                                                                                                                                                                               \
        visitDefault(syn);                                                                                                                                                                                                                                                                                                                                                                                     \
        lastChildStack.pop_back();                                                                                                                                                                                                                                                                                                                                                                             \
        depth--;                                                                                                                                                                                                                                                                                                                                                                                               \
    } while (0)

    void handle(const ModuleDeclarationSyntax &syn) {
        PREFIX_CODE();

        extra += "moduleName: ";
        extra += syn.header->name.rawText();

        PRINT_INFO_AND_VISIT();
    }

    void handle(const DeclaratorSyntax &syn) {
        PREFIX_CODE();

        extra += "declName: ";
        extra += syn.name.toString();

        PRINT_INFO_AND_VISIT();
    }

    void handle(const ContinuousAssignSyntax &syn) {
        PREFIX_CODE();

        auto &assign     = syn.assignments[0]->as<BinaryExpressionSyntax>();
        auto &identifier = assign.left->as<IdentifierNameSyntax>();
        extra += " left: ";
        extra += identifier.identifier.toString();
        extra += " right: ";
        extra += assign.right->toString();
        extra += " ";
        extra += syn.toString();

        PRINT_INFO_AND_VISIT();
    }

    void handle(const IdentifierNameSyntax &syn) {
        PREFIX_CODE();

        extra += " name: ";
        extra += syn.identifier.rawText();
        extra += " ";

        PRINT_INFO_AND_VISIT();
    }

    void handle(const IdentifierSelectNameSyntax &syn) {
        PREFIX_CODE();

        extra += " name: ";
        extra += syn.identifier.rawText();
        extra += " ";

        PRINT_INFO_AND_VISIT();
    }

    /*
        BinaryExpressionSyntax:
            vec <= a + b
            val = a & b
            ...
    */
    void handle(const BinaryExpressionSyntax &syn) {
        PREFIX_CODE();

        if (syn.kind == SyntaxKind::NonblockingAssignmentExpression && findAlwaysBlock(&syn)) {
            extra += " binExprNonblocking: ";
            extra += syn.toString();
            if (syn.getChildCount() > 0) {
                if (syn.left->kind == SyntaxKind::IdentifierName) {
                    extra += " left: ";
                    auto &id = syn.left->as<IdentifierNameSyntax>();
                    extra += id.identifier.rawText();
                }
            }
        }

        PRINT_INFO_AND_VISIT();
    }

    /*
        BlockStatementSyntax:
            begin
                <some code...>
            end

            begin
                if(reset) begin
                    <some code...>
                end else begin
                    <some code...>
                end
            end
    */
    void handle(const BlockStatementSyntax &syn) {
        PREFIX_CODE();

        PRINT_INFO_AND_VISIT();
    }

    // const SyntaxNode *findAlwaysBlock(const SyntaxNode *syn) {
    bool findAlwaysBlock(const SyntaxNode *syn) {
        if (syn->kind == SyntaxKind::AlwaysBlock) {
            // fmt::println("found always block!");
            // return syn;
            return true;
        }

        if (syn->parent == nullptr) {
            // assert(false && "Not found always block!");
            return false;
        }

        return findAlwaysBlock(syn->parent);
    }

    void handle(const auto &syn) {
        PREFIX_CODE();

        PRINT_INFO_AND_VISIT();
    }

    // template<typename T>
    // void handle(const T& syn) {
    //     PREFIX_CODE();

    //     PRINT_INFO_AND_VISIT();
    // }

    std::string createPrefix() {
        std::string result;
        for (size_t j = 0; j < lastChildStack.size(); ++j) {
            if (j == lastChildStack.size() - 1) {
                result += lastChildStack[j] ? "└─ " : "├─ ";
            } else {
                result += lastChildStack[j] ? "    " : "│   ";
            }
        }
        return result;
    }
#undef SYNTAX_NAME()
#undef PREFIX_CODE()
#undef PRINT_INFO_AND_VISIT()
};

// Helper template to detect the presence of a method
template <typename, typename = std::void_t<>> struct has_getSyntax : std::false_type {};

// Specialization that detects the presence of a method
template <typename T> struct has_getSyntax<T, std::void_t<decltype(std::declval<T>().getSyntax())>> : std::true_type {};

class ASTLister : public ASTVisitor<ASTLister, true, true> {
  public:
    const uint64_t maxDepth;
    uint64_t depth = 0;
    uint64_t count = 0;
    std::vector<bool> lastChildStack;

    ASTLister(uint64_t maxDepth = 10000) : maxDepth(maxDepth) {}

    // clang-format off
    #define AST_NAME() \
        extra += "\tastName: "; \
        auto name = boost::typeindex::type_id<decltype(ast)>().pretty_name(); \
        extra += std::string(name); \
        extra += " "; \
        extra += "\tsynKindName: ";  \
        if constexpr (has_getSyntax<decltype(ast)>::value) { \
            const auto syn = ast.getSyntax(); \
            if (syn != nullptr) { \
                auto synKind = syn->kind; \
                extra += toString(synKind); \
            } else { \
                extra += "Null"; \
            } \
        } else { \ 
            extra += "None"; \
        }

    #define PREFIX_CODE() \
        if (depth > maxDepth) \
            return; \
        std::string prefix = createPrefix(); \
        std::string extra = ""; \
        AST_NAME();

    #define PRINT_INFO_AND_VISIT() \
        do { \
            fmt::println("{}[{}] depth: {}\tastKind: {}\t{}", prefix, count, depth, toString(ast.kind), extra); \
            count++; \
            lastChildStack.push_back(false); \
            depth++; \
            visitDefault(ast); \
            lastChildStack.pop_back(); \
            depth--; \
        } while(0)
    // clang-format on

    void handle(const auto &ast) {
        PREFIX_CODE();

        PRINT_INFO_AND_VISIT();
    }

    void handle(const InstanceSymbol &ast) {
        PREFIX_CODE();
        auto defName  = ast.getDefinition().name;
        auto instName = ast.name;

        extra += " defName: ";
        extra += defName;
        extra += " instName: ";
        extra += instName;

        PRINT_INFO_AND_VISIT();
    }

    void handle(const NetSymbol &ast) {
        PREFIX_CODE();
        auto netName  = ast.name;
        auto dataType = ast.netType.getDataType().toString();
        auto bitWidth = ast.getDeclaredType()->getType().getBitWidth();

        extra += " netName: ";
        extra += netName;

        extra += " dataType: ";
        extra += dataType;

        extra += " bitWidth: ";
        extra += fmt::format("{}", bitWidth);

        PRINT_INFO_AND_VISIT();
    }

    void handle(const PortSymbol &ast) {
        PREFIX_CODE();

        auto &port         = ast.as<PortSymbol>();
        auto &pType        = port.getType();
        auto &dir          = port.direction;
        auto &internalKind = port.internalSymbol->kind;
        auto &pTypeKind    = pType.kind;

        extra += fmt::format(" portName: {} dir: {} internalKind: {} portWidth: {} portType: {} portTypeKind: {}", port.name, toString(port.direction), toString(internalKind), pType.getBitWidth(), pType.toString(), toString(pType.kind));

        if (internalKind == SymbolKind::Net) {
            auto &net  = port.internalSymbol->as<NetSymbol>();
            auto dType = net.netType.getDataType().toString();
            extra += fmt::format(" dataType: {}", dType);
        } else if (internalKind == SymbolKind::Variable) {
            auto &var = port.internalSymbol->as<VariableSymbol>();
        } else {
            // TODO: handle other kinds
        }

        PRINT_INFO_AND_VISIT();
    }

    void handle(const VariableSymbol &ast) {
        PREFIX_CODE();
        auto varName = ast.name;

        extra += " varName: ";
        extra += varName;

        PRINT_INFO_AND_VISIT();
    }

    std::string createPrefix() {
        std::string result;
        for (size_t j = 0; j < lastChildStack.size(); ++j) {
            if (j == lastChildStack.size() - 1) {
                result += lastChildStack[j] ? "└─ " : "├─ ";
            } else {
                result += lastChildStack[j] ? "    " : "│   ";
            }
        }
        return result;
    }
#undef AST_NAME()
#undef PREFIX_CODE()
#undef PRINT_INFO_AND_VISIT()
};

void listAST(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth = 1000) {
    Compilation compilation;
    compilation.addSyntaxTree(tree);

    ASTLister visitor(maxDepth);
    compilation.getRoot().visit(visitor);
}

void listSyntaxTree(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth = 1000) {
    SynaxLister sl(maxDepth);
    tree->root().visit(sl);
}

void listSyntaxNode(const SyntaxNode &node, uint64_t maxDepth = 1000) {
    SynaxLister sl(maxDepth);
    node.visit(sl);
}

void listASTNode(std::shared_ptr<SyntaxTree> tree, const ModuleDeclarationSyntax &syntax, uint64_t maxDepth = 1000) {
    Compilation compilation;
    compilation.addSyntaxTree(tree);

    ASTLister visitor(maxDepth);
    const auto def = compilation.getDefinition(compilation.getRoot(), syntax);
    auto inst      = &InstanceSymbol::createDefault(compilation, def->as<DefinitionSymbol>());
    inst->body.visit(visitor);
}

const DefinitionSymbol *getDefSymbol(std::shared_ptr<SyntaxTree> tree, const ModuleDeclarationSyntax &syntax) {
    Compilation compilation;
    compilation.addSyntaxTree(tree);

    return compilation.getDefinition(compilation.getRoot(), syntax);
}

const InstanceSymbol *getInstSymbol(Compilation &compilation, const ModuleDeclarationSyntax &syntax) {
    auto def = compilation.getDefinition(compilation.getRoot(), syntax);
    return &InstanceSymbol::createDefault(compilation, def->as<DefinitionSymbol>());
}

const SyntaxNode *getNetDeclarationSyntax(const SyntaxNode *node, std::string_view identifierName, bool reverse) {
    if (node == nullptr) {
        return nullptr;
    }

    if (node->kind == SyntaxKind::NetDeclaration) {
        auto &netDeclSyn = node->as<NetDeclarationSyntax>();
        auto t           = netDeclSyn.declarators[0]->as<DeclaratorSyntax>().name.rawText();
        if (t == identifierName) {
            return node;
        }
    }

    if (reverse) {
        // from node to parent node
        return getNetDeclarationSyntax(node->parent, identifierName, reverse);
    } else {
        // from node to child nodes
        for (uint32_t i = 0; i < node->getChildCount(); i++) {
            auto childNode = node->childNode(i);
            auto newNode   = getNetDeclarationSyntax(childNode, identifierName);
            if (newNode != nullptr) {
                return newNode;
            }
        }
    }

    return nullptr;
}

} // namespace slang_common