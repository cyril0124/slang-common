/*
 * XMRRewriter.cpp - XMR Syntax Rewriters implementation
 *
 * This file contains the syntax rewriters that modify the SystemVerilog
 * source code to eliminate XMR references.
 */

#include "XMRRewriter.h"
#include "../XMREliminate.h"
#include "XMRChangeSet.h"
#include "fmt/core.h"

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

//==============================================================================
// XMRRewriterFirst implementation
//==============================================================================

XMRRewriterFirst::XMRRewriterFirst(const XMRChangeSet &c, const XMREliminateConfig &cfg) : changes(c), config(cfg) {}

void XMRRewriterFirst::handle(const slang::syntax::ModuleDeclarationSyntax &syntax) {
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

void XMRRewriterFirst::handle(const slang::syntax::ScopedNameSyntax &syntax) {
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

//==============================================================================
// XMRRewriterSecond implementation
//==============================================================================

XMRRewriterSecond::XMRRewriterSecond(const XMRChangeSet &c) : changes(c) {}

void XMRRewriterSecond::handle(const slang::syntax::ModuleDeclarationSyntax &syntax) {
    currentModuleName = std::string(syntax.header->name.rawText());
    visitDefault(syntax);
}

void XMRRewriterSecond::handle(const slang::syntax::HierarchyInstantiationSyntax &syntax) {
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
        bool addedAnyConnection     = false;
        bool hasExistingConnections = !inst.connections.empty();

        for (const auto *conn : connIt->second) {
            std::string connKey = currentModuleName + "." + thisInstName + "." + conn->portName;
            if (processedConnections.count(connKey) > 0)
                continue;
            processedConnections.insert(connKey);

            // Add port connection
            bool needComma = hasExistingConnections || addedAnyConnection;
            if (needComma) {
                std::string newConn = fmt::format(",\n        .{}({})", conn->portName, conn->signalName);
                insertAtBack(inst.connections, parse(newConn));
            } else {
                std::string newConn = fmt::format("\n        .{}({})", conn->portName, conn->signalName);
                insertAtBack(inst.connections, parse(newConn));
            }
            addedAnyConnection = true;
        }
    }

    visitDefault(syntax);
}

} // namespace internal
} // namespace xmr
} // namespace slang_common
