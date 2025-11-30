#include "SlangCommon.h"
#include "fmt/core.h"
#include "slang/driver/Driver.h"
#include "slang/syntax/SyntaxTree.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <optional>

class ModuleSyntaxGetter : public slang::syntax::SyntaxVisitor<ModuleSyntaxGetter> {
  public:
    std::string moduleName;
    uint32_t depth;
    bool found = false;

    ModuleSyntaxGetter(std::string moduleName, uint32_t depth) : moduleName(moduleName), depth(depth) {}

    void handle(const ModuleDeclarationSyntax &syntax) {
        if (syntax.header->name.rawText() == this->moduleName) {
            fmt::println("[ModuleSyntaxGetter] reach moduleName: {}", this->moduleName);
            slang_common::listSyntaxNode(syntax, depth);
            found = true;
            return;
        }

        visitDefault(syntax);
    }
};

class ModuleASTGetter : public slang::syntax::SyntaxVisitor<ModuleASTGetter> {
  public:
    std::string moduleName;
    uint32_t depth;
    std::shared_ptr<SyntaxTree> &tree;
    bool found = false;

    ModuleASTGetter(std::string moduleName, uint32_t depth, std::shared_ptr<SyntaxTree> &tree) : moduleName(moduleName), depth(depth), tree(tree) {}

    void handle(const ModuleDeclarationSyntax &syntax) {
        if (syntax.header->name.rawText() == this->moduleName) {
            fmt::println("[ModuleASTGetter] reach moduleName: {}", this->moduleName);
            slang_common::listASTNode(tree, syntax, depth);
            found = true;
            return;
        }

        visitDefault(syntax);
    }
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
            std::cout << "List syntax tree" << std::endl;

            ModuleSyntaxGetter getter(topModuleName, _depth);
            getter.visit(tree->root());
            assert(getter.found && fmt::format("Could not find module: {}", topModuleName).c_str());
        }

        if (listAst) {
            std::cout << "List AST" << std::endl;

            ModuleASTGetter getter(topModuleName, _depth, tree);
            getter.visit(tree->root());
            assert(getter.found && fmt::format("Could not find module: {}", topModuleName).c_str());
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