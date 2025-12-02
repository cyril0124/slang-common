#pragma once

#include "SemanticModel.h"
#include "SlangCommon.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slang_common {
namespace xmr {

/**
 * @file XMREliminate.h
 * @brief XMR (Cross-Module Reference) Elimination utilities
 *
 * This module provides functionality similar to Chisel HDL's BoringUtils:
 *
 * 1. **XMR Elimination**: Identifies hierarchical references in SystemVerilog
 *    code and converts them to explicit port connections.
 *
 * 2. **Pipeline Registers**: Supports adding pipeline registers along signal
 *    propagation paths for timing closure.
 *
 * ## Usage Example:
 * @code
 * using namespace slang_common::xmr;
 *
 * // Configure XMR elimination
 * XMREliminateConfig config;
 * config.modules = {"top_module"};
 * config.clockName = "clk";
 * config.resetName = "rst_n";
 * config.resetActiveLow = true;
 *
 * // Optional: Add pipeline registers
 * config.pipeRegConfigMap["top_module"] = XMRPipeRegConfig::createGlobal(2);
 *
 * // Perform elimination
 * auto result = xmrEliminate({"file1.sv", "file2.sv"}, config);
 *
 * if (result.success()) {
 *     for (const auto& modifiedFile : result.modifiedFiles) {
 *         // Process modified file content
 *     }
 * }
 * @endcode
 */

/// @brief Pipeline register mode for XMR signals
enum class PipeRegMode {
    None,      ///< No pipeline registers
    Global,    ///< Add specified number of registers in the target module
    PerModule, ///< Add one register at each module boundary along the path
    Selective  ///< Add registers at specified module boundaries only
};

/// @brief Pipeline register configuration for a specific module or signal
struct PipeRegEntry {
    std::string moduleName;           ///< Module where registers should be added
    int regCount = 0;                 ///< Number of pipeline registers to add
    std::vector<std::string> signals; ///< Specific signals (empty means all signals in this module)
};

/**
 * @brief Pipeline register configuration structure
 *
 * Supports three modes:
 * 1. **Global**: Add N registers in the target module
 * 2. **PerModule**: Add 1 register at each module boundary
 * 3. **Selective**: Add registers only at specified modules/signals
 */
struct XMRPipeRegConfig {
    PipeRegMode mode   = PipeRegMode::None; ///< Pipeline register mode
    int globalRegCount = 0;                 ///< For Global mode: number of registers
    std::vector<PipeRegEntry> entries;      ///< For Selective mode: per-module configuration

    /// @brief Check if pipeline registers are enabled
    bool isEnabled() const { return mode != PipeRegMode::None; }

    /**
     * @brief Get register count for a specific module/signal
     * @param moduleName The module to check
     * @param signalName The signal to check (optional)
     * @return Number of registers to add (0 if none)
     */
    int getRegCountForModule(const std::string &moduleName, const std::string &signalName = "") const;

    /// @brief Create a global pipeline register configuration
    /// @param regCount Number of registers to add
    static XMRPipeRegConfig createGlobal(int regCount);

    /// @brief Create a per-module pipeline register configuration
    static XMRPipeRegConfig createPerModule();

    /// @brief Create a selective pipeline register configuration
    /// @param entries Configuration entries specifying where to add registers
    static XMRPipeRegConfig createSelective(const std::vector<PipeRegEntry> &entries);
};

/**
 * @brief Configuration for XMR elimination
 */
struct XMREliminateConfig {
    /// Modules to process for XMR elimination (empty = all modules with XMRs)
    std::vector<std::string> modules;

    /// Top module name (empty = auto-detect)
    std::string topModule;

    /// Per-module pipeline register configuration (key = module name)
    std::unordered_map<std::string, XMRPipeRegConfig> pipeRegConfigMap;

    /// Clock signal name (used for pipeline registers)
    std::string clockName = "clk";

    /// Reset signal name (used for pipeline registers)
    std::string resetName = "rst_n";

    /// Whether reset is active low (true) or active high (false)
    bool resetActiveLow = true;
};

/**
 * @brief Information about a detected XMR reference
 */
struct XMRInfo {
    std::string sourceModule;                              ///< Module containing the XMR reference
    std::string targetModule;                              ///< Module being referenced (where signal is defined)
    std::string targetSignal;                              ///< Signal being referenced
    std::string fullPath;                                  ///< Full hierarchical path (e.g., "u_inst.sig")
    std::vector<std::string> pathSegments;                 ///< Path segments from source to target
    bool isRead                                 = false;   ///< Whether this is a read access
    bool isWrite                                = false;   ///< Whether this is a write access
    bool isUpwardReference                      = false;   ///< Whether this is an upward reference (absolute path)
    int upwardCount                             = 0;       ///< Number of levels to go up for upward reference
    int bitWidth                                = 1;       ///< Bit width of the signal
    const slang::syntax::SyntaxNode *syntaxNode = nullptr; ///< Original syntax node

    /// @brief Get a unique identifier for this XMR
    std::string getUniqueId() const;

    /// @brief Get the generated port name for this XMR
    std::string getPortName() const;
};

/**
 * @brief Result of XMR elimination
 */
struct XMREliminateResult {
    std::vector<std::string> modifiedFiles;      ///< List of modified file contents
    std::vector<XMRInfo> eliminatedXMRs;         ///< List of eliminated XMRs
    std::vector<std::string> errors;             ///< Any errors encountered
    std::vector<std::string> warnings;           ///< Any warnings generated
    std::string outputDir;                       ///< Output directory where files were written
    std::vector<std::string> detectedTopModules; ///< List of detected top modules
    std::string usedTopModule;                   ///< The top module that was actually used

    /// @brief Check if elimination was successful (no errors)
    bool success() const { return errors.empty(); }

    /// @brief Get summary of XMR elimination
    /// @return Human-readable summary string
    std::string getSummary() const;
};

/**
 * @brief Main XMR elimination function
 *
 * Processes input files and eliminates XMR references by converting them
 * to explicit port connections. Optionally adds pipeline registers.
 *
 * Output files are written to the specified output directory (default: ".xmrEliminate").
 * This preserves the original files and writes modified versions to the output directory.
 *
 * @param inputFiles List of input SystemVerilog files
 * @param config Configuration for XMR elimination
 * @param outputDir Output directory for modified files (default: ".xmrEliminate")
 * @return Result containing modified files or errors
 *
 * @example
 * @code
 * XMREliminateConfig config;
 * config.modules = {"my_module"};
 * auto result = xmrEliminate({"design.sv"}, config, "output");
 * @endcode
 */
XMREliminateResult xmrEliminate(const std::vector<std::string> &inputFiles, const XMREliminateConfig &config, const std::string &outputDir = ".xmrEliminate");

/**
 * @brief Detect all XMR references in the given files
 *
 * Scans the input files for hierarchical references without modifying them.
 *
 * @param inputFiles List of input SystemVerilog files
 * @param targetModules Modules to scan for XMRs (empty = all modules)
 * @return List of detected XMR references
 */
std::vector<XMRInfo> detectXMRs(const std::vector<std::string> &inputFiles, const std::vector<std::string> &targetModules = {});

/**
 * @brief Analyze XMR path to find common ancestor and path segments
 *
 * @param compilation The compilation context
 * @param xmrExpr The XMR expression
 * @return XMRInfo with path information filled in
 */
XMRInfo analyzeXMRPath(slang::ast::Compilation &compilation, const slang::ast::HierarchicalValueExpression &xmrExpr);

/**
 * @brief Generate pipeline register code for a signal
 *
 * @param inputSignal Name of the input signal (source)
 * @param outputSignal Name of the output signal (destination, typically a port)
 * @param bitWidth Bit width of the signal
 * @param regCount Number of pipeline stages
 * @param clockName Clock signal name
 * @param resetName Reset signal name
 * @param resetActiveLow Whether reset is active low
 * @return Generated Verilog code for pipeline registers
 */
std::string generatePipelineRegisters(const std::string &inputSignal, const std::string &outputSignal, int bitWidth, int regCount, const std::string &clockName, const std::string &resetName,
                                      bool resetActiveLow);

/**
 * @brief Detect top-level modules in a compilation
 *
 * A top-level module is one that is not instantiated by any other module.
 *
 * @param compilation The compilation context
 * @return List of top module names
 */
std::vector<std::string> detectTopModules(slang::ast::Compilation &compilation);

} // namespace xmr
} // namespace slang_common
