/*
 * XMRTypes.h - Internal type definitions for XMR elimination
 *
 * This file contains internal data structures used by the XMR elimination
 * implementation. These are not part of the public API.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace slang_common {
namespace xmr {
namespace internal {

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

} // namespace internal
} // namespace xmr
} // namespace slang_common
