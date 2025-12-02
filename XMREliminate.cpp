#include "XMREliminate.h"
#include "fmt/core.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/syntax/SyntaxPrinter.h"
#include "slang/syntax/SyntaxVisitor.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <queue>
#include <regex>
#include <set>
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
TYPE 1: DOWNWARD XMR (Relative Path)
================================================================================

BEFORE (with XMR):
                   ┌─────────────────────────────────────┐
                   │            top_module               │
                   │                                     │
                   │  assign out = u_sub.data[3:0];      │  <-- XMR Reference
                   │               ↑                     │
                   │               │ (Hierarchical Ref)  │
                   │  ┌────────────┴───────────────┐     │
                   │  │        sub_module          │     │
                   │  │                            │     │
                   │  │  reg [7:0] data;           │     │
                   │  │                            │     │
                   │  └────────────────────────────┘     │
                   └─────────────────────────────────────┘

AFTER (with explicit ports):
                   ┌─────────────────────────────────────┐
                   │            top_module               │
                   │                                     │
                   │  wire [7:0] __xmr__u_sub_data;      │  <-- Added wire
                   │  assign out = __xmr__u_sub_data[3:0];│  <-- XMR replaced
                   │               ↑                     │
                   │               │ (Port connection)   │
                   │  ┌────────────┴───────────────┐     │
                   │  │        sub_module          │     │
                   │  │  output [7:0] __xmr__...   │     │  <-- Added output port
                   │  │                            │     │
                   │  │  reg [7:0] data;           │     │
                   │  │  assign __xmr__... = data; │     │  <-- Added assign
                   │  └────────────────────────────┘     │
                   └─────────────────────────────────────┘

MULTI-LEVEL HIERARCHY (Downward):
    top
     │  wire __xmr__u_mid_u_bottom_signal;
     │  u_mid(..., .__xmr__...(__xmr__...))
     │
     └── u_mid (mid_module)
          │  output __xmr__...;  <-- Pass-through port
          │  u_bottom(..., .__xmr__...(__xmr__...))
          │
          └── u_bottom (bottom_module)
               │  output __xmr__...;
               │  assign __xmr__... = signal;
               │
               └── signal

================================================================================
TYPE 2: UPWARD XMR (Absolute Path)
================================================================================

BEFORE (with XMR):
    ┌────────────────────────────────────────────────────┐
    │                      tb_top                         │
    │                                                     │
    │  ┌─────────────────┐    ┌────────────────────────┐ │
    │  │   uut (dut)     │    │  other_inst (others)   │ │
    │  │                 │    │                        │ │
    │  │  reg counter;   │    │  if (tb_top.uut.counter│ │ <-- Upward XMR
    │  │                 │    │      == 8'hFF)         │ │
    │  └─────────────────┘    └────────────────────────┘ │
    └────────────────────────────────────────────────────┘

AFTER (with explicit ports):
    ┌────────────────────────────────────────────────────┐
    │                      tb_top                         │
    │  wire __xmr__tb_top_uut_counter;                   │ <-- Added wire
    │                                                     │
    │  ┌─────────────────┐    ┌────────────────────────┐ │
    │  │   uut (dut)     │    │  other_inst (others)   │ │
    │  │                 │    │                        │ │
    │  │  output __xmr__ │───>│  input __xmr__...      │ │ <-- Ports added
    │  │  assign = ctr;  │    │  if (__xmr__... ==     │ │
    │  │                 │    │      8'hFF)            │ │
    │  └─────────────────┘    └────────────────────────┘ │
    │  assign __xmr__... = uut.__xmr__...;               │ <-- Connect wire
    └────────────────────────────────────────────────────┘

KEY DIFFERENCES:
  - Downward XMR: Source module is PARENT, needs wire. Target adds OUTPUT port.
  - Upward XMR: Source module is SIBLING/CHILD, adds INPUT port. Target adds OUTPUT port.
                Root module (common ancestor) adds wire + assign to connect them.

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
                  ┌───────────────────────────────┐
                  │   Detect Upward References    │
                  │   (HierarchicalReference.     │
                  │    upwardCount > 0)           │
                  └───────────────┬───────────────┘
                                  │
                                  ▼
  ┌────────────────┐     ┌─────────────────┐     ┌─────────────────────┐
  │  Output Files  │<────│  2-Pass Rewrite │<────│  Compute ChangeSet  │
  │  (Modified)    │     │  (Syntax Based) │     │  (Ports, Wires...)  │
  └────────────────┘     └─────────────────┘     └─────────────────────┘

  Pass 1: Module modifications (ports, assigns, XMR replacements)
  Pass 2: Instance connection updates

================================================================================
NON-ANSI PORT STYLE
================================================================================

For modules with no existing ports (e.g., `module m;`), we use non-ANSI style
to avoid formatting issues:

BEFORE:                          AFTER:
    module m;                        module m;
        ...                              input wire port1;
    endmodule                            output wire port2;
                                         ...
                                     endmodule

This approach inserts port declarations as member statements at the beginning
of the module body, preserving the original module header format.

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
    // Format: __xmr__<path_with_underscores>
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
// Helper functions
//==============================================================================

/// @brief Trim whitespace from string
static std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

/// @brief Check if a module has a specific port/signal
static bool hasSignal(const slang::ast::InstanceSymbol &inst, const std::string &signalName) {
    auto port = inst.body.findPort(signalName);
    if (port)
        return true;

    for (const auto &member : inst.body.members()) {
        if (member.kind == slang::ast::SymbolKind::Net || member.kind == slang::ast::SymbolKind::Variable) {
            if (std::string(member.name) == signalName) {
                return true;
            }
        }
    }
    return false;
}

/// @brief Generate port name for XMR at specific hierarchy level
/// @param fullPath The full XMR path (e.g., "u_mid.u_bottom.counter_value")
/// @return Port name with __xmr__ prefix (e.g., "__xmr__u_mid_u_bottom_counter_value")
static std::string generatePortName(const std::string &fullPath) {
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
// XMR Detection Visitor
//==============================================================================

/// @brief AST visitor to detect hierarchical (XMR) references
class XMRDetector : public slang::ast::ASTVisitor<XMRDetector, true, true> {
  public:
    std::vector<XMRInfo> detectedXMRs;
    std::unordered_set<std::string> targetModules;
    const slang::ast::InstanceSymbol *currentInstance = nullptr;
    slang::ast::Compilation &compilation;
    std::set<std::string> processedXMRs;

    XMRDetector(slang::ast::Compilation &comp, const std::vector<std::string> &modules) : compilation(comp) {
        for (const auto &m : modules) {
            targetModules.insert(m);
        }
    }

    void handle(const slang::ast::InstanceSymbol &inst) {
        auto prevInstance = currentInstance;
        currentInstance   = &inst;
        visitDefault(inst);
        currentInstance = prevInstance;
    }

    void handle(const slang::ast::HierarchicalValueExpression &expr) {
        if (!currentInstance) {
            return;
        }

        auto moduleName = std::string(currentInstance->getDefinition().name);
        if (!targetModules.empty() && targetModules.count(moduleName) == 0) {
            return;
        }

        XMRInfo info;
        info.sourceModule = moduleName;
        info.targetSignal = std::string(expr.symbol.name);

        // Get the bit width of the target symbol, not the expression type
        // This handles partial selects correctly (e.g., signal[0] still gets full signal width)
        if (auto *varSym = expr.symbol.as_if<slang::ast::VariableSymbol>()) {
            info.bitWidth = static_cast<int>(varSym->getType().getBitWidth());
        } else if (auto *netSym = expr.symbol.as_if<slang::ast::NetSymbol>()) {
            info.bitWidth = static_cast<int>(netSym->getType().getBitWidth());
        } else {
            info.bitWidth = static_cast<int>(expr.type->getBitWidth());
        }

        info.syntaxNode = expr.syntax;
        info.isRead     = true;

        // Check if this is an upward reference (absolute path starting from root)
        const auto &hierRef    = expr.ref;
        info.upwardCount       = static_cast<int>(hierRef.upwardCount);
        info.isUpwardReference = (hierRef.upwardCount > 0);

        // Build fullPath from the HierarchicalReference path
        // The path contains instance symbols, and the target signal is expr.symbol
        std::string pathStr;
        for (const auto &elem : hierRef.path) {
            // Only add instance symbols to the path
            if (elem.symbol->kind == slang::ast::SymbolKind::Instance) {
                if (!pathStr.empty()) {
                    pathStr += ".";
                }
                pathStr += std::string(elem.symbol->name);
                info.pathSegments.push_back(std::string(elem.symbol->name));
            }
        }
        // Add the target signal name to the full path
        if (!pathStr.empty()) {
            pathStr += ".";
        }
        pathStr += info.targetSignal;
        info.fullPath = pathStr;

        // Find target module - find the last Instance in the path (not the signal)
        if (!info.pathSegments.empty()) {
            // Find the last instance in the path
            for (auto it = hierRef.path.rbegin(); it != hierRef.path.rend(); ++it) {
                if (it->symbol->kind == slang::ast::SymbolKind::Instance) {
                    info.targetModule = std::string(it->symbol->as<slang::ast::InstanceSymbol>().getDefinition().name);
                    break;
                }
            }
        }

        // Avoid duplicates
        std::string uniqueKey = info.getUniqueId();
        if (processedXMRs.find(uniqueKey) == processedXMRs.end()) {
            processedXMRs.insert(uniqueKey);
            detectedXMRs.push_back(std::move(info));
        }
    }
};

//==============================================================================
// Hierarchical Port Propagation Data Structures
//==============================================================================

/// @brief Represents a port that needs to be added to a module
struct PortChange {
    std::string moduleName;     // Module to add port to
    std::string portName;       // Port name (e.g., __xmr__u_mid_u_bottom_counter_value)
    std::string direction;      // "input" or "output"
    int bitWidth;               // Bit width
    std::string signalToAssign; // For output ports in target module: signal to assign
};

/// @brief Represents a connection that needs to be updated in an instantiation
struct ConnectionChange {
    std::string parentModule;   // Module containing the instantiation
    std::string instanceName;   // Name of the instance being modified
    std::string instanceModule; // Module type of the instance
    std::string portName;       // Port name to connect
    std::string signalName;     // Signal to connect to the port
};

/// @brief Represents a wire declaration needed in a module
struct WireDecl {
    std::string moduleName;
    std::string wireName;
    int bitWidth;
};

/// @brief Represents a pipeline register to be added
struct PipeRegDecl {
    std::string moduleName;   // Module to add pipeline register to
    std::string inputSignal;  // Input signal name (source signal, e.g., counter_value)
    std::string outputSignal; // Output signal name (port name, e.g., __xmr__xxx)
    int bitWidth;             // Bit width
    int regCount;             // Number of pipeline stages
    std::string clockName;    // Clock signal name
    std::string resetName;    // Reset signal name
    bool resetActiveLow;      // Reset polarity
};

/// @brief Compute all changes needed for XMR elimination with hierarchical propagation
struct XMRChangeSet {
    std::unordered_map<std::string, std::vector<PortChange>> portsToAdd;     // Per module
    std::unordered_map<std::string, std::vector<std::string>> assignsToAdd;  // Per module
    std::unordered_map<std::string, std::vector<WireDecl>> wiresToAdd;       // Per module
    std::unordered_map<std::string, std::vector<PipeRegDecl>> pipeRegsToAdd; // Per module
    std::vector<ConnectionChange> connectionChanges;

    // For XMR replacement: maps (sourceModule, originalXMRPath) -> newSignalName
    std::map<std::pair<std::string, std::string>, std::string> xmrReplacements;
};

/// @brief Build a map from instance name to module definition name
class InstanceMapper : public slang::ast::ASTVisitor<InstanceMapper, true, true> {
  public:
    // Maps (parentModule, instanceName) -> instanceModuleName
    std::map<std::pair<std::string, std::string>, std::string> instanceMap;
    std::string currentModuleName;

    void handle(const slang::ast::InstanceSymbol &inst) {
        std::string defName = std::string(inst.getDefinition().name);

        // If this is a module instance (not root), record it
        if (!currentModuleName.empty()) {
            std::string instName                       = std::string(inst.name);
            instanceMap[{currentModuleName, instName}] = defName;
        }

        auto prevModule   = currentModuleName;
        currentModuleName = defName;
        visitDefault(inst);
        currentModuleName = prevModule;
    }
};

/// @brief Extract base path from XMR path (remove all array indices)
/// Example: "u_sub.arr[2][3]" -> "u_sub.arr"
static std::string extractBasePath(const std::string &fullPath) {
    std::string result;
    int bracketDepth = 0;
    for (char c : fullPath) {
        if (c == '[') {
            bracketDepth++;
        } else if (c == ']') {
            bracketDepth--;
        } else if (bracketDepth == 0) {
            result += c;
        }
    }
    return result;
}

/// @brief Extract all array suffixes from XMR path
/// Example: "u_sub.arr[2][3]" -> "[2][3]"
static std::string extractArraySuffix(const std::string &fullPath) {
    std::string suffix;
    int bracketDepth = 0;
    for (char c : fullPath) {
        if (c == '[') {
            bracketDepth++;
            suffix += c;
        } else if (c == ']') {
            suffix += c;
            bracketDepth--;
        } else if (bracketDepth > 0) {
            suffix += c;
        }
    }
    return suffix;
}

/*
 * ============================================================================
 * COMPUTE XMR CHANGES - CORE ALGORITHM
 * ============================================================================
 *
 * This function computes all necessary modifications to eliminate XMRs.
 *
 * TWO TYPES OF XMR REFERENCES:
 *
 * 1. DOWNWARD XMR (Relative Path) - e.g., top_module: u_mid.u_bottom.signal
 *    Signal flows UP through the hierarchy (output ports).
 *
 *     top_module (source)    -> Add wire, update instance connection
 *     u_mid (intermediate)   -> Add output port, update instance connection
 *     u_bottom (target)      -> Add output port, add assign statement
 *
 * 2. UPWARD XMR (Absolute Path) - e.g., others: tb_top.uut.counter
 *    Signal flows DOWN through the hierarchy (input ports to source).
 *
 *     tb_top (root/target)   -> Add wire, connect from uut and to other_inst
 *     dut (signal owner)     -> Add output port, add assign statement
 *     others (source)        -> Add input port
 *
 * ============================================================================
 */

/// @brief Find the instance path from root to a given module
class InstancePathFinder : public slang::ast::ASTVisitor<InstancePathFinder, true, true> {
  public:
    std::string targetModule;
    std::vector<std::string> currentPath;
    std::vector<std::vector<std::string>> foundPaths;

    InstancePathFinder(const std::string &target) : targetModule(target) {}

    void handle(const slang::ast::InstanceSymbol &inst) {
        std::string defName = std::string(inst.getDefinition().name);
        currentPath.push_back(std::string(inst.name));

        if (defName == targetModule) {
            foundPaths.push_back(currentPath);
        }

        visitDefault(inst);
        currentPath.pop_back();
    }
};

/// @brief Compute all changes needed for XMR elimination
static XMRChangeSet computeXMRChanges(const std::vector<XMRInfo> &xmrInfos, slang::ast::Compilation &compilation, const XMREliminateConfig &config) {
    XMRChangeSet changes;
    std::set<std::string> processedXMRs;      // Track processed XMRs by unique ID
    std::set<std::string> processedBasePaths; // Track processed base paths to avoid duplicate ports

    // Build instance map: (parentModule, instanceName) -> instanceModuleName
    InstanceMapper mapper;
    compilation.getRoot().visit(mapper);

    for (const auto &xmr : xmrInfos) {
        std::string xmrKey = xmr.getUniqueId();
        if (processedXMRs.count(xmrKey) > 0) {
            continue;
        }
        processedXMRs.insert(xmrKey);

        /*
         * Extract base path and array suffix from XMR path.
         * E.g., "u_sub.data[3]" -> basePath="u_sub.data", arraySuffix="[3]"
         */
        std::string basePath    = extractBasePath(xmr.fullPath);
        std::string arraySuffix = extractArraySuffix(xmr.fullPath);

        // Generate the port name for the XMR path (without array indices)
        std::string portName = generatePortName(basePath);

        // For XMR replacement: if there's an array suffix, append it
        std::string replacementName = arraySuffix.empty() ? portName : (portName + arraySuffix);

        // Store the XMR replacement mapping
        changes.xmrReplacements[{xmr.sourceModule, xmr.fullPath}] = replacementName;

        // Check if we've already processed this base path (for same source module)
        std::string basePathKey = xmr.sourceModule + "::" + basePath;
        if (processedBasePaths.count(basePathKey) > 0) {
            continue;
        }
        processedBasePaths.insert(basePathKey);

        // Skip if path segments are empty
        if (xmr.pathSegments.empty()) {
            continue;
        }

        /*
         * ==================================================================
         * UPWARD REFERENCE HANDLING (Absolute Path XMR)
         * ==================================================================
         *
         * For an upward reference like: others: tb_top.uut.counter
         *
         * Hierarchy:
         *   tb_top (root)
         *   ├── uut (dut instance) -> has 'counter' signal
         *   └── other_inst (others instance) -> references tb_top.uut.counter
         *
         * We need to:
         * 1. In 'dut' (target module): Add output port, assign counter to it
         * 2. In 'tb_top' (root): Add wire, connect uut's output to other_inst's input
         * 3. In 'others' (source): Add input port
         *
         * ==================================================================
         */
        if (xmr.isUpwardReference) {
            // For upward reference, first path segment is the root module instance name (usually same as module name)
            // Path: [tb_top, uut, ...] where tb_top is the root, uut is a child

            if (xmr.pathSegments.empty())
                continue;

            std::string rootModuleName = xmr.pathSegments[0]; // This is actually the instance name at root, usually same as module name

            // Find the module definition for the first path segment
            std::string firstModuleDef;
            // For root level, the instance name is the same as the module name in most cases
            // We need to find it from the mapper - look for any parent that has this instance
            for (const auto &[key, val] : mapper.instanceMap) {
                if (key.second == rootModuleName) {
                    firstModuleDef = val;
                    break;
                }
            }

            // If not found in mapper, assume instance name == module name (common for top level)
            if (firstModuleDef.empty()) {
                firstModuleDef = rootModuleName;
            }

            // Add input port in source module (the module that contains the XMR reference)
            {
                PortChange inPort;
                inPort.moduleName = xmr.sourceModule;
                inPort.portName   = portName;
                inPort.direction  = "input";
                inPort.bitWidth   = xmr.bitWidth;
                changes.portsToAdd[xmr.sourceModule].push_back(inPort);
            }

            // Find the path from root to the source module instance
            InstancePathFinder pathFinder(xmr.sourceModule);
            compilation.getRoot().visit(pathFinder);

            if (!pathFinder.foundPaths.empty()) {
                // Use the first path found (usually there's only one)
                const auto &sourceInstPath = pathFinder.foundPaths[0];

                // Find the common parent between source module path and XMR target path
                // For tb_top.uut.counter referenced from others:
                // - Source path: [tb_top, other_inst]
                // - XMR path: [tb_top, uut]
                // - Common parent: tb_top

                // The root module needs to:
                // 1. Add a wire for the signal
                // 2. Connect the signal from the target instance to the source instance

                if (sourceInstPath.size() >= 2) {
                    std::string parentModuleName = firstModuleDef;
                    std::string sourceInstName   = sourceInstPath.back();

                    // Check that parent has both the XMR target and source as children
                    // Add wire in the parent module (the common ancestor)
                    {
                        WireDecl wire;
                        wire.moduleName = parentModuleName;
                        wire.wireName   = portName;
                        wire.bitWidth   = xmr.bitWidth;
                        changes.wiresToAdd[parentModuleName].push_back(wire);
                    }

                    // Add connection from source instance (input)
                    {
                        ConnectionChange conn;
                        conn.parentModule   = parentModuleName;
                        conn.instanceName   = sourceInstName;
                        conn.instanceModule = xmr.sourceModule;
                        conn.portName       = portName;
                        conn.signalName     = portName;
                        changes.connectionChanges.push_back(conn);
                    }
                }
            }

            // Now trace the XMR path to add output ports along the way
            // Skip the first segment (root module) and trace from there
            std::string currentModule = firstModuleDef;
            for (size_t i = 1; i < xmr.pathSegments.size(); i++) {
                const std::string &instName = xmr.pathSegments[i];

                auto it = mapper.instanceMap.find({currentModule, instName});
                if (it == mapper.instanceMap.end()) {
                    break;
                }
                std::string instModuleName = it->second;

                // Add connection from parent to child instance
                ConnectionChange conn;
                conn.parentModule   = currentModule;
                conn.instanceName   = instName;
                conn.instanceModule = instModuleName;
                conn.portName       = portName;
                conn.signalName     = portName;
                changes.connectionChanges.push_back(conn);

                // Add output port to pass the signal through (or at final target)
                PortChange outPort;
                outPort.moduleName = instModuleName;
                outPort.portName   = portName;
                outPort.direction  = "output";
                outPort.bitWidth   = xmr.bitWidth;
                changes.portsToAdd[instModuleName].push_back(outPort);

                currentModule = instModuleName;
            }

            // Add assign in target module
            if (!xmr.targetModule.empty()) {
                changes.assignsToAdd[xmr.targetModule].push_back(fmt::format("assign {} = {};", portName, xmr.targetSignal));
            }

            continue; // Skip the normal (downward) processing
        }

        /*
         * ==================================================================
         * DOWNWARD REFERENCE HANDLING (Relative Path XMR)
         * ==================================================================
         *
         * For a downward reference like: top_module: u_mid.u_bottom.signal
         *
         * We need to:
         * 1. In 'top_module' (source): Add wire
         * 2. In 'u_mid' (intermediate): Add output port
         * 3. In 'u_bottom' (target): Add output port, assign signal to it
         *
         * ==================================================================
         */

        // Add wire declaration in source module
        {
            WireDecl wire;
            wire.moduleName = xmr.sourceModule;
            wire.wireName   = portName;
            wire.bitWidth   = xmr.bitWidth;
            changes.wiresToAdd[xmr.sourceModule].push_back(wire);
        }

        // Walk the hierarchy from source to target
        std::string currentModule = xmr.sourceModule;

        for (size_t i = 0; i < xmr.pathSegments.size(); i++) {
            const std::string &instName = xmr.pathSegments[i];

            // Find what module type this instance is
            auto it = mapper.instanceMap.find({currentModule, instName});
            if (it == mapper.instanceMap.end()) {
                // Can't find instance mapping, skip remaining path
                break;
            }
            std::string instModuleName = it->second;

            // Add connection from parent to child instance
            ConnectionChange conn;
            conn.parentModule   = currentModule;
            conn.instanceName   = instName;
            conn.instanceModule = instModuleName;
            conn.portName       = portName;
            conn.signalName     = portName;
            changes.connectionChanges.push_back(conn);

            // For intermediate modules (not the final target):
            // Add output port to pass the signal through
            if (i < xmr.pathSegments.size() - 1) {
                PortChange outPort;
                outPort.moduleName = instModuleName;
                outPort.portName   = portName;
                outPort.direction  = "output";
                outPort.bitWidth   = xmr.bitWidth;
                changes.portsToAdd[instModuleName].push_back(outPort);
            }

            currentModule = instModuleName;
        }

        // Check for pipeline register configuration
        bool hasPipelineRegs = false;
        auto pipeConfigIt    = config.pipeRegConfigMap.find(xmr.sourceModule);
        if (pipeConfigIt != config.pipeRegConfigMap.end()) {
            hasPipelineRegs = pipeConfigIt->second.isEnabled();
        }

        // Add output port and assign in target module
        if (!xmr.targetModule.empty()) {
            PortChange tgtPort;
            tgtPort.moduleName     = xmr.targetModule;
            tgtPort.portName       = portName;
            tgtPort.direction      = "output";
            tgtPort.bitWidth       = xmr.bitWidth;
            tgtPort.signalToAssign = xmr.targetSignal;
            changes.portsToAdd[xmr.targetModule].push_back(tgtPort);

            // Add assign statement only if there are no pipeline registers
            if (!hasPipelineRegs) {
                changes.assignsToAdd[xmr.targetModule].push_back(fmt::format("assign {} = {};", portName, xmr.targetSignal));
            }
        }

        // Add pipeline registers based on configuration
        if (pipeConfigIt != config.pipeRegConfigMap.end()) {
            const XMRPipeRegConfig &pipeConfig = pipeConfigIt->second;

            if (pipeConfig.mode == PipeRegMode::Global && pipeConfig.globalRegCount > 0) {
                PipeRegDecl pipeReg;
                pipeReg.moduleName     = xmr.targetModule;
                pipeReg.inputSignal    = xmr.targetSignal;
                pipeReg.outputSignal   = portName;
                pipeReg.bitWidth       = xmr.bitWidth;
                pipeReg.regCount       = pipeConfig.globalRegCount;
                pipeReg.clockName      = config.clockName;
                pipeReg.resetName      = config.resetName;
                pipeReg.resetActiveLow = config.resetActiveLow;
                changes.pipeRegsToAdd[xmr.targetModule].push_back(pipeReg);
            } else if (pipeConfig.mode == PipeRegMode::PerModule) {
                PipeRegDecl pipeReg;
                pipeReg.moduleName     = xmr.targetModule;
                pipeReg.inputSignal    = xmr.targetSignal;
                pipeReg.outputSignal   = portName;
                pipeReg.bitWidth       = xmr.bitWidth;
                pipeReg.regCount       = static_cast<int>(xmr.pathSegments.size());
                pipeReg.clockName      = config.clockName;
                pipeReg.resetName      = config.resetName;
                pipeReg.resetActiveLow = config.resetActiveLow;
                changes.pipeRegsToAdd[xmr.targetModule].push_back(pipeReg);
            } else if (pipeConfig.mode == PipeRegMode::Selective) {
                int totalRegs = 0;
                for (const auto &entry : pipeConfig.entries) {
                    if (entry.regCount <= 0)
                        continue;

                    bool matchesSignal = entry.signals.empty();
                    if (!matchesSignal) {
                        for (const auto &sig : entry.signals) {
                            if (sig == portName || sig == xmr.targetSignal) {
                                matchesSignal = true;
                                break;
                            }
                        }
                    }

                    if (matchesSignal) {
                        totalRegs += entry.regCount;
                    }
                }

                if (totalRegs > 0) {
                    PipeRegDecl pipeReg;
                    pipeReg.moduleName     = xmr.targetModule;
                    pipeReg.inputSignal    = xmr.targetSignal;
                    pipeReg.outputSignal   = portName;
                    pipeReg.bitWidth       = xmr.bitWidth;
                    pipeReg.regCount       = totalRegs;
                    pipeReg.clockName      = config.clockName;
                    pipeReg.resetName      = config.resetName;
                    pipeReg.resetActiveLow = config.resetActiveLow;
                    changes.pipeRegsToAdd[xmr.targetModule].push_back(pipeReg);
                }
            }
        }
    }

    // Deduplicate all changes
    for (auto &[mod, ports] : changes.portsToAdd) {
        std::set<std::string> seen;
        std::vector<PortChange> unique;
        for (const auto &p : ports) {
            if (seen.insert(p.portName + p.direction).second) {
                unique.push_back(p);
            }
        }
        ports = std::move(unique);
    }

    for (auto &[mod, wires] : changes.wiresToAdd) {
        std::set<std::string> seen;
        std::vector<WireDecl> unique;
        for (const auto &w : wires) {
            if (seen.insert(w.wireName).second) {
                unique.push_back(w);
            }
        }
        wires = std::move(unique);
    }

    // Deduplicate connection changes
    {
        std::set<std::string> seen;
        std::vector<ConnectionChange> unique;
        for (const auto &c : changes.connectionChanges) {
            std::string key = c.parentModule + "." + c.instanceName + "." + c.portName;
            if (seen.insert(key).second) {
                unique.push_back(c);
            }
        }
        changes.connectionChanges = std::move(unique);
    }

    // Deduplicate pipeline registers
    for (auto &[mod, pipeRegs] : changes.pipeRegsToAdd) {
        std::set<std::string> seen;
        std::vector<PipeRegDecl> unique;
        for (const auto &p : pipeRegs) {
            if (seen.insert(p.outputSignal).second) {
                unique.push_back(p);
            }
        }
        pipeRegs = std::move(unique);
    }

    return changes;
}

//==============================================================================
// XMR Rewriter - First Pass (Module modifications: ports, assigns, XMR replacements)
//==============================================================================

class XMRRewriterFirst : public slang::syntax::SyntaxRewriter<XMRRewriterFirst> {
  public:
    const XMRChangeSet &changes;
    const XMREliminateConfig &config;
    std::string currentModuleName;
    std::set<std::string> addedPorts;

    XMRRewriterFirst(const XMRChangeSet &c, const XMREliminateConfig &cfg) : changes(c), config(cfg) {}

    void handle(const slang::syntax::ModuleDeclarationSyntax &syntax) {
        currentModuleName = std::string(syntax.header->name.rawText());

        auto portsIt   = changes.portsToAdd.find(currentModuleName);
        auto assignsIt = changes.assignsToAdd.find(currentModuleName);
        auto wiresIt   = changes.wiresToAdd.find(currentModuleName);

        bool hasPorts   = (portsIt != changes.portsToAdd.end() && !portsIt->second.empty());
        bool hasAssigns = (assignsIt != changes.assignsToAdd.end() && !assignsIt->second.empty());
        bool hasWires   = (wiresIt != changes.wiresToAdd.end() && !wiresIt->second.empty());

        if (hasPorts || hasAssigns || hasWires) {
            // Add wire declarations at the beginning of the module
            if (hasWires) {
                for (const auto &wire : wiresIt->second) {
                    // Only add if this wire is not also a port (avoid duplicate declarations)
                    bool isPort = false;
                    if (hasPorts) {
                        for (const auto &p : portsIt->second) {
                            if (p.portName == wire.wireName) {
                                isPort = true;
                                break;
                            }
                        }
                    }
                    if (!isPort) {
                        std::string widthSpec = (wire.bitWidth > 1) ? fmt::format("[{}:0] ", wire.bitWidth - 1) : "";
                        insertAtFront(syntax.members, parse(fmt::format("\n    wire {}{};", widthSpec, wire.wireName)));
                    }
                }
            }

            // Add ports
            if (hasPorts) {
                if (syntax.header->ports && syntax.header->ports->kind == slang::syntax::SyntaxKind::AnsiPortList) {
                    // ANSI port list - append to existing ports
                    auto &ansiPorts = syntax.header->ports->as<slang::syntax::AnsiPortListSyntax>();

                    for (const auto &port : portsIt->second) {
                        std::string widthSpec = (port.bitWidth > 1) ? fmt::format("[{}:0] ", port.bitWidth - 1) : "";
                        std::string portDecl  = fmt::format(",\n    {} wire {}{}", port.direction, widthSpec, port.portName);
                        insertAtBack(ansiPorts.ports, parse(portDecl));
                        addedPorts.insert(port.portName);
                    }
                } else if (!syntax.header->ports) {
                    // No port list at all (module m;)
                    // Use non-ANSI style: add port declarations as members inside the module body
                    // This preserves the module header structure and avoids newline issues
                    for (const auto &port : portsIt->second) {
                        std::string widthSpec = (port.bitWidth > 1) ? fmt::format("[{}:0] ", port.bitWidth - 1) : "";
                        insertAtFront(syntax.members, parse(fmt::format("\n    {} wire {}{};", port.direction, widthSpec, port.portName)));
                        addedPorts.insert(port.portName);
                    }
                } else {
                    // Non-ANSI port list (module m(a, b); input a; output b;)
                    // Add port declarations inside the module body
                    for (const auto &port : portsIt->second) {
                        std::string widthSpec = (port.bitWidth > 1) ? fmt::format("[{}:0] ", port.bitWidth - 1) : "";
                        insertAtFront(syntax.members, parse(fmt::format("\n    {} wire {}{};", port.direction, widthSpec, port.portName)));
                        addedPorts.insert(port.portName);
                    }
                }
            }

            // Add assigns at the end of the module (before endmodule)
            if (hasAssigns) {
                for (const auto &assign : assignsIt->second) {
                    insertAtBack(syntax.members, parse(fmt::format("\n    {}", assign)));
                }
            }

            // Add pipeline registers
            auto pipeRegsIt = changes.pipeRegsToAdd.find(currentModuleName);
            if (pipeRegsIt != changes.pipeRegsToAdd.end() && !pipeRegsIt->second.empty()) {
                for (const auto &pipeReg : pipeRegsIt->second) {
                    std::string pipeRegCode =
                        generatePipelineRegisters(pipeReg.inputSignal, pipeReg.outputSignal, pipeReg.bitWidth, pipeReg.regCount, pipeReg.clockName, pipeReg.resetName, pipeReg.resetActiveLow);
                    if (!pipeRegCode.empty()) {
                        insertAtBack(syntax.members, parse(fmt::format("\n{}", pipeRegCode)));
                    }
                }
            }
        }

        visitDefault(syntax);
    }

    /// @brief Handle XMR replacement in expressions
    void handle(const slang::syntax::ScopedNameSyntax &syntax) {
        std::string fullName = trim(syntax.toString());

        // Try exact match first
        auto it = changes.xmrReplacements.find({currentModuleName, fullName});
        if (it != changes.xmrReplacements.end()) {
            // Add a leading space to preserve token separation
            replace(syntax, parse(" " + it->second));
            return;
        }

        // Try base path match (for handling array indices)
        std::string basePath    = extractBasePath(fullName);
        std::string arraySuffix = extractArraySuffix(fullName);

        it = changes.xmrReplacements.find({currentModuleName, basePath});
        if (it != changes.xmrReplacements.end()) {
            std::string replacement      = it->second;
            std::string replacementBase  = extractBasePath(replacement);
            std::string finalReplacement = arraySuffix.empty() ? replacementBase : (replacementBase + arraySuffix);
            // Add a leading space to preserve token separation
            replace(syntax, parse(" " + finalReplacement));
            return;
        }

        visitDefault(syntax);
    }
};

//==============================================================================
// XMR Rewriter - Second Pass (Instance connection updates)
//==============================================================================

class XMRRewriterSecond : public slang::syntax::SyntaxRewriter<XMRRewriterSecond> {
  public:
    const XMRChangeSet &changes;
    std::string currentModuleName;
    std::set<std::string> processedConnections;

    XMRRewriterSecond(const XMRChangeSet &c) : changes(c) {}

    void handle(const slang::syntax::ModuleDeclarationSyntax &syntax) {
        currentModuleName = std::string(syntax.header->name.rawText());
        visitDefault(syntax);
    }

    void handle(const slang::syntax::HierarchyInstantiationSyntax &syntax) {
        std::string instModuleName = std::string(syntax.type.rawText());

        // Find connections for instances of this module type in current module
        std::map<std::string, std::vector<const ConnectionChange *>> instanceConnections;
        for (const auto &conn : changes.connectionChanges) {
            if (conn.parentModule == currentModuleName && conn.instanceModule == instModuleName) {
                instanceConnections[conn.instanceName].push_back(&conn);
            }
        }

        if (instanceConnections.empty()) {
            visitDefault(syntax);
            return;
        }

        // Process each instance
        for (size_t i = 0; i < syntax.instances.size(); i++) {
            auto &inst = *syntax.instances[i];
            if (!inst.decl)
                continue;

            std::string thisInstName = std::string(inst.decl->name.rawText());
            auto connIt              = instanceConnections.find(thisInstName);
            if (connIt == instanceConnections.end())
                continue;

            // Track whether we've added any connection in this loop iteration
            // so subsequent connections in the same loop need comma prefix
            bool addedAnyConnection     = false;
            bool hasExistingConnections = !inst.connections.empty();

            for (const auto *conn : connIt->second) {
                std::string connKey = currentModuleName + "." + thisInstName + "." + conn->portName;
                if (processedConnections.count(connKey) > 0)
                    continue;
                processedConnections.insert(connKey);

                // Add port connection
                // Need comma if: has existing connections OR we've already added one in this loop
                bool needComma = hasExistingConnections || addedAnyConnection;
                if (needComma) {
                    std::string newConn = fmt::format(",\n        .{}({})", conn->portName, conn->signalName);
                    insertAtBack(inst.connections, parse(newConn));
                } else {
                    // First connection when no existing connections
                    std::string newConn = fmt::format("\n        .{}({})", conn->portName, conn->signalName);
                    insertAtBack(inst.connections, parse(newConn));
                }
                addedAnyConnection = true;
            }
        }

        visitDefault(syntax);
    }
};

//==============================================================================
// Clock/Reset Signal Verification
//==============================================================================

struct ClockResetVerifier : public slang::ast::ASTVisitor<ClockResetVerifier, true, true> {
    const std::string &clockName;
    const std::string &resetName;
    std::set<std::string> modulesWithClock;
    std::set<std::string> modulesWithReset;
    std::set<std::string> allModules;

    ClockResetVerifier(const std::string &clk, const std::string &rst) : clockName(clk), resetName(rst) {}

    void handle(const slang::ast::InstanceSymbol &inst) {
        std::string moduleName = std::string(inst.getDefinition().name);
        allModules.insert(moduleName);

        if (hasSignal(inst, clockName)) {
            modulesWithClock.insert(moduleName);
        }
        if (hasSignal(inst, resetName)) {
            modulesWithReset.insert(moduleName);
        }

        visitDefault(inst);
    }
};

//==============================================================================
// Top Module Detection
//==============================================================================

/// @brief Visitor to collect all module definitions and their instantiations
class TopModuleDetector : public slang::ast::ASTVisitor<TopModuleDetector, true, true> {
  public:
    std::set<std::string> allModules;          // All module definitions
    std::set<std::string> instantiatedModules; // Modules that are instantiated by others
    std::string currentModuleName;

    void handle(const slang::ast::InstanceSymbol &inst) {
        std::string defName = std::string(inst.getDefinition().name);

        // Track this as a module definition
        allModules.insert(defName);

        // If we're inside another module, this module is being instantiated
        if (!currentModuleName.empty() && defName != currentModuleName) {
            instantiatedModules.insert(defName);
        }

        auto prevModule   = currentModuleName;
        currentModuleName = defName;
        visitDefault(inst);
        currentModuleName = prevModule;
    }
};

std::vector<std::string> detectTopModules(slang::ast::Compilation &compilation) {
    TopModuleDetector detector;
    compilation.getRoot().visit(detector);

    std::vector<std::string> topModules;
    for (const auto &mod : detector.allModules) {
        if (detector.instantiatedModules.count(mod) == 0) {
            topModules.push_back(mod);
        }
    }

    // Sort for consistent ordering
    std::sort(topModules.begin(), topModules.end());
    return topModules;
}

//==============================================================================
// Pipeline Register Generator
//==============================================================================

/*
 * Pipeline Register Generation Diagram:
 * =====================================
 *
 * For a signal with N pipeline stages:
 *
 *   input_sig ──┬──> pipe_0 ──> pipe_1 ──> ... ──> pipe_{N-1} ──> output_port
 *               │       │          │                    │
 *               │       ▼          ▼                    ▼
 *               │    always @(posedge clk)  ...     assign
 *               │
 *   (source)    └──────────────────────────────────────────────> (destination)
 *
 * Generated code structure:
 *   1. Register declarations for each stage
 *   2. Always block with clock and reset
 *   3. Assignment from last stage to output port
 */

std::string generatePipelineRegisters(const std::string &inputSignal, const std::string &outputSignal, int bitWidth, int regCount, const std::string &clockName, const std::string &resetName,
                                      bool resetActiveLow) {
    if (regCount <= 0) {
        return "";
    }

    std::stringstream ss;

    std::string widthSpec = (bitWidth > 1) ? fmt::format("[{}:0] ", bitWidth - 1) : "";
    std::string resetCond = resetActiveLow ? fmt::format("!{}", resetName) : resetName;

    // Use output signal name as base for pipeline registers
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

    // Pipeline chain: input signal -> pipe_0 -> pipe_1 -> ...
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
// Internal XMR Detection (uses existing compilation)
//==============================================================================

/// @brief Detect XMRs using an existing compilation (no new Driver creation)
static std::vector<XMRInfo> detectXMRsFromCompilation(slang::ast::Compilation &compilation, const std::vector<std::string> &targetModules) {
    XMRDetector detector(compilation, targetModules);
    compilation.getRoot().visit(detector);
    return detector.detectedXMRs;
}

/// @brief Verify clock/reset signals in modules using existing compilation
static std::vector<std::string> verifyClockResetSignals(slang::ast::Compilation &compilation, const XMREliminateConfig &config) {
    std::vector<std::string> errors;

    ClockResetVerifier verifier(config.clockName, config.resetName);
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
// Main implementation
//==============================================================================

/*
 * XMR Elimination Main Entry Point
 * =================================
 *
 * Optimized Pipeline (Single Driver):
 *
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │                          ONE DRIVER                                 │
 *   │                                                                     │
 *   │  Input Files ──> Load ──> Parse ──> Compile ──┬──> Detect XMRs     │
 *   │                                               │                     │
 *   │                                               ├──> Verify Clk/Rst  │
 *   │                                               │                     │
 *   │                                               └──> Compute Changes │
 *   │                                                                     │
 *   │  Syntax Trees ──> Rewrite Pass 1 ──> Rewrite Pass 2 ──> Output     │
 *   │                                                                     │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * Previous Implementation (3 Drivers - SLOW):
 *   - Driver 1: detectXMRs()
 *   - Driver 2: verifyClockResetSignals()
 *   - Driver 3: actual rewriting
 *
 * Current Implementation (1 Driver - FAST):
 *   - Single Driver handles everything
 *   - Compilation done once, reused for all operations
 */

std::vector<XMRInfo> detectXMRs(const std::vector<std::string> &inputFiles, const std::vector<std::string> &targetModules) {
    // Check if all input files exist first
    for (const auto &file : inputFiles) {
        if (!std::filesystem::exists(file)) {
            return {};
        }
    }

    // Create a compilation from input files
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

    // Use internal detection function
    return detectXMRsFromCompilation(*compilation, targetModules);
}

XMRInfo analyzeXMRPath(slang::ast::Compilation &compilation, const slang::ast::HierarchicalValueExpression &xmrExpr) {
    XMRInfo info;

    info.targetSignal = std::string(xmrExpr.symbol.name);
    info.bitWidth     = static_cast<int>(xmrExpr.type->getBitWidth());

    if (xmrExpr.syntax) {
        info.fullPath = trim(xmrExpr.syntax->toString());
    }

    // Build path segments
    const auto &hierRef = xmrExpr.ref;
    for (const auto &elem : hierRef.path) {
        info.pathSegments.push_back(std::string(elem.symbol->name));
    }

    return info;
}

XMREliminateResult xmrEliminate(const std::vector<std::string> &inputFiles, const XMREliminateConfig &config, const std::string &outputDir) {
    /*
     * =========================================================================
     * XMR ELIMINATION WORKFLOW
     * =========================================================================
     *
     * Step 1: Validate inputs
     *         ↓
     * Step 2: Create single Driver and compile
     *         ↓
     * Step 3: Detect XMRs using compiled AST
     *         ↓
     * Step 4: Verify clock/reset signals (if pipeline registers needed)
     *         ↓
     * Step 5: Compute all changes (ports, wires, assigns, connections)
     *         ↓
     * Step 6: Rewrite Pass 1 - Module modifications
     *         ↓
     * Step 7: Rewrite Pass 2 - Instance connections
     *         ↓
     * Step 8: Generate output files
     *
     * =========================================================================
     */

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
    std::string actualOutputDir = outputDir;
    if (actualOutputDir.empty()) {
        actualOutputDir = ".xmrEliminate";
    }

    std::string workDir = actualOutputDir + "/.work";
    if (!std::filesystem::exists(workDir)) {
        std::filesystem::create_directories(workDir);
    }

    //==========================================================================
    // Step 3: Create SINGLE Driver for all operations (PERFORMANCE OPTIMIZATION)
    //==========================================================================
    /*
     * IMPORTANT: Previously, we created 3 separate Drivers:
     *   1. detectXMRs() - created its own Driver
     *   2. verifyClockResetSignals() - created another Driver
     *   3. xmrEliminate() - created yet another Driver
     *
     * This was slow because each Driver compiles all files from scratch.
     * Now we use ONE Driver and share the compilation for all operations.
     */
    slang_common::Driver driver("XMREliminator");
    driver.addStandardArgs();

    std::vector<std::string> backupFiles;

    driver.addFiles(const_cast<std::vector<std::string> &>(inputFiles));
    driver.loadAllSources([&workDir, &backupFiles](std::string_view file) {
        auto newFile = slang_common::file_manage::backupFile(file, workDir);
        auto f       = std::filesystem::absolute(newFile).string();
        backupFiles.push_back(f);
        return f;
    });

    driver.processOptions(false); // Use multi-unit mode to get separate syntax trees per file
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

    // Print detected top modules
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

    // Determine which top module to use
    if (!config.topModule.empty()) {
        // User specified top module
        result.usedTopModule = config.topModule;
        std::cout << "[XMR Eliminate] Using user-specified top module: " << config.topModule << std::endl;
    } else if (result.detectedTopModules.size() == 1) {
        // Single top module detected, use it
        result.usedTopModule = result.detectedTopModules[0];
        std::cout << "[XMR Eliminate] Using auto-detected top module: " << result.usedTopModule << std::endl;
    } else if (result.detectedTopModules.size() > 1) {
        // Multiple top modules detected
        result.warnings.push_back(fmt::format("Multiple top modules detected ({}). Use -t to specify one.", fmt::join(result.detectedTopModules, ", ")));
        std::cout << "[XMR Eliminate] Warning: Multiple top modules detected. Processing all modules with XMRs." << std::endl;
    }

    //==========================================================================
    // Step 5: Detect XMRs using the shared compilation
    //==========================================================================
    // If no modules specified, detect XMRs in all modules
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
        // Return original files unchanged
        for (const auto &file : inputFiles) {
            std::ifstream ifs(file);
            if (ifs) {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                result.modifiedFiles.push_back(buffer.str());
            }
        }
        // Clean up backup files
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
        // Use the SAME compilation to verify clock/reset signals (no new Driver!)
        auto clockResetErrors = verifyClockResetSignals(*compilation, config);
        if (!clockResetErrors.empty()) {
            result.errors = std::move(clockResetErrors);
            // Clean up backup files before returning
            for (const auto &backupFile : backupFiles) {
                if (std::filesystem::exists(backupFile)) {
                    std::filesystem::remove(backupFile);
                }
            }
            return result;
        }
    }

    //==========================================================================
    // Step 7: Compute all changes needed (using the shared compilation)
    //==========================================================================
    XMRChangeSet changeSet = computeXMRChanges(xmrInfos, *compilation, config);

    //==========================================================================
    // Step 8: Rewrite Pass 1 - Module modifications
    //==========================================================================
    /*
     * Pass 1 handles:
     *   - Adding wire declarations in source modules
     *   - Adding output ports in intermediate/target modules
     *   - Adding assign statements
     *   - Replacing XMR expressions with port names
     *   - Adding pipeline registers if configured
     */

    // Keep rewriters alive to preserve their BumpAllocator memory
    std::vector<std::unique_ptr<XMRRewriterFirst>> firstRewriters;
    std::vector<std::unique_ptr<XMRRewriterSecond>> secondRewriters;

    // Keep all intermediate trees alive - transformed trees may reference data from originals
    // See slang SyntaxRewriter note: "If you shallow clone something from old tree into
    // new one, you have to make sure that shared_ptr to original doesn't go out of scope"
    std::vector<std::shared_ptr<slang::syntax::SyntaxTree>> allTrees;
    allTrees.reserve(driver.driver.syntaxTrees.size() * 3); // Original + first pass + second pass
    for (auto &tree : driver.driver.syntaxTrees) {
        allTrees.push_back(tree);
    }

    std::shared_ptr<slang::syntax::SyntaxTree> newTree;
    for (size_t i = 0; i < driver.driver.syntaxTrees.size(); i++) {
        auto &tree = driver.driver.syntaxTrees[i];

        auto rewriter = std::make_unique<XMRRewriterFirst>(changeSet, config);
        newTree       = rewriter->transform(tree);
        if (!newTree) {
            result.errors.push_back(fmt::format("First rewrite pass failed for tree {}", i));
            continue;
        }
        allTrees.push_back(newTree); // Keep first pass result alive
        tree = newTree;
        firstRewriters.push_back(std::move(rewriter));
    }

    //==========================================================================
    // Step 9: Rewrite Pass 2 - Instance connection updates
    //==========================================================================
    /*
     * Pass 2 handles:
     *   - Adding port connections to instance instantiations
     *   - Connecting the new XMR ports through the hierarchy
     */
    for (size_t i = 0; i < driver.driver.syntaxTrees.size(); i++) {
        auto &tree = driver.driver.syntaxTrees[i];
        if (!tree) {
            continue;
        }
        auto rewriter = std::make_unique<XMRRewriterSecond>(changeSet);
        newTree       = rewriter->transform(tree);
        allTrees.push_back(newTree); // Keep second pass result alive
        tree = newTree;
        secondRewriters.push_back(std::move(rewriter));
    }

    //==========================================================================
    // Step 10: Generate output
    //==========================================================================
    // Get the original file paths from backup files (format: path/filename.sv.bak)
    std::vector<std::string> originalFilePaths;
    for (const auto &bak : backupFiles) {
        std::filesystem::path bakPath(bak);
        std::string filename = bakPath.filename().string();
        // Remove .bak suffix
        if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".bak") {
            filename = filename.substr(0, filename.size() - 4);
        }
        // Read the backup file to get the original path from //BEGIN: marker
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

            // If content doesn't start with //BEGIN:, add the marker
            if (content.find("//BEGIN:") != 0) {
                // Skip initial whitespace
                size_t start = content.find_first_not_of(" \t\n\r");
                if (start != std::string::npos && content.substr(start, 8) != "//BEGIN:") {
                    // Add //BEGIN: and //END: markers
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

    // Clean up backup files
    for (const auto &backupFile : backupFiles) {
        if (std::filesystem::exists(backupFile)) {
            std::filesystem::remove(backupFile);
        }
    }

    // Store output directory in result
    result.outputDir = actualOutputDir;

    // Print summary
    std::cout << result.getSummary();

    return result;
}

} // namespace xmr
} // namespace slang_common
