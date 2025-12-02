/*
 * XMRRewriter.h - XMR Syntax Rewriters
 *
 * This file contains the syntax rewriters that modify the SystemVerilog
 * source code to eliminate XMR references.
 */

#pragma once

#include "../XMREliminate.h"
#include "XMRTypes.h"
#include "slang/syntax/SyntaxVisitor.h"

#include <memory>
#include <set>
#include <vector>

namespace slang_common {
namespace xmr {
namespace internal {

//==============================================================================
// XMR Rewriter - First Pass (Module modifications)
//==============================================================================

/// @brief First pass rewriter: adds ports, assigns, wires, and replaces XMR references
class XMRRewriterFirst : public slang::syntax::SyntaxRewriter<XMRRewriterFirst> {
  public:
    const XMRChangeSet &changes;
    const XMREliminateConfig &config;
    std::string currentModuleName;
    std::set<std::string> addedPorts;

    XMRRewriterFirst(const XMRChangeSet &c, const XMREliminateConfig &cfg);

    void handle(const slang::syntax::ModuleDeclarationSyntax &syntax);
    void handle(const slang::syntax::ScopedNameSyntax &syntax);
};

//==============================================================================
// XMR Rewriter - Second Pass (Instance connections)
//==============================================================================

/// @brief Second pass rewriter: updates instance port connections
class XMRRewriterSecond : public slang::syntax::SyntaxRewriter<XMRRewriterSecond> {
  public:
    const XMRChangeSet &changes;
    std::string currentModuleName;
    std::set<std::string> processedConnections;

    XMRRewriterSecond(const XMRChangeSet &c);

    void handle(const slang::syntax::ModuleDeclarationSyntax &syntax);
    void handle(const slang::syntax::HierarchyInstantiationSyntax &syntax);
};

} // namespace internal
} // namespace xmr
} // namespace slang_common
