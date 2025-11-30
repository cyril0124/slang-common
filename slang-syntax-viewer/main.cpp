#include "SlangCommon.h"
#include "fmt/core.h"
#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <optional>

/**
 * @class ModuleSyntaxTreePrinter
 * @brief Visitor class to find and print syntax tree for a specific module
 *
 * This class traverses the syntax tree to locate a module by name and prints
 * its syntax tree structure up to a specified depth. It uses the visitor pattern
 * for efficient tree traversal.
 */
class ModuleSyntaxTreePrinter : public slang::syntax::SyntaxVisitor<ModuleSyntaxTreePrinter> {
  public:
    /**
     * @brief Construct a new Module Syntax Tree Printer
     * @param targetModuleName Name of the module to find and print
     * @param maxDepth Maximum depth to traverse when printing the tree
     */
    explicit ModuleSyntaxTreePrinter(std::string_view targetModuleName, uint32_t maxDepth) : targetModuleName_(targetModuleName), maxDepth_(maxDepth), found_(false) {}

    /**
     * @brief Handler for module declaration syntax nodes
     * @param syntax The module declaration syntax node to process
     *
     * When the target module is found, prints its syntax tree and stops traversal.
     * Otherwise, continues visiting child nodes.
     */
    void handle(const ModuleDeclarationSyntax &syntax) {
        // Check if this is the module we're looking for
        if (syntax.header->name.rawText() == targetModuleName_) {
            fmt::println("[ModuleSyntaxTreePrinter] Found module: {}", targetModuleName_);
            slang_common::listSyntaxNode(syntax, maxDepth_);
            found_ = true;
            return; // Stop traversal once found
        }

        // Continue searching in child nodes
        visitDefault(syntax);
    }

    /// @brief Check if the target module was found during traversal
    [[nodiscard]] bool wasFound() const { return found_; }

  private:
    std::string_view targetModuleName_; ///< Name of module to locate
    uint32_t maxDepth_;                 ///< Maximum depth for tree printing
    bool found_;                        ///< Whether target module was found
};

/**
 * @class ModuleASTPrinter
 * @brief Visitor class to find and print AST for a specific module
 *
 * This class traverses the syntax tree to locate a module by name and prints
 * its Abstract Syntax Tree (AST) structure. The AST provides semantic information
 * beyond the pure syntactic structure.
 */
class ModuleASTPrinter : public slang::syntax::SyntaxVisitor<ModuleASTPrinter> {
  public:
    /**
     * @brief Construct a new Module AST Printer
     * @param targetModuleName Name of the module to find and print
     * @param maxDepth Maximum depth to traverse when printing the AST
     * @param syntaxTree Reference to the syntax tree containing the module
     */
    explicit ModuleASTPrinter(std::string_view targetModuleName, uint32_t maxDepth, std::shared_ptr<SyntaxTree> &syntaxTree)
        : targetModuleName_(targetModuleName), maxDepth_(maxDepth), syntaxTree_(syntaxTree), found_(false) {}

    /**
     * @brief Handler for module declaration syntax nodes
     * @param syntax The module declaration syntax node to process
     *
     * When the target module is found, prints its AST structure and stops traversal.
     * The AST includes semantic information like types, symbols, and bindings.
     */
    void handle(const ModuleDeclarationSyntax &syntax) {
        // Check if this is the module we're looking for
        if (syntax.header->name.rawText() == targetModuleName_) {
            fmt::println("[ModuleASTPrinter] Found module: {}", targetModuleName_);
            slang_common::listASTNode(syntaxTree_, syntax, maxDepth_);
            found_ = true;
            return; // Stop traversal once found
        }

        // Continue searching in child nodes
        visitDefault(syntax);
    }

    /// @brief Check if the target module was found during traversal
    [[nodiscard]] bool wasFound() const { return found_; }

  private:
    std::string_view targetModuleName_;       ///< Name of module to locate
    uint32_t maxDepth_;                       ///< Maximum depth for AST printing
    std::shared_ptr<SyntaxTree> &syntaxTree_; ///< Reference to syntax tree
    bool found_;                              ///< Whether target module was found
};

class SlangSyntaxViewer {
  private:
    slang::driver::Driver driver;
    std::optional<int> depth;
    std::optional<bool> listSyntaxTree;
    std::optional<bool> listAst;
    std::optional<bool> showHelp;
    std::string topModuleName;

  public:
    SlangSyntaxViewer() {
        this->driver.addStandardArgs();
        this->driver.cmdLine.add("-d,--depth", depth, "Depth", "<integer>");
        this->driver.cmdLine.add("--lsyn,--list-syntax-tree", listSyntaxTree, "List syntax tree");
        this->driver.cmdLine.add("--last,--list-ast", listAst, "List AST");
        this->driver.cmdLine.add("-h,--help", showHelp, "Show help");
    };

    void parseCommandLine(int argc, char **argv) {
        if (!this->driver.parseCommandLine(argc, argv)) {
            std::cout << "Failed to parse command line arguments" << std::endl;
            exit(1);
        };

        if (showHelp) {
            std::cout << this->driver.cmdLine.getHelpText("slang-syntax-viewer") << std::endl;
            exit(0);
        }

        assert(driver.processOptions() && "Failed to process options");
        driver.options.singleUnit = true;

        assert(driver.parseAllSources() && "Failed to parse all sources");
        assert(driver.reportParseDiags() && "Failed to report parse diagnostics");
        assert(driver.syntaxTrees.size() == 1 && "Only one SyntaxTree is expected");

        bool compileSuccess = driver.runFullCompilation(false);
        assert(compileSuccess && "Failed to compile the design");
        auto compilation = driver.createCompilation();

        std::shared_ptr<slang::syntax::SyntaxTree> tree = driver.syntaxTrees[0];

        if (!driver.options.topModules.empty()) {
            if (driver.options.topModules.size() > 1) {
                assert(false && "Multiple top-level modules specified!");
            }
            topModuleName = driver.options.topModules[0];
        }

        if (topModuleName.empty()) {
            topModuleName = compilation->getRoot().topInstances[0]->name;
            fmt::println("[slang-syntax-viewer] `--top` is not set, use `{}` as top module name", topModuleName);
        }

        int _depth = depth.value_or(9999);
        if (listSyntaxTree) {
            std::cout << "Listing syntax tree for module: " << topModuleName << std::endl;

            ModuleSyntaxTreePrinter printer(topModuleName, _depth);
            printer.visit(tree->root());

            if (!printer.wasFound()) {
                fmt::println("Error: Could not find module '{}' in syntax tree", topModuleName);
                exit(1);
            }
        }

        if (listAst) {
            std::cout << "Listing AST for module: " << topModuleName << std::endl;

            ModuleASTPrinter printer(topModuleName, _depth, tree);
            printer.visit(tree->root());

            if (!printer.wasFound()) {
                fmt::println("Error: Could not find module '{}' in syntax tree", topModuleName);
                exit(1);
            }
        }

        if (!listSyntaxTree && !listAst) {
            std::cout << "Neither `--list-syntax-tree/--lsyn` nor `--list-ast/--last` is set, do nothing." << std::endl;
        }
    }
};

int main(int argc, char **argv) {
    SlangSyntaxViewer viewer;
    viewer.parseCommandLine(argc, argv);
    return 0;
}