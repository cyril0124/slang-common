/*
 * XMRDetector.h - XMR Detection visitor
 *
 * This file contains the AST visitor that detects hierarchical (XMR) references
 * in SystemVerilog code.
 */

#pragma once

#include "../XMREliminate.h"
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/expressions/CallExpression.h"

#include <set>
#include <unordered_set>
#include <vector>

namespace slang_common {
namespace xmr {
namespace internal {

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

    /// Set of syntax nodes for XMRs used as DPI output arguments
    std::set<const slang::syntax::SyntaxNode *> outputArgXMRs;

    XMRDetector(slang::ast::Compilation &comp, const std::vector<std::string> &modules);

    void handle(const slang::ast::InstanceSymbol &inst);
    void handle(const slang::ast::CallExpression &call);
    void handle(const slang::ast::HierarchicalValueExpression &expr);
};

//==============================================================================
// Instance Mapper
//==============================================================================

/// @brief Build a map from instance name to module definition name
class InstanceMapper : public slang::ast::ASTVisitor<InstanceMapper, true, true> {
  public:
    // Maps (parentModule, instanceName) -> instanceModuleName
    std::map<std::pair<std::string, std::string>, std::string> instanceMap;
    std::string currentModuleName;

    void handle(const slang::ast::InstanceSymbol &inst);
};

//==============================================================================
// Instance Path Finder
//==============================================================================

/// @brief Find the instance path from root to a given module
class InstancePathFinder : public slang::ast::ASTVisitor<InstancePathFinder, true, true> {
  public:
    std::string targetModule;
    std::vector<std::string> currentPath;
    std::vector<std::vector<std::string>> foundPaths;

    InstancePathFinder(const std::string &target);
    void handle(const slang::ast::InstanceSymbol &inst);
};

//==============================================================================
// Top Module Detector
//==============================================================================

/// @brief Visitor to collect all module definitions and their instantiations
class TopModuleDetector : public slang::ast::ASTVisitor<TopModuleDetector, true, true> {
  public:
    std::set<std::string> allModules;          // All module definitions
    std::set<std::string> instantiatedModules; // Modules that are instantiated by others
    std::string currentModuleName;

    void handle(const slang::ast::InstanceSymbol &inst);
};

//==============================================================================
// Clock/Reset Verifier
//==============================================================================

/// @brief Verify clock/reset signals exist in modules
struct ClockResetVerifier : public slang::ast::ASTVisitor<ClockResetVerifier, true, true> {
    const std::string &clockName;
    const std::string &resetName;
    std::set<std::string> modulesWithClock;
    std::set<std::string> modulesWithReset;
    std::set<std::string> allModules;

    ClockResetVerifier(const std::string &clk, const std::string &rst);
    void handle(const slang::ast::InstanceSymbol &inst);
};

} // namespace internal
} // namespace xmr
} // namespace slang_common
