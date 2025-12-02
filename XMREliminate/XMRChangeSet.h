/*
 * XMRChangeSet.h - XMR Change computation
 *
 * This file contains the algorithm to compute all changes needed
 * for XMR elimination with hierarchical propagation.
 */

#pragma once

#include "../XMREliminate.h"
#include "XMRTypes.h"
#include "slang/ast/Compilation.h"

#include <vector>

namespace slang_common {
namespace xmr {
namespace internal {

/// @brief Generate port name for XMR at specific hierarchy level
/// @param fullPath The full XMR path (e.g., "u_mid.u_bottom.counter_value")
/// @return Port name with __xmr__ prefix (e.g., "__xmr__u_mid_u_bottom_counter_value")
std::string generatePortName(const std::string &fullPath);

/// @brief Extract base path from XMR path (remove all array indices)
/// Example: "u_sub.arr[2][3]" -> "u_sub.arr"
std::string extractBasePath(const std::string &fullPath);

/// @brief Extract all array suffixes from XMR path
/// Example: "u_sub.arr[2][3]" -> "[2][3]"
std::string extractArraySuffix(const std::string &fullPath);

/// @brief Compute all changes needed for XMR elimination
XMRChangeSet computeXMRChanges(const std::vector<XMRInfo> &xmrInfos, slang::ast::Compilation &compilation, const XMREliminateConfig &config);

} // namespace internal
} // namespace xmr
} // namespace slang_common
