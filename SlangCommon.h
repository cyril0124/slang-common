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

namespace slang_common {

namespace file_operations {
/// @brief Insert content before the head of a file
/// @param filePath Path to the file to modify
/// @param str Content to insert
/// @return true if successful, false otherwise
bool insertBeforeFileHead(std::string_view filePath, std::string_view str);

/// @brief Insert content after the end of a file
/// @param filePath Path to the file to modify
/// @param str Content to append
/// @return true if successful, false otherwise
bool insertAfterFileEnd(std::string_view filePath, std::string_view str);
} // namespace file_operations

namespace file_manage {
std::string backupFile(std::string_view inputFile, std::string workdir);
bool isFileNewer(const std::string &file1, const std::string &file2);
void generateNewFile(const std::string &content, const std::string &newPath);
} // namespace file_manage

/// @brief Check if diagnostics contain any errors
/// @param diags The diagnostics collection to check
/// @return true if any error diagnostic is found, false otherwise
bool checkDiagsError(Diagnostics &diags);

/// @brief Rebuild a syntax tree from its printed representation
/// @param oldTree The original syntax tree
/// @param printTree Whether to print the tree on error
/// @param sourceManager Source manager for the new tree
/// @param options Compilation options
/// @return Rebuilt syntax tree
std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree = false, slang::SourceManager &sourceManager = SyntaxTree::getDefaultSourceManager(), const Bag &options = {});

/// @brief Rebuild a syntax tree with error limit
/// @param oldTree The original syntax tree
/// @param printTree Whether to print the tree on error
/// @param errorLimit Maximum number of errors to report
/// @param sourceManager Source manager for the new tree
/// @param options Compilation options
/// @return Rebuilt syntax tree
std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree = false, int errorLimit = 0, slang::SourceManager &sourceManager = SyntaxTree::getDefaultSourceManager(),
                                              const Bag &options = {});

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

    /// @brief Get list of input files
    std::vector<std::string> &getFiles() { return files; }

    /// @brief Get the source manager (for rebuildSyntaxTree operations)
    slang::SourceManager &getEmptySourceManager() { return emptySourceManager; }

    /// @brief Get underlying slang driver for advanced operations
    slang::driver::Driver &getInternalDriver() { return driver; }

    /// @brief Get bag of compilation options
    slang::Bag &getBag() { return bag; }

    /**
     * @brief Attempt to get the top module name from command line options
     * @return Optional containing top module name if specified
     * @throws Assertion failure if multiple top modules are specified
     */
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

    /**
     * @brief Get the single syntax tree (for single-file compilation)
     * @return Shared pointer to the syntax tree
     * @throws Assertion failure if multiple syntax trees exist
     */
    std::shared_ptr<slang::syntax::SyntaxTree> getSingleSyntaxTree() {
        assert(parseAllSourcesDone && "parseAllSources() must be called before getSingleSyntaxTree()");

        if (driver.syntaxTrees.size() != 1) {
            fmt::println("driver.syntaxTrees.size(): {}", driver.syntaxTrees.size());
            assert(false && "Multiple syntax trees found!");
        }
        return driver.syntaxTrees[0];
    }

    /// @brief Add standard command-line arguments (includes, defines, etc.)
    void addStandardArgs();

    /// @brief Parse command line arguments
    bool parseCommandLine(int argc, char **argv);

    /// @brief Load all source files with optional transformation
    /// @param fileTransform Optional function to transform file paths before loading
    void loadAllSources(std::function<std::string(std::string_view)> fileTransform = nullptr);

    /// @brief Process compilation options
    /// @param singleUnit Whether to compile as single unit
    bool processOptions(bool singleUnit = true);

    /// @brief Parse all loaded source files
    bool parseAllSources();

    /// @brief Report any parsing diagnostics
    bool reportParseDiags();

    /// @brief Create compilation object from parsed sources
    std::unique_ptr<slang::ast::Compilation> createCompilation();

    /// @brief Create compilation and report any errors
    /// @param quiet Whether to suppress output
    std::unique_ptr<slang::ast::Compilation> createAndReportCompilation(bool quiet = false);

    /// @brief Rebuild a syntax tree (wrapper for slang_common::rebuildSyntaxTree)
    std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree = false, int errorLimit = 0);
};

/// @brief Print AST (Abstract Syntax Tree) of a syntax tree
/// @param tree Syntax tree to print
/// @param maxDepth Maximum depth to traverse
void listAST(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth);

/// @brief Print syntax tree structure
/// @param tree Syntax tree to print
/// @param maxDepth Maximum depth to traverse
void listSyntaxTree(std::shared_ptr<SyntaxTree> tree, uint64_t maxDepth);

/// @brief Print syntax tree structure (pointer version)
/// @param tree Pointer to syntax tree
/// @param maxDepth Maximum depth to traverse
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

#define INSERT_BEFORE_FILE_HEAD(filePath, str) slang_common::file_operations::insertBeforeFileHead(filePath, str)
#define INSERT_AFTER_FILE_END(filePath, str) slang_common::file_operations::insertAfterFileEnd(filePath, str)