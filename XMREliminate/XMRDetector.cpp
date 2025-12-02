/*
 * XMRDetector.cpp - XMR Detection visitor implementation
 *
 * This file contains the AST visitor that detects hierarchical (XMR) references
 * in SystemVerilog code.
 */

#include "XMRDetector.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/VariableSymbols.h"

#include <algorithm>

namespace slang_common {
namespace xmr {
namespace internal {

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

//==============================================================================
// XMRDetector implementation
//==============================================================================

XMRDetector::XMRDetector(slang::ast::Compilation &comp, const std::vector<std::string> &modules) : compilation(comp) {
    for (const auto &m : modules) {
        targetModules.insert(m);
    }
}

void XMRDetector::handle(const slang::ast::InstanceSymbol &inst) {
    auto prevInstance = currentInstance;
    currentInstance   = &inst;
    visitDefault(inst);
    currentInstance = prevInstance;
}

void XMRDetector::handle(const slang::ast::HierarchicalValueExpression &expr) {
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

//==============================================================================
// InstanceMapper implementation
//==============================================================================

void InstanceMapper::handle(const slang::ast::InstanceSymbol &inst) {
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

//==============================================================================
// InstancePathFinder implementation
//==============================================================================

InstancePathFinder::InstancePathFinder(const std::string &target) : targetModule(target) {}

void InstancePathFinder::handle(const slang::ast::InstanceSymbol &inst) {
    std::string defName = std::string(inst.getDefinition().name);
    currentPath.push_back(std::string(inst.name));

    if (defName == targetModule) {
        foundPaths.push_back(currentPath);
    }

    visitDefault(inst);
    currentPath.pop_back();
}

//==============================================================================
// TopModuleDetector implementation
//==============================================================================

void TopModuleDetector::handle(const slang::ast::InstanceSymbol &inst) {
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

//==============================================================================
// ClockResetVerifier implementation
//==============================================================================

ClockResetVerifier::ClockResetVerifier(const std::string &clk, const std::string &rst) : clockName(clk), resetName(rst) {}

void ClockResetVerifier::handle(const slang::ast::InstanceSymbol &inst) {
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

} // namespace internal
} // namespace xmr
} // namespace slang_common
