/**
 * @file xmr_eliminate_main.cpp
 * @brief Command-line tool for XMR (Cross-Module Reference) Elimination
 *
 * This tool identifies hierarchical references in SystemVerilog code and
 * converts them to explicit port connections.
 *
 * Uses slang::driver::Driver pattern for argument parsing, consistent with
 * slang's command-line interface conventions.
 */

#include "SlangCommon.h"
#include "XMREliminate.h"
#include "slang/driver/Driver.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

/**
 * @brief Split a string by delimiter
 */
std::vector<std::string> splitString(const std::string &str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

/**
 * @class XMREliminatorCLI
 * @brief Command-line interface for XMR elimination tool
 *
 * This class encapsulates the command-line parsing and execution logic
 * for the XMR elimination tool, following the slang Driver pattern.
 */
class XMREliminatorCLI {
  private:
    slang::driver::Driver driver;

    // Command-line options
    std::optional<std::string> outputDir;
    std::optional<std::string> moduleList;
    std::optional<bool> verbose;

    std::optional<std::string> pipeRegMode;
    std::optional<int> pipeRegCount;
    std::optional<std::string> clockName;
    std::optional<std::string> resetName;
    std::optional<bool> resetActiveHigh;
    std::optional<bool> showHelp;
    std::optional<bool> checkOutput;
    std::optional<std::string> topModule;

    // Input files (collected from positional args)
    std::vector<std::string> inputFiles;

  public:
    XMREliminatorCLI() {
        // Add standard slang arguments (includes file handling, etc.)
        driver.addStandardArgs();

        // Add XMR elimination specific arguments
        driver.cmdLine.add("-o,--output", outputDir, "Output directory for modified files", "<dir>");
        driver.cmdLine.add("-m,--module", moduleList, "Target modules for XMR elimination (comma-separated)", "<modules>");
        driver.cmdLine.add("--verbose", verbose, "Enable verbose output");
        driver.cmdLine.add("--co,--check-output", checkOutput, "Check output");

        // Pipeline register options
        driver.cmdLine.add("--pipe-reg-mode", pipeRegMode, "Pipeline register mode: none|global|permodule|selective", "<mode>");
        driver.cmdLine.add("--pipe-reg-count", pipeRegCount, "Number of pipeline registers (for global mode)", "<n>");
        driver.cmdLine.add("--clock", clockName, "Clock signal name (default: clk)", "<name>");
        driver.cmdLine.add("--reset", resetName, "Reset signal name (default: rst_n)", "<name>");
        driver.cmdLine.add("--reset-active-high", resetActiveHigh, "Reset is active high (default: active low)");
        driver.cmdLine.add("-t,--top", topModule, "Top module name (auto-detected if not specified)", "<module>");
        driver.cmdLine.add("-h,--help", showHelp, "Show this help message");
    }

    int run(int argc, char **argv) {
        // Parse command line
        if (!driver.parseCommandLine(argc, argv)) {
            return 1;
        }

        // Handle help
        if (showHelp.value_or(false)) {
            printHelp();
            return 0;
        }

        // Process options (this validates files and paths)
        if (!driver.processOptions()) {
            return 1;
        }

        // Parse all sources to get the input files
        if (!driver.parseAllSources()) {
            std::cerr << "Error: Failed to parse source files" << std::endl;
            return 1;
        }

        // Check if we have input files
        if (driver.syntaxTrees.empty()) {
            std::cerr << "Error: No input files specified" << std::endl;
            std::cerr << "Use '-h' for help" << std::endl;
            return 1;
        }

        // Collect input files from syntax trees
        for (const auto &tree : driver.syntaxTrees) {
            auto path = tree->sourceManager().getFullPath(tree->root().getFirstToken().location().buffer());
            if (!path.empty()) {
                inputFiles.push_back(fs::absolute(std::string(path)).string());
            }
        }

        // Deduplicate input files
        std::sort(inputFiles.begin(), inputFiles.end());
        inputFiles.erase(std::unique(inputFiles.begin(), inputFiles.end()), inputFiles.end());

        // Parse module list
        std::vector<std::string> modules;
        if (moduleList.has_value()) {
            modules = splitString(moduleList.value(), ',');
        }

        bool isVerbose = verbose.value_or(false);

        if (isVerbose) {
            std::cout << "XMR Elimination Tool" << std::endl;
            std::cout << "====================" << std::endl;
            std::cout << "\nInput files:" << std::endl;
            for (const auto &file : inputFiles) {
                std::cout << "  " << file << std::endl;
            }
            if (!modules.empty()) {
                std::cout << "Target modules: ";
                for (const auto &mod : modules) {
                    std::cout << mod << " ";
                }
                std::cout << std::endl;
            }
            std::cout << "Output directory: " << outputDir.value_or(".xmrEliminate") << std::endl;
        }

        // Configure XMR elimination
        slang_common::xmr::XMREliminateConfig config;
        config.modules        = modules;                // May be empty, xmrEliminate will auto-detect all XMRs
        config.topModule      = topModule.value_or(""); // Empty means auto-detect
        config.clockName      = clockName.value_or("clk");
        config.resetName      = resetName.value_or("rst_n");
        config.resetActiveLow = !resetActiveHigh.value_or(false);

        // Check if output verification is requested
        auto checkOutputEnv = std::getenv("CHECK_OUTPUT");
        if (checkOutputEnv && std::string(checkOutputEnv) == "1") {
            config.checkOutput = true;
        }
        if (checkOutput.value_or(false)) {
            config.checkOutput = true;
        }

        // Configure pipeline registers
        std::string mode = pipeRegMode.value_or("none");
        int regCount     = pipeRegCount.value_or(0);

        if (mode == "global" && regCount > 0) {
            for (const auto &mod : modules) {
                config.pipeRegConfigMap[mod] = slang_common::xmr::XMRPipeRegConfig::createGlobal(regCount);
            }
        } else if (mode == "permodule") {
            for (const auto &mod : modules) {
                config.pipeRegConfigMap[mod] = slang_common::xmr::XMRPipeRegConfig::createPerModule();
            }
        }

        // Run XMR elimination
        if (isVerbose) {
            std::cout << "\nRunning XMR elimination..." << std::endl;
        }

        auto result = slang_common::xmr::xmrEliminate(inputFiles, config, outputDir.value_or(".xmrEliminate"));

        // Report results
        if (!result.success()) {
            std::cerr << "\nXMR elimination failed with errors:" << std::endl;
            for (const auto &error : result.errors) {
                std::cerr << "  Error: " << error << std::endl;
            }
            return 1;
        }

        // Print warnings
        for (const auto &warning : result.warnings) {
            std::cout << "Warning: " << warning << std::endl;
        }

        // Print summary
        std::cout << "\nXMR Elimination Summary:" << std::endl;
        std::cout << "  XMRs eliminated: " << result.eliminatedXMRs.size() << std::endl;
        std::cout << "  Output directory: " << result.outputDir << std::endl;

        if (isVerbose && !result.eliminatedXMRs.empty()) {
            std::cout << "\nEliminated XMRs:" << std::endl;
            for (const auto &xmr : result.eliminatedXMRs) {
                std::cout << "  - " << xmr.sourceModule << ": " << xmr.fullPath << " (width: " << xmr.bitWidth << ")" << std::endl;
            }
        }

        std::cout << "\n✓ XMR elimination completed successfully!" << std::endl;

        return 0;
    }

  private:
    void printHelp() {
        std::cout << "XMR (Cross-Module Reference) Elimination Tool\n"
                  << "==============================================\n\n"
                  << "Converts hierarchical references to explicit port connections.\n\n"
                  << "If -m is not specified, all detected XMRs will be eliminated.\n"
                  << "If -t is not specified, top module(s) will be auto-detected.\n\n"
                  << driver.cmdLine.getHelpText("xmr-eliminate") << "\nExamples:\n"
                  << "  xmr-eliminate design.sv -o output              # Auto-detect all XMRs\n"
                  << "  xmr-eliminate design.sv -o output -m top       # Only process 'top' module\n"
                  << "  xmr-eliminate design.sv -o output -t tb_top    # Specify top module\n"
                  << "  xmr-eliminate file1.sv file2.sv -m top,mid --pipe-reg-mode global --pipe-reg-count 2\n"
                  << "  xmr-eliminate *.sv -m top --clock sys_clk --reset sys_rst_n\n"
                  << std::endl;
    }
};

int main(int argc, char *argv[]) {
    XMREliminatorCLI cli;
    return cli.run(argc, argv);
}
