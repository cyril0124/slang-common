#pragma once

#include "fmt/core.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"
#include "slang/diagnostics/DiagnosticEngine.h"
#include "slang/diagnostics/Diagnostics.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/driver/Driver.h"
#include "slang/numeric/Time.h"
#include "slang/parsing/Lexer.h"
#include "slang/parsing/Parser.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/syntax/SyntaxKind.h"
#include "slang/syntax/SyntaxNode.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/SyntaxVisitor.h"
#include "slang/text/SourceLocation.h"
#include "slang/text/SourceManager.h"
#include "slang/util/Bag.h"
#include "slang/util/CommandLine.h"
#include "slang/util/LanguageVersion.h"
#include "slang/util/Util.h"
#include <boost/type_index.hpp>
#include <cstddef>
#include <cstdint>
#include <fmt/ranges.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace slang;
using namespace slang::parsing;
using namespace slang::syntax;
using namespace slang::ast;

#define INSERT_BEFORE_FILE_HEAD(filePath, str)                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        std::ifstream inFile(filePath);                                                                                                                                                                \
        std::stringstream buffer;                                                                                                                                                                      \
        buffer << "\n" << str << "\n" << inFile.rdbuf();                                                                                                                                               \
        inFile.close();                                                                                                                                                                                \
        std::ofstream outFile(filePath);                                                                                                                                                               \
        outFile << buffer.rdbuf();                                                                                                                                                                     \
        outFile.close();                                                                                                                                                                               \
    } while (0)

#define INSERT_AFTER_FILE_END(filePath, str)                                                                                                                                                           \
    do {                                                                                                                                                                                               \
        std::ofstream outFile(filePath, std::ios::app);                                                                                                                                                \
        outFile << "\n" << str;                                                                                                                                                                        \
        outFile.close();                                                                                                                                                                               \
    } while (0)

namespace slang_common {

namespace file_manage {
std::string backupFile(std::string_view inputFile, std::string workdir);
bool isFileNewer(const std::string &file1, const std::string &file2);
void generateNewFile(const std::string &content, const std::string &newPath);
} // namespace file_manage

bool checkDiagsError(Diagnostics &diags);

std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree = false, slang::SourceManager &sourceManager = SyntaxTree::getDefaultSourceManager(), const Bag &options = {});

class Driver {
  private:
    slang::SourceManager emptySourceManager;
    slang::Bag bag;
    std::vector<std::string> files;
    std::optional<bool> showHelp;
    std::string name;
    bool loadAllSourcesDone  = false;
    bool parseAllSourcesDone = false;
    bool verbose             = false;

  public:
    slang::driver::Driver driver;
    slang::CommandLine &cmdLine;

    Driver(std::string name = "Unknown");
    ~Driver();

    void setName(std::string name) { this->name = name; }
    void setVerbose(bool verbose) { this->verbose = verbose; }
    void addFile(std::string_view file) { files.push_back(std::string(file)); }
    void addFiles(std::vector<std::string> files) {
        for (auto &file : files) {
            this->files.push_back(file);
        }
    }
    std::vector<std::string> &getFiles() { return files; }
    slang::SourceManager &getEmptySourceManager() { return emptySourceManager; }
    slang::driver::Driver &getInternalDriver() { return driver; }
    slang::Bag &getBag() { return bag; }
    std::optional<std::string> tryGetTopModuleName() {
        if (!driver.options.topModules.empty()) {
            if (driver.options.topModules.size() > 1) {
                fmt::println("topModules: {}", fmt::to_string(fmt::join(driver.options.topModules, ",")));
                assert(false && "Multiple top modules specified!");
            }
            return std::optional<std::string>(driver.options.topModules[0]);
        }
        return std::nullopt;
    }
    std::shared_ptr<slang::syntax::SyntaxTree> getSingleSyntaxTree() {
        assert(parseAllSourcesDone && "parseAllSources() must be called before getSingleSyntaxTree()");

        if (driver.syntaxTrees.size() != 1) {
            fmt::println("driver.syntaxTrees.size(): {}", driver.syntaxTrees.size());
            assert(false && "Multiple syntax trees found!");
        }
        return driver.syntaxTrees[0];
    }
    void addStandardArgs();
    bool parseCommandLine(int argc, char **argv);
    void loadAllSources(std::function<std::string(std::string_view)> fileTransform = nullptr);
    bool processOptions(bool singleUnit = true);
    bool parseAllSources();
    bool reportParseDiags();
    bool reportCompilation(slang::ast::Compilation &compilation, bool quiet = false);
    std::unique_ptr<slang::ast::Compilation> createCompilation();
    std::unique_ptr<slang::ast::Compilation> createAndReportCompilation(bool quiet = false);
    std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree = false);
};

void listAST(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth);

void listSyntaxTree(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth);

void listSyntaxTree(const slang::syntax::SyntaxTree *tree, uint64_t maxDepth);

void listSyntaxNode(const SyntaxNode &node, uint64_t maxDepth);

void listASTNode(std::shared_ptr<SyntaxTree> tree, const ModuleDeclarationSyntax &syntax, uint64_t maxDepth);

const DefinitionSymbol *getDefSymbol(std::shared_ptr<SyntaxTree> tree, const ModuleDeclarationSyntax &syntax);

const InstanceSymbol *getInstSymbol(Compilation &compilation, const ModuleDeclarationSyntax &syntax);

const SyntaxNode *getNetDeclarationSyntax(const SyntaxNode *node, std::string_view identifierName, bool reverse = false);

std::vector<std::string> getHierPaths(slang::ast::Compilation &compilation, std::string moduleName);

std::vector<std::string> getHierPaths(slang::ast::Compilation *compilation, std::string moduleName);

std::vector<std::string> getHierPaths(slang::ast::Compilation *compilation, std::string_view moduleName);

} // namespace slang_common