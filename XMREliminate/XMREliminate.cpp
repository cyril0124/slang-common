/*
 * XMREliminate.cpp - XMR Elimination main implementation
 *
 * This is the main entry point for XMR elimination. It coordinates the
 * detection, change computation, and rewriting phases.
 *
 * For detailed documentation see the header file and the workflow diagrams
 * in this file.
 */

#include "../XMREliminate.h"
#include "../SlangCommon.h"
#include "XMRChangeSet.h"
#include "XMRDetector.h"
#include "XMRRewriter.h"
#include "XMRTypes.h"
#include "fmt/core.h"
#include "slang/syntax/SyntaxPrinter.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace slang_common {
namespace xmr {

/*
================================================================================
XMR ELIMINATE WORKFLOW DIAGRAM
================================================================================

This module converts Cross-Module References (XMRs) to explicit port connections.
It supports two types of XMR references:

  1. DOWNWARD XMR (Relative Path) - e.g., top: u_sub.data
     Signal flows UP through hierarchy via output ports.

  2. UPWARD XMR (Absolute Path) - e.g., others: tb_top.uut.counter
     Signal flows DOWN through hierarchy via input ports to the source module.

================================================================================
PROCESSING PIPELINE
================================================================================

  ┌────────────────┐     ┌─────────────────┐     ┌───────────────────┐
  │   Input Files  │────>│  Single Driver  │────>│  XMR Detection    │
  │   (.sv files)  │     │   (Compile)     │     │  (AST Visitor)    │
  └────────────────┘     └─────────────────┘     └─────────┬─────────┘
                                                           │
                              ┌─────────────────────────────┘
                              ▼
                  ┌───────────────────────────┐
                  │   Detect Upward References│
                  │   (HierarchicalReference. │
                  │    upwardCount > 0)       │
                  └───────────────┬───────────┘
                                  │
                                  ▼
  ┌────────────────┐     ┌─────────────────┐     ┌─────────────────────┐
  │  Output Files  │<────│  2-Pass Rewrite │<────│  Compute ChangeSet  │
  │  (Modified)    │     │  (Syntax Based) │     │  (Ports, Wires...)  │
  └────────────────┘     └─────────────────┘     └─────────────────────┘

  Pass 1: Module modifications (ports, assigns, XMR replacements)
  Pass 2: Instance connection updates

================================================================================
*/

//==============================================================================
// XMRPipeRegConfig implementation
//==============================================================================

int XMRPipeRegConfig::getRegCountForModule(const std::string &moduleName, const std::string &signalName) const {
    switch (mode) {
    case PipeRegMode::None:
        return 0;
    case PipeRegMode::Global:
        return globalRegCount;
    case PipeRegMode::PerModule:
        return 1;
    case PipeRegMode::Selective: {
        for (const auto &entry : entries) {
            if (entry.moduleName == moduleName) {
                if (entry.signals.empty()) {
                    return entry.regCount;
                }
                if (!signalName.empty()) {
                    auto it = std::find(entry.signals.begin(), entry.signals.end(), signalName);
                    if (it != entry.signals.end()) {
                        return entry.regCount;
                    }
                }
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

XMRPipeRegConfig XMRPipeRegConfig::createGlobal(int regCount) {
    XMRPipeRegConfig config;
    config.mode           = PipeRegMode::Global;
    config.globalRegCount = regCount;
    return config;
}

XMRPipeRegConfig XMRPipeRegConfig::createPerModule() {
    XMRPipeRegConfig config;
    config.mode = PipeRegMode::PerModule;
    return config;
}

XMRPipeRegConfig XMRPipeRegConfig::createSelective(const std::vector<PipeRegEntry> &entries) {
    XMRPipeRegConfig config;
    config.mode    = PipeRegMode::Selective;
    config.entries = entries;
    return config;
}

//==============================================================================
// XMRInfo implementation
//==============================================================================

std::string XMRInfo::getUniqueId() const { return fmt::format("{}_{}", sourceModule, fullPath); }

std::string XMRInfo::getPortName() const {
    std::string result     = "__xmr__";
    bool lastWasUnderscore = true;

    for (char c : fullPath) {
        if (c == '.' || c == ' ' || c == '\t' || c == '\n') {
            if (!lastWasUnderscore) {
                result += "_";
                lastWasUnderscore = true;
            }
        } else {
            result += c;
            lastWasUnderscore = false;
        }
    }
    return result;
}

//==============================================================================
// XMREliminateResult implementation
//==============================================================================

std::string XMREliminateResult::getSummary() const {
    std::stringstream ss;

    ss << "\n";
    ss << "================================================================================\n";
    ss << "                          XMR ELIMINATION SUMMARY                               \n";
    ss << "================================================================================\n\n";

    // Top module info
    if (!detectedTopModules.empty()) {
        ss << "Detected top module(s): ";
        for (size_t i = 0; i < detectedTopModules.size(); ++i) {
            if (i > 0)
                ss << ", ";
            ss << detectedTopModules[i];
        }
        ss << "\n";
    }
    if (!usedTopModule.empty()) {
        ss << "Used top module: " << usedTopModule << "\n";
    }
    ss << "\n";

    // XMR statistics
    ss << "XMRs Eliminated: " << eliminatedXMRs.size() << "\n";
    ss << "Output Directory: " << outputDir << "\n\n";

    if (!eliminatedXMRs.empty()) {
        // Group XMRs by source module
        std::map<std::string, std::vector<const XMRInfo *>> xmrsByModule;
        for (const auto &xmr : eliminatedXMRs) {
            xmrsByModule[xmr.sourceModule].push_back(&xmr);
        }

        ss << "XMR Details by Module:\n";
        ss << "----------------------\n";
        for (const auto &[moduleName, xmrs] : xmrsByModule) {
            ss << "\n  Module: " << moduleName << " (" << xmrs.size() << " XMRs)\n";
            for (const auto *xmr : xmrs) {
                ss << "    - " << xmr->fullPath;
                ss << " -> " << xmr->targetModule << "." << xmr->targetSignal;
                ss << " (width: " << xmr->bitWidth << ")\n";
            }
        }

        // List unique target modules
        std::set<std::string> targetModules;
        for (const auto &xmr : eliminatedXMRs) {
            if (!xmr.targetModule.empty()) {
                targetModules.insert(xmr.targetModule);
            }
        }

        if (!targetModules.empty()) {
            ss << "\nTarget Modules Affected: ";
            bool first = true;
            for (const auto &mod : targetModules) {
                if (!first)
                    ss << ", ";
                ss << mod;
                first = false;
            }
            ss << "\n";
        }
    }

    // Warnings
    if (!warnings.empty()) {
        ss << "\nWarnings:\n";
        for (const auto &w : warnings) {
            ss << "  - " << w << "\n";
        }
    }

    // Errors
    if (!errors.empty()) {
        ss << "\nErrors:\n";
        for (const auto &e : errors) {
            ss << "  - " << e << "\n";
        }
    }

    ss << "\n================================================================================\n";

    return ss.str();
}

//==============================================================================
// Pipeline Register Generator
//==============================================================================

std::string generatePipelineRegisters(const std::string &inputSignal, const std::string &outputSignal, int bitWidth, int regCount, const std::string &clockName, const std::string &resetName,
                                      bool resetActiveLow) {
    if (regCount <= 0) {
        return "";
    }

    std::stringstream ss;

    std::string widthSpec = (bitWidth > 1) ? fmt::format("[{}:0] ", bitWidth - 1) : "";
    std::string resetCond = resetActiveLow ? fmt::format("!{}", resetName) : resetName;

    std::string regBaseName = outputSignal;

    // Declare pipeline registers
    for (int i = 0; i < regCount; i++) {
        ss << fmt::format("    reg {}{}_pipe_{};\n", widthSpec, regBaseName, i);
    }

    // Generate always block
    ss << fmt::format("    always @(posedge {} or {} {}) begin\n", clockName, resetActiveLow ? "negedge" : "posedge", resetName);
    ss << fmt::format("        if ({}) begin\n", resetCond);

    // Reset values
    for (int i = 0; i < regCount; i++) {
        ss << fmt::format("            {}_pipe_{} <= {}'h0;\n", regBaseName, i, bitWidth);
    }

    ss << "        end else begin\n";

    // Pipeline chain
    ss << fmt::format("            {}_pipe_0 <= {};\n", regBaseName, inputSignal);
    for (int i = 1; i < regCount; i++) {
        ss << fmt::format("            {}_pipe_{} <= {}_pipe_{};\n", regBaseName, i, regBaseName, i - 1);
    }

    ss << "        end\n";
    ss << "    end\n";

    // Assign output port from the last pipeline stage
    ss << fmt::format("    assign {} = {}_pipe_{};\n", outputSignal, regBaseName, regCount - 1);

    return ss.str();
}

//==============================================================================
// Top Module Detection
//==============================================================================

std::vector<std::string> detectTopModules(slang::ast::Compilation &compilation) {
    internal::TopModuleDetector detector;
    compilation.getRoot().visit(detector);

    std::vector<std::string> topModules;
    for (const auto &mod : detector.allModules) {
        if (detector.instantiatedModules.count(mod) == 0) {
            topModules.push_back(mod);
        }
    }

    std::sort(topModules.begin(), topModules.end());
    return topModules;
}

//==============================================================================
// Internal helper functions
//==============================================================================

/// @brief Detect XMRs using an existing compilation (no new Driver creation)
static std::vector<XMRInfo> detectXMRsFromCompilation(slang::ast::Compilation &compilation, const std::vector<std::string> &targetModules) {
    internal::XMRDetector detector(compilation, targetModules);
    compilation.getRoot().visit(detector);
    return detector.detectedXMRs;
}

/// @brief Verify clock/reset signals in modules using existing compilation
static std::vector<std::string> verifyClockResetSignals(slang::ast::Compilation &compilation, const XMREliminateConfig &config) {
    std::vector<std::string> errors;

    internal::ClockResetVerifier verifier(config.clockName, config.resetName);
    compilation.getRoot().visit(verifier);

    for (const auto &[modName, pipeConfig] : config.pipeRegConfigMap) {
        if (pipeConfig.isEnabled()) {
            if (verifier.modulesWithClock.count(modName) == 0) {
                errors.push_back(fmt::format("Pipeline registers requested for module '{}' but clock signal '{}' not found", modName, config.clockName));
            }
            if (verifier.modulesWithReset.count(modName) == 0) {
                errors.push_back(fmt::format("Pipeline registers requested for module '{}' but reset signal '{}' not found", modName, config.resetName));
            }
        }
    }

    return errors;
}

//==============================================================================
// Public API implementations
//==============================================================================

std::vector<XMRInfo> detectXMRs(const std::vector<std::string> &inputFiles, const std::vector<std::string> &targetModules) {
    for (const auto &file : inputFiles) {
        if (!std::filesystem::exists(file)) {
            return {};
        }
    }

    slang_common::Driver driver("XMRDetector");
    driver.addStandardArgs();
    driver.addFiles(const_cast<std::vector<std::string> &>(inputFiles));
    driver.loadAllSources();
    driver.processOptions(true);
    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    if (!compilation) {
        return {};
    }

    return detectXMRsFromCompilation(*compilation, targetModules);
}

XMRInfo analyzeXMRPath(slang::ast::Compilation &compilation, const slang::ast::HierarchicalValueExpression &xmrExpr) {
    XMRInfo info;

    info.targetSignal = std::string(xmrExpr.symbol.name);
    info.bitWidth     = static_cast<int>(xmrExpr.type->getBitWidth());

    if (xmrExpr.syntax) {
        std::string pathStr = xmrExpr.syntax->toString();
        size_t first        = pathStr.find_first_not_of(" \t\n\r");
        if (first != std::string::npos) {
            size_t last   = pathStr.find_last_not_of(" \t\n\r");
            info.fullPath = pathStr.substr(first, last - first + 1);
        }
    }

    const auto &hierRef = xmrExpr.ref;
    for (const auto &elem : hierRef.path) {
        info.pathSegments.push_back(std::string(elem.symbol->name));
    }

    return info;
}

//==============================================================================
// Main XMR Elimination Function
//==============================================================================

XMREliminateResult xmrEliminate(const std::vector<std::string> &inputFiles, const XMREliminateConfig &config, const std::string &outputDir) {
    XMREliminateResult result;

    //==========================================================================
    // Step 1: Validate inputs
    //==========================================================================
    if (inputFiles.empty()) {
        result.errors.push_back("No input files provided");
        return result;
    }

    for (const auto &file : inputFiles) {
        if (!std::filesystem::exists(file)) {
            result.errors.push_back(fmt::format("Input file does not exist: {}", file));
            return result;
        }
    }

    //==========================================================================
    // Step 2: Determine output directory and create work directory
    //==========================================================================
    // outputDir should already be an absolute path (handled by caller)
    std::string actualOutputDir = outputDir;
    if (actualOutputDir.empty()) {
        actualOutputDir = std::filesystem::absolute(".xmrEliminate").string();
    }

    std::string workDir = actualOutputDir + "/.work";
    if (!std::filesystem::exists(workDir)) {
        std::filesystem::create_directories(workDir);
    }

    //==========================================================================
    // Step 3: Create SINGLE Driver for all operations
    //==========================================================================
    slang_common::Driver driver("XMREliminator");
    driver.addStandardArgs();

    // Apply driver options from config (include dirs, defines, etc.)
    const auto &driverOpts = config.driverOptions;
    for (const auto &dir : driverOpts.includeDirs) {
        driver.driver.sourceManager.addUserDirectories(dir);
        driver.getEmptySourceManager().addUserDirectories(dir);
    }
    for (const auto &dir : driverOpts.systemIncludeDirs) {
        driver.driver.sourceManager.addSystemDirectories(dir);
        driver.getEmptySourceManager().addSystemDirectories(dir);
    }
    for (const auto &def : driverOpts.defines) {
        driver.driver.options.defines.push_back(def);
    }
    for (const auto &undef : driverOpts.undefines) {
        driver.driver.options.undefines.push_back(undef);
    }
    for (const auto &dir : driverOpts.libDirs) {
        driver.driver.sourceLoader.addSearchDirectories(dir);
    }
    for (const auto &ext : driverOpts.libExts) {
        driver.driver.sourceLoader.addSearchExtension(ext);
    }

    std::vector<std::string> backupFiles;

    driver.addFiles(const_cast<std::vector<std::string> &>(inputFiles));
    driver.loadAllSources([&workDir, &backupFiles](std::string_view file) {
        auto newFile = slang_common::file_manage::backupFile(file, workDir);
        auto f       = std::filesystem::absolute(newFile).string();
        backupFiles.push_back(f);
        return f;
    });

    driver.processOptions(false);
    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    if (!compilation) {
        result.errors.push_back("Failed to create compilation");
        return result;
    }

    //==========================================================================
    // Step 4: Detect top modules
    //==========================================================================
    result.detectedTopModules = detectTopModules(*compilation);

    std::cout << "\n[XMR Eliminate] Detected top module(s): ";
    if (result.detectedTopModules.empty()) {
        std::cout << "(none detected)";
    } else {
        for (size_t i = 0; i < result.detectedTopModules.size(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << result.detectedTopModules[i];
        }
    }
    std::cout << std::endl;

    if (!config.topModule.empty()) {
        result.usedTopModule = config.topModule;
        std::cout << "[XMR Eliminate] Using user-specified top module: " << config.topModule << std::endl;
    } else if (result.detectedTopModules.size() == 1) {
        result.usedTopModule = result.detectedTopModules[0];
        std::cout << "[XMR Eliminate] Using auto-detected top module: " << result.usedTopModule << std::endl;
    } else if (result.detectedTopModules.size() > 1) {
        result.warnings.push_back(fmt::format("Multiple top modules detected ({}). Use -t to specify one.", fmt::join(result.detectedTopModules, ", ")));
        std::cout << "[XMR Eliminate] Warning: Multiple top modules detected. Processing all modules with XMRs." << std::endl;
    }

    //==========================================================================
    // Step 5: Detect XMRs using the shared compilation
    //==========================================================================
    std::vector<std::string> targetModules = config.modules;
    if (targetModules.empty()) {
        std::cout << "[XMR Eliminate] No modules specified with -m, scanning all modules for XMRs..." << std::endl;
    } else {
        std::cout << "[XMR Eliminate] Scanning specified modules: ";
        for (size_t i = 0; i < targetModules.size(); ++i) {
            if (i > 0)
                std::cout << ", ";
            std::cout << targetModules[i];
        }
        std::cout << std::endl;
    }

    auto xmrInfos = detectXMRsFromCompilation(*compilation, targetModules);

    if (xmrInfos.empty()) {
        result.warnings.push_back("No XMR references found in specified modules");
        for (const auto &file : inputFiles) {
            std::ifstream ifs(file);
            if (ifs) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                result.modifiedFiles.push_back(buffer.str());
            }
        }
        for (const auto &backupFile : backupFiles) {
            if (std::filesystem::exists(backupFile)) {
                std::filesystem::remove(backupFile);
            }
        }
        return result;
    }

    result.eliminatedXMRs = xmrInfos;

    //==========================================================================
    // Step 6: Verify clock/reset signals if pipeline registers are requested
    //==========================================================================
    bool needsPipelineRegs = false;
    for (const auto &[modName, pipeConfig] : config.pipeRegConfigMap) {
        if (pipeConfig.isEnabled()) {
            needsPipelineRegs = true;
            break;
        }
    }

    if (needsPipelineRegs) {
        auto clockResetErrors = verifyClockResetSignals(*compilation, config);
        if (!clockResetErrors.empty()) {
            result.errors = std::move(clockResetErrors);
            for (const auto &backupFile : backupFiles) {
                if (std::filesystem::exists(backupFile)) {
                    std::filesystem::remove(backupFile);
                }
            }
            return result;
        }
    }

    //==========================================================================
    // Step 7: Compute all changes needed
    //==========================================================================
    internal::XMRChangeSet changeSet = internal::computeXMRChanges(xmrInfos, *compilation, config);

    //==========================================================================
    // Step 8: Rewrite Pass 1 - Module modifications
    //==========================================================================
    std::vector<std::unique_ptr<internal::XMRRewriterFirst>> firstRewriters;
    std::vector<std::unique_ptr<internal::XMRRewriterSecond>> secondRewriters;

    // Keep all intermediate trees alive - transformed trees may reference data from originals
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> allTrees;
    allTrees.reserve(driver.driver.syntaxTrees.size() * 3);
    for (auto &tree : driver.driver.syntaxTrees) {
        allTrees.push_back(tree);
    }

    std::shared_ptr<slang::syntax::SyntaxTree> newTree;
    for (size_t i = 0; i < driver.driver.syntaxTrees.size(); i++) {
        auto &tree = driver.driver.syntaxTrees[i];

        auto rewriter = std::make_unique<internal::XMRRewriterFirst>(changeSet, config);
        newTree       = rewriter->transform(tree);
        if (!newTree) {
            result.errors.push_back(fmt::format("First rewrite pass failed for tree {}", i));
            continue;
        }
        allTrees.push_back(newTree);
        tree = newTree;
        firstRewriters.push_back(std::move(rewriter));
    }

    //==========================================================================
    // Step 9: Rewrite Pass 2 - Instance connection updates
    //==========================================================================
    for (size_t i = 0; i < driver.driver.syntaxTrees.size(); i++) {
        auto &tree = driver.driver.syntaxTrees[i];
        if (!tree) {
            continue;
        }
        auto rewriter = std::make_unique<internal::XMRRewriterSecond>(changeSet);
        newTree       = rewriter->transform(tree);
        allTrees.push_back(newTree);
        tree = newTree;
        secondRewriters.push_back(std::move(rewriter));
    }

    //==========================================================================
    // Step 10: Generate output
    //==========================================================================
    std::vector<std::string> originalFilePaths;
    for (const auto &bak : backupFiles) {
        std::filesystem::path bakPath(bak);
        std::string filename = bakPath.filename().string();
        if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".bak") {
            filename = filename.substr(0, filename.size() - 4);
        }
        std::ifstream file(bak);
        if (file.is_open()) {
            std::string firstLine;
            if (std::getline(file, firstLine)) {
                if (firstLine.find("//BEGIN:") == 0) {
                    originalFilePaths.push_back(firstLine.substr(8));
                    continue;
                }
            }
        }
        originalFilePaths.push_back(filename);
    }

    for (size_t i = 0; i < driver.driver.syntaxTrees.size(); i++) {
        auto &tree = driver.driver.syntaxTrees[i];
        if (!tree) {
            result.modifiedFiles.push_back("");
            continue;
        }
        try {
            std::string content = slang::syntax::SyntaxPrinter::printFile(*tree);

            if (content.find("//BEGIN:") != 0) {
                size_t start = content.find_first_not_of(" \t\n\r");
                if (start != std::string::npos && content.substr(start, 8) != "//BEGIN:") {
                    std::string originalPath = (i < originalFilePaths.size()) ? originalFilePaths[i] : "unknown.sv";
                    content                  = fmt::format("//BEGIN:{}\n{}\n//END:{}", originalPath, content, originalPath);
                }
            }

            result.modifiedFiles.push_back(content);
        } catch (const std::exception &e) {
            result.errors.push_back(fmt::format("Error printing syntax tree {}: {}", i, e.what()));
            result.modifiedFiles.push_back("");
        } catch (...) {
            result.errors.push_back(fmt::format("Unknown error printing syntax tree {}", i));
            result.modifiedFiles.push_back("");
        }
    }

    //==========================================================================
    // Step 11: Write output files and cleanup
    //==========================================================================
    if (!actualOutputDir.empty()) {
        for (const auto &content : result.modifiedFiles) {
            slang_common::file_manage::generateNewFile(content, actualOutputDir);
        }
    }

    for (const auto &backupFile : backupFiles) {
        if (std::filesystem::exists(backupFile)) {
            std::filesystem::remove(backupFile);
        }
    }

    result.outputDir = actualOutputDir;

    std::cout << result.getSummary();

    //==========================================================================
    // Step 12: Check output files if requested
    //==========================================================================
    if (config.checkOutput) {
        std::cout << "\n==========================================";
        std::cout << "\nChecking output files...";
        std::cout << "\n==========================================";
        std::cout << std::endl;

        slang_common::Driver checkDriver("CheckDriver");
        checkDriver.addStandardArgs();

        // Apply driver options from config (include dirs, defines, etc.)
        for (const auto &dir : driverOpts.includeDirs) {
            checkDriver.driver.sourceManager.addUserDirectories(dir);
            checkDriver.getEmptySourceManager().addUserDirectories(dir);
        }
        for (const auto &dir : driverOpts.systemIncludeDirs) {
            checkDriver.driver.sourceManager.addSystemDirectories(dir);
            checkDriver.getEmptySourceManager().addSystemDirectories(dir);
        }
        for (const auto &def : driverOpts.defines) {
            checkDriver.driver.options.defines.push_back(def);
        }
        for (const auto &undef : driverOpts.undefines) {
            checkDriver.driver.options.undefines.push_back(undef);
        }
        for (const auto &dir : driverOpts.libDirs) {
            checkDriver.driver.sourceLoader.addSearchDirectories(dir);
        }
        for (const auto &ext : driverOpts.libExts) {
            checkDriver.driver.sourceLoader.addSearchExtension(ext);
        }

        // Add output files to check driver
        std::vector<std::string> outputFiles;
        for (const auto &inputFile : inputFiles) {
            std::filesystem::path inputPath(inputFile);
            std::filesystem::path outputPath = std::filesystem::path(actualOutputDir) / inputPath.filename();
            if (std::filesystem::exists(outputPath)) {
                outputFiles.push_back(outputPath.string());
            }
        }

        if (outputFiles.empty()) {
            result.errors.push_back("No output files found to check");
            return result;
        }

        // Add files and load sources
        checkDriver.addFiles(outputFiles);
        checkDriver.loadAllSources();

        // Set top module if available
        if (!result.usedTopModule.empty()) {
            checkDriver.driver.options.topModules.clear();
            checkDriver.driver.options.topModules.push_back(result.usedTopModule);
        } else if (!config.topModule.empty()) {
            checkDriver.driver.options.topModules.clear();
            checkDriver.driver.options.topModules.push_back(config.topModule);
        }

        // Process options
        if (!checkDriver.processOptions(false)) {
            result.errors.push_back("[checkDriver] Failed to process options");
            return result;
        }

        // Parse all sources
        if (!checkDriver.parseAllSources()) {
            result.errors.push_back("[checkDriver] Failed to parse source files");
            return result;
        }

        // Run full compilation
        auto checkCompilation = checkDriver.createCompilation();
        if (!checkCompilation) {
            result.errors.push_back("[checkDriver] Failed to compile");
            return result;
        }

        // Check for compilation errors
        auto &diags    = checkCompilation->getAllDiagnostics();
        bool hasErrors = false;
        for (auto &diag : diags) {
            if (diag.isError()) {
                hasErrors = true;
                break;
            }
        }

        if (hasErrors) {
            std::string errorMsg = slang::DiagnosticEngine::reportAll(checkDriver.driver.sourceManager, diags);
            result.errors.push_back(fmt::format("[checkDriver] Compilation errors:\\n{}", errorMsg));
            return result;
        }

        std::cout << "✓ Output files compiled successfully!" << std::endl;
    }

    return result;
}

} // namespace xmr
} // namespace slang_common
