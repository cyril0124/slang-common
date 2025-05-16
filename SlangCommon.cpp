#include "SlangCommon.h"

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

std::shared_ptr<SyntaxTree> rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree, slang::SourceManager &sourceManager, const Bag &options) {
    auto oldTreeStr = SyntaxPrinter::printFile(oldTree);
    auto newTree    = SyntaxTree::fromFileInMemory(oldTreeStr, sourceManager, "slang_common::rebuildSyntaxTree"sv, "", options);
    if (newTree->diagnostics().empty() == false) {
        auto diags = newTree->diagnostics();
        if (checkDiagsError(diags)) {
            auto ret = DiagnosticEngine::reportAll(sourceManager, diags);
            fmt::println(R"(
=== [slang_common::rebuildSyntaxTree] SYNTAX ERROR ===
{}
======================================================
)",
                         ret);
            fflush(stdout);

            if (printTree) {
                fmt::println(R"(
=== [slang_common::rebuildSyntaxTree] ORIGINAL SYNTAX TREE ===
{}
==============================================================
)",
                             oldTreeStr);
                fflush(stdout);
            }

            assert(false && "[slang_common::rebuildSyntaxTree] Syntax error during syntax tree reconstruction");
        }
    } else {
        Compilation compilation(options);
        compilation.addSyntaxTree(newTree);
        auto diags = compilation.getAllDiagnostics();
        if (diags.empty() == false) {
            if (checkDiagsError(diags)) {
                auto ret = DiagnosticEngine::reportAll(sourceManager, diags);
                fmt::println(R"(
=== [slang_common::rebuildSyntaxTree] COMPILATION ERROR ===
{}
===========================================================
)",
                             ret);
                fflush(stdout);

                if (printTree) {
                    fmt::println(R"(
=== [slang_common::rebuildSyntaxTree] ORIGINAL SYNTAX TREE ===
{}
==============================================================
)",
                                 oldTreeStr);
                    fflush(stdout);
                }

                assert(false && "[slang_common::rebuildSyntaxTree] Compilation error during syntax tree reconstruction");
            }
        }
    }

    return newTree;
}

namespace file_manage {

std::string backupFile(std::string_view inputFile, std::string workdir) {
    std::filesystem::path _workdir(workdir);
    std::filesystem::path path(inputFile);
    std::string targetFile = std::string(workdir) + "/" + path.filename().string() + ".bak";

    if (std::filesystem::exists(targetFile)) {
        std::filesystem::remove(targetFile);
    }
    std::filesystem::copy_file(inputFile, targetFile.c_str());

    INSERT_BEFORE_FILE_HEAD(targetFile, fmt::format("//BEGIN:{}", inputFile));
    INSERT_AFTER_FILE_END(targetFile, fmt::format("//END:{}", inputFile));

    return targetFile;
}

void generateNewFile(const std::string &content, const std::string &newPath) {
    std::istringstream stream(content);
    std::string line;
    std::string currentFile;
    std::ofstream outFile;
    std::vector<std::string> buffer;

    if (!newPath.empty()) {
        if (!std::filesystem::exists(newPath)) {
            std::filesystem::create_directories(newPath);
        }
    }

    auto flushBuffer = [&]() {
        if (!buffer.empty() && outFile.is_open()) {
            for (const auto &l : buffer) {
                outFile << l << '\n';
            }
            buffer.clear();
        }
    };

    while (std::getline(stream, line)) {
        if (line.find("//BEGIN:") == 0) {
            flushBuffer();
            currentFile = line.substr(8);

            std::filesystem::path path = currentFile;
            if (!newPath.empty()) {
                currentFile = newPath + "/" + path.filename().string();
            }

            outFile.open(currentFile, std::ios::out | std::ios::trunc);
            if (!outFile.is_open()) {
                std::cerr << "Failed to open file: " << currentFile << std::endl;
                assert(false);
            }
        } else if (line.find("//END:") == 0) {
            flushBuffer();
            if (outFile.is_open()) {
                outFile.close();
            }
        } else {
            if (outFile.is_open()) {
                buffer.push_back(line);
                if (buffer.size() >= 10000) {
                    flushBuffer();
                }
            }
        }
    }

    flushBuffer();
    if (outFile.is_open()) {
        outFile.close();
    }
}

bool isFileNewer(const std::string &file1, const std::string &file2) {
    try {
        auto time1 = std::filesystem::last_write_time(file1);
        auto time2 = std::filesystem::last_write_time(file2);

        return time1 > time2;
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "[isFileNewer] Error: " << e.what() << std::endl;
        return false;
    }
}

} // namespace file_manage

Driver::Driver(std::string name) : cmdLine(driver.cmdLine) {
    this->name = name;
    this->driver.cmdLine.add("-h,--help", showHelp, "Display available options");
}

Driver::~Driver() {}

void Driver::addStandardArgs() {
    driver.addStandardArgs();

    // Include paths (override default include paths of slang)
    driver.cmdLine.add(
        "-I,--include-directory,+incdir",
        [this](std::string_view value) {
            if (auto ec = this->driver.sourceManager.addUserDirectories(value)) {
                fmt::println("include directory '{}': {}", value, ec.message());
            }

            // Append the include directory into the default source manager which will be used by `slang_common::rebuildSyntaxTree()`.
            // Without this, the include directories will not be considered when building the syntax tree.
            this->emptySourceManager.addUserDirectories(value);

            return "";
        },
        "Additional include search paths", "<dir-pattern>[,...]", CommandLineFlags::CommaList);

    driver.cmdLine.add(
        "--isystem",
        [this](std::string_view value) {
            if (auto ec = this->driver.sourceManager.addSystemDirectories(value)) {
                fmt::println("system include directory '{}': {}", value, ec.message());
            }

            // The same as above, but for the default source manager
            this->emptySourceManager.addSystemDirectories(value);

            return "";
        },
        "Additional system include search paths", "<dir-pattern>[,...]", CommandLineFlags::CommaList);

    driver.cmdLine.setPositional(
        [this](std::string_view value) {
            if (!this->driver.options.excludeExts.empty()) {
                if (size_t extIndex = value.find_last_of('.'); extIndex != std::string_view::npos) {
                    if (driver.options.excludeExts.count(std::string(value.substr(extIndex + 1))))
                        return "";
                }
            }

            if (value.ends_with(".f")) {
                std::ifstream infile(value.data());
                if (!infile) {
                    fmt::println("Failed to open file: {}", value);
                    assert(false);
                } else {
                    std::string line;
                    while (std::getline(infile, line)) {
                        if (!line.empty()) {
                            this->files.push_back(line);
                        }
                    }
                    infile.close();
                }
            }

            this->files.push_back(std::string(value));
            return "";
        },
        "files", {}, true);

    driver.cmdLine.add("-h,--help", showHelp, "Display available options");
}

bool Driver::parseCommandLine(int argc, char **argv) {
    auto success = driver.parseCommandLine(argc, argv);
    if (showHelp) {
        std::cout << fmt::format("{}\n", driver.cmdLine.getHelpText(name).c_str());
        exit(0);
    }
    return success;
}

void Driver::loadAllSources(std::function<std::string(std::string_view)> fileTransform) {
    auto totalFileCount = files.size();

    if (!verbose) {
        fmt::println("[{}] Loading {} files... ", name, totalFileCount);
    }

    for (int i = 0; i < totalFileCount; i++) {
        auto file = files[i];

        if (verbose) {
            fmt::println("[{}] [{}/{}] get file: {}", name, i + 1, totalFileCount, file);
            fflush(stdout);
        } else {
            fmt::print("\t{}/{} {:.2f}%\r", i + 1, totalFileCount, (double)(i + 1) / (double)totalFileCount * 100);
            fflush(stdout);
        }

        if (fileTransform == nullptr) {
            driver.sourceLoader.addFiles(std::string_view(file));
        } else {
            driver.sourceLoader.addFiles(std::string_view(fileTransform(file)));
        }
    }

    if (!verbose) {
        fmt::println("");
    }

    loadAllSourcesDone = true;
}

bool Driver::processOptions(bool singleUnit) {
    driver.options.singleUnit = singleUnit;
    bool success              = driver.processOptions();

    auto &options = driver.options;

    // Add parser options
    {
        slang::driver::SourceOptions soptions;
        soptions.numThreads             = options.numThreads;
        soptions.singleUnit             = options.singleUnit == true;
        soptions.onlyLint               = options.lintMode();
        soptions.librariesInheritMacros = options.librariesInheritMacros == true;

        slang::parsing::PreprocessorOptions ppoptions;
        ppoptions.predefines      = options.defines;
        ppoptions.undefines       = options.undefines;
        ppoptions.predefineSource = "<command-line>";
        ppoptions.languageVersion = driver.languageVersion;
        if (options.maxIncludeDepth.has_value())
            ppoptions.maxIncludeDepth = *options.maxIncludeDepth;
        for (const auto &d : options.ignoreDirectives)
            ppoptions.ignoreDirectives.emplace(d);

        slang::parsing::LexerOptions loptions;
        loptions.languageVersion     = driver.languageVersion;
        loptions.enableLegacyProtect = options.enableLegacyProtect == true;
        if (options.maxLexerErrors.has_value())
            loptions.maxErrors = *options.maxLexerErrors;

        if (loptions.enableLegacyProtect)
            loptions.commentHandlers["pragma"]["protect"] = {CommentHandler::Protect};

        slang::parsing::ParserOptions poptions;
        poptions.languageVersion = driver.languageVersion;
        if (options.maxParseDepth.has_value())
            poptions.maxRecursionDepth = *options.maxParseDepth;

        bag.set(soptions);
        bag.set(ppoptions);
        bag.set(loptions);
        bag.set(poptions);
    }

    // Add compilation options
    {
        slang::ast::CompilationOptions coptions;
        coptions.flags           = slang::ast::CompilationFlags::None;
        coptions.languageVersion = driver.languageVersion;
        if (options.maxInstanceDepth.has_value())
            coptions.maxInstanceDepth = *options.maxInstanceDepth;
        if (options.maxGenerateSteps.has_value())
            coptions.maxGenerateSteps = *options.maxGenerateSteps;
        if (options.maxConstexprDepth.has_value())
            coptions.maxConstexprDepth = *options.maxConstexprDepth;
        if (options.maxConstexprSteps.has_value())
            coptions.maxConstexprSteps = *options.maxConstexprSteps;
        if (options.maxConstexprBacktrace.has_value())
            coptions.maxConstexprBacktrace = *options.maxConstexprBacktrace;
        if (options.maxInstanceArray.has_value())
            coptions.maxInstanceArray = *options.maxInstanceArray;
        if (options.maxUDPCoverageNotes.has_value())
            coptions.maxUDPCoverageNotes = *options.maxUDPCoverageNotes;
        if (options.errorLimit.has_value())
            coptions.errorLimit = *options.errorLimit * 2;

        for (auto &[flag, value] : options.compilationFlags) {
            if (value == true)
                coptions.flags |= flag;
        }

        if (options.lintMode())
            coptions.flags |= slang::ast::CompilationFlags::SuppressUnused;

        for (auto &name : options.topModules)
            coptions.topModules.emplace(name);
        for (auto &opt : options.paramOverrides)
            coptions.paramOverrides.emplace_back(opt);
        for (auto &lib : options.libraryOrder)
            coptions.defaultLiblist.emplace_back(lib);

        if (options.minTypMax.has_value()) {
            if (options.minTypMax == "min")
                coptions.minTypMax = slang::ast::MinTypMax::Min;
            else if (options.minTypMax == "typ")
                coptions.minTypMax = slang::ast::MinTypMax::Typ;
            else if (options.minTypMax == "max")
                coptions.minTypMax = slang::ast::MinTypMax::Max;
        }

        if (options.timeScale.has_value())
            coptions.defaultTimeScale = slang::TimeScale::fromString(*options.timeScale);

        bag.set(coptions);
    }

    return success;
}

bool Driver::parseAllSources() {
    if (!loadAllSourcesDone) {
        assert(false && "loadAllSources() must be called before parseAllSources()");
    }

    parseAllSourcesDone = true;
    return driver.parseAllSources();
}

bool Driver::reportParseDiags() { return driver.reportParseDiags(); }

std::unique_ptr<slang::ast::Compilation> Driver::createCompilation() { return driver.createCompilation(); }

bool Driver::reportCompilation(slang::ast::Compilation &compilation, bool quiet) { return driver.reportCompilation(compilation, quiet); }

std::unique_ptr<slang::ast::Compilation> Driver::createAndReportCompilation(bool quiet) {
    auto compilation = this->createCompilation();
    if (!this->reportCompilation(*compilation, quiet)) {
        assert(false && "reportCompilation() failed");
    }
    return compilation;
}

// Wrap slang_common::rebuildSyntaxTree
std::shared_ptr<SyntaxTree> Driver::rebuildSyntaxTree(const SyntaxTree &oldTree, bool printTree) {
    return slang_common::rebuildSyntaxTree(oldTree, printTree, this->getEmptySourceManager(), this->getBag());
}

class SynaxLister : public SyntaxVisitor<SynaxLister> {
  public:
    const uint64_t maxDepth;
    uint64_t depth = 0;
    uint64_t count = 0;
    std::vector<bool> lastChildStack;

    SynaxLister(uint64_t maxDepth = 10000) : maxDepth(maxDepth) {}

#define SYNTAX_NAME()                                                                                                                                                                                  \
    extra += "\tsynName: ";                                                                                                                                                                            \
    auto name = boost::typeindex::type_id<decltype(syn)>().pretty_name();                                                                                                                              \
    extra += std::string(name);                                                                                                                                                                        \
    extra += " ";

#define PREFIX_CODE()                                                                                                                                                                                  \
    if (depth > maxDepth)                                                                                                                                                                              \
        return;                                                                                                                                                                                        \
    std::string prefix = createPrefix();                                                                                                                                                               \
    std::string extra  = "";                                                                                                                                                                           \
    SYNTAX_NAME();

#define PRINT_INFO_AND_VISIT()                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        fmt::println("{}[{}] depth: {}\tsynKind: {}\t{}", prefix, count, depth, toString(syn.kind), extra);                                                                                            \
        count++;                                                                                                                                                                                       \
        lastChildStack.push_back(false);                                                                                                                                                               \
        depth++;                                                                                                                                                                                       \
        visitDefault(syn);                                                                                                                                                                             \
        lastChildStack.pop_back();                                                                                                                                                                     \
        depth--;                                                                                                                                                                                       \
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
#undef SYNTAX_NAME
#undef PREFIX_CODE
#undef PRINT_INFO_AND_VISIT
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

#define AST_NAME()                                                                                                                                                                                     \
    extra += "\tastName: ";                                                                                                                                                                            \
    auto name = boost::typeindex::type_id<decltype(ast)>().pretty_name();                                                                                                                              \
    extra += std::string(name);                                                                                                                                                                        \
    extra += " ";                                                                                                                                                                                      \
    extra += "\tsynKindName: ";                                                                                                                                                                        \
    if constexpr (has_getSyntax<decltype(ast)>::value) {                                                                                                                                               \
        const auto syn = ast.getSyntax();                                                                                                                                                              \
        if (syn != nullptr) {                                                                                                                                                                          \
            auto synKind = syn->kind;                                                                                                                                                                  \
            extra += toString(synKind);                                                                                                                                                                \
        } else {                                                                                                                                                                                       \
            extra += "Null";                                                                                                                                                                           \
        }                                                                                                                                                                                              \
    } else {                                                                                                                                                                                           \
        extra += "None";                                                                                                                                                                               \
    }

#define PREFIX_CODE()                                                                                                                                                                                  \
    if (depth > maxDepth)                                                                                                                                                                              \
        return;                                                                                                                                                                                        \
    std::string prefix = createPrefix();                                                                                                                                                               \
    std::string extra  = "";                                                                                                                                                                           \
    AST_NAME();

#define PRINT_INFO_AND_VISIT()                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        fmt::println("{}[{}] depth: {}\tastKind: {}\t{}", prefix, count, depth, toString(ast.kind), extra);                                                                                            \
        count++;                                                                                                                                                                                       \
        lastChildStack.push_back(false);                                                                                                                                                               \
        depth++;                                                                                                                                                                                       \
        visitDefault(ast);                                                                                                                                                                             \
        lastChildStack.pop_back();                                                                                                                                                                     \
        depth--;                                                                                                                                                                                       \
    } while (0)

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

        extra += fmt::format(" portName: {} dir: {} internalKind: {} portWidth: {} portType: {} portTypeKind: {}", port.name, toString(port.direction), toString(internalKind), pType.getBitWidth(),
                             pType.toString(), toString(pType.kind));

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
#undef AST_NAME
#undef PREFIX_CODE
#undef PRINT_INFO_AND_VISIT
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

void listSyntaxTree(const slang::syntax::SyntaxTree *tree, uint64_t maxDepth = 1000) {
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

std::vector<std::string> getHierPaths(slang::ast::Compilation &compilation, std::string moduleName) {
    struct HierPathGetter : public slang::ast::ASTVisitor<HierPathGetter, false, false> {
        std::string moduleName;
        std::vector<std::string> hierPaths;
        HierPathGetter(std::string moduleName) : moduleName(moduleName) {}

        void handle(const InstanceSymbol &inst) {
            auto _moduleName = inst.getDefinition().name;

            if (_moduleName == moduleName) {
                // Hierarchical path may come from multiple instances
                std::string hierPath = "";
                inst.getHierarchicalPath(hierPath);
                hierPaths.emplace_back(hierPath);
            } else {
                visitDefault(inst);
            }
        }
    };

    HierPathGetter visitor(moduleName);
    compilation.getRoot().visit(visitor);
    return visitor.hierPaths;
}

std::vector<std::string> getHierPaths(slang::ast::Compilation *compilation, std::string moduleName) { return getHierPaths(*compilation, moduleName); }

std::vector<std::string> getHierPaths(slang::ast::Compilation *compilation, std::string_view moduleName) { return getHierPaths(*compilation, std::string(moduleName)); }

} // namespace slang_common