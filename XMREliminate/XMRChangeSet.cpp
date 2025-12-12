/*
 * XMRChangeSet.cpp - XMR Change computation implementation
 *
 * This file contains the core algorithm to compute all changes needed
 * for XMR elimination with hierarchical propagation.
 */

#include "XMRChangeSet.h"
#include "XMRDetector.h"
#include "fmt/core.h"

#include <algorithm>
#include <set>

namespace slang_common {
namespace xmr {
namespace internal {

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

//==============================================================================
// Helper functions
//==============================================================================

std::string generatePortName(const std::string &fullPath) {
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

std::string extractBasePath(const std::string &fullPath) {
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

std::string extractArraySuffix(const std::string &fullPath) {
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

//==============================================================================
// computeXMRChanges implementation
//==============================================================================

XMRChangeSet computeXMRChanges(const std::vector<XMRInfo> &xmrInfos, slang::ast::Compilation &compilation, const XMREliminateConfig &config) {
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
         * Handle self-reference XMRs (e.g., top.clock referenced from top)
         * These have empty pathSegments after filtering out the self-reference prefix.
         * For these, we just replace the XMR with the signal name directly - no ports needed.
         */
        if (xmr.pathSegments.empty()) {
            // Self-reference: just replace XMR with the signal name
            changes.xmrReplacements[{xmr.sourceModule, xmr.fullPath}] = xmr.targetSignal;
            continue;
        }

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
         */
        if (xmr.isUpwardReference) {
            if (xmr.pathSegments.empty())
                continue;

            std::string rootModuleName = xmr.pathSegments[0];

            // Find the module definition for the first path segment
            std::string firstModuleDef;
            for (const auto &[key, val] : mapper.instanceMap) {
                if (key.second == rootModuleName) {
                    firstModuleDef = val;
                    break;
                }
            }

            if (firstModuleDef.empty()) {
                firstModuleDef = rootModuleName;
            }

            // Add input port in source module
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
                const auto &sourceInstPath = pathFinder.foundPaths[0];

                if (sourceInstPath.size() >= 2) {
                    std::string parentModuleName = firstModuleDef;
                    std::string sourceInstName   = sourceInstPath.back();

                    // Add wire in the parent module
                    {
                        WireDecl wire;
                        wire.moduleName = parentModuleName;
                        wire.wireName   = portName;
                        wire.bitWidth   = xmr.bitWidth;
                        changes.wiresToAdd[parentModuleName].push_back(wire);
                    }

                    // Add connection from source instance
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

            // Trace the XMR path to add output ports
            std::string currentModule = firstModuleDef;
            for (size_t i = 1; i < xmr.pathSegments.size(); i++) {
                const std::string &instName = xmr.pathSegments[i];

                auto it = mapper.instanceMap.find({currentModule, instName});
                if (it == mapper.instanceMap.end()) {
                    break;
                }
                std::string instModuleName = it->second;

                // Add connection
                ConnectionChange conn;
                conn.parentModule   = currentModule;
                conn.instanceName   = instName;
                conn.instanceModule = instModuleName;
                conn.portName       = portName;
                conn.signalName     = portName;
                changes.connectionChanges.push_back(conn);

                // Add output port
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

            continue;
        }

        /*
         * ==================================================================
         * DOWNWARD REFERENCE HANDLING (Relative Path XMR)
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

            auto it = mapper.instanceMap.find({currentModule, instName});
            if (it == mapper.instanceMap.end()) {
                break;
            }
            std::string instModuleName = it->second;

            // Add connection
            ConnectionChange conn;
            conn.parentModule   = currentModule;
            conn.instanceName   = instName;
            conn.instanceModule = instModuleName;
            conn.portName       = portName;
            conn.signalName     = portName;
            changes.connectionChanges.push_back(conn);

            // For intermediate modules, add port to pass through
            // For read XMRs: output ports (signal flows up from target to source)
            // For write XMRs: input ports (signal flows down from source to target)
            if (i < xmr.pathSegments.size() - 1) {
                PortChange passPort;
                passPort.moduleName = instModuleName;
                passPort.portName   = portName;
                passPort.direction  = xmr.isWrite ? "input" : "output";
                passPort.bitWidth   = xmr.bitWidth;
                changes.portsToAdd[instModuleName].push_back(passPort);
            }

            currentModule = instModuleName;
        }

        // Check for pipeline register configuration
        bool hasPipelineRegs = false;
        auto pipeConfigIt    = config.pipeRegConfigMap.find(xmr.sourceModule);
        if (pipeConfigIt != config.pipeRegConfigMap.end()) {
            hasPipelineRegs = pipeConfigIt->second.isEnabled();
        }

        // Add port and assign in target module
        // For write XMRs (DPI output args): input port, assign target = port
        // For read XMRs: output port, assign port = target
        if (!xmr.targetModule.empty()) {
            PortChange tgtPort;
            tgtPort.moduleName     = xmr.targetModule;
            tgtPort.portName       = portName;
            tgtPort.bitWidth       = xmr.bitWidth;
            tgtPort.signalToAssign = xmr.targetSignal;

            if (xmr.isWrite) {
                // Write XMR: DPI output argument writes to target signal
                // Need input port in target module to receive the value
                tgtPort.direction = "input";
                changes.portsToAdd[xmr.targetModule].push_back(tgtPort);

                if (!hasPipelineRegs) {
                    // Assign from port to target signal (write direction)
                    changes.assignsToAdd[xmr.targetModule].push_back(fmt::format("assign {} = {};", xmr.targetSignal, portName));
                }
            } else {
                // Read XMR: source module reads from target signal
                // Need output port in target module to provide the value
                tgtPort.direction = "output";
                changes.portsToAdd[xmr.targetModule].push_back(tgtPort);

                if (!hasPipelineRegs) {
                    // Assign from target signal to port (read direction)
                    changes.assignsToAdd[xmr.targetModule].push_back(fmt::format("assign {} = {};", portName, xmr.targetSignal));
                }
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

} // namespace internal
} // namespace xmr
} // namespace slang_common
