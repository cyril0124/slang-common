/*
 * test_helpers.h - Shared test utilities for XMR elimination tests
 *
 * This file contains helper functions used across all XMR elimination test files.
 */

#pragma once

#include "../XMREliminate.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace test_xmr {

/// @brief Create a test file with given content
inline void createTestFile(const std::string &filename, const std::string &content) {
    std::ofstream out(filename);
    out << content;
    out.close();
}

/// @brief Cleanup a test file
inline void cleanupTestFile(const std::string &filename) {
    if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
    }
}

/// @brief Normalize output for comparison (removes extra whitespace, normalizes line endings)
inline std::string normalizeOutput(const std::string &input) {
    std::string result;
    bool prevSpace = false;
    for (char c : input) {
        if (c == '\r')
            continue;
        if (c == ' ' || c == '\t') {
            if (!prevSpace && !result.empty() && result.back() != '\n') {
                result += ' ';
            }
            prevSpace = true;
        } else {
            result += c;
            prevSpace = false;
        }
    }

    // Remove //BEGIN: and //END: markers and their lines
    std::stringstream ss(result);
    std::string line;
    result.clear();
    while (std::getline(ss, line)) {
        if (line.find("//BEGIN:") == 0 || line.find("//END:") == 0) {
            continue;
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty() || !result.empty()) {
            result += line + "\n";
        }
    }
    while (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

/// @brief Generate a colored unified diff between two strings
inline std::string generateColoredDiff(const std::string &actual, const std::string &expected) {
    const char *RED   = "\033[31m";
    const char *GREEN = "\033[32m";
    const char *CYAN  = "\033[36m";
    const char *RESET = "\033[0m";
    const char *BOLD  = "\033[1m";

    std::stringstream diff;
    std::istringstream actualStream(actual), expectedStream(expected);
    std::string actualLine, expectedLine;
    std::vector<std::string> actualLines, expectedLines;

    while (std::getline(actualStream, actualLine))
        actualLines.push_back(actualLine);
    while (std::getline(expectedStream, expectedLine))
        expectedLines.push_back(expectedLine);

    size_t maxLines = std::max(actualLines.size(), expectedLines.size());

    diff << BOLD << CYAN << "=== Diff (actual vs expected) ===" << RESET << "\n";
    diff << CYAN << "Lines prefixed with '-' are from actual output" << RESET << "\n";
    diff << CYAN << "Lines prefixed with '+' are from expected output" << RESET << "\n\n";

    for (size_t i = 0; i < maxLines; ++i) {
        bool hasActual   = i < actualLines.size();
        bool hasExpected = i < expectedLines.size();

        if (hasActual && hasExpected) {
            if (actualLines[i] == expectedLines[i]) {
                diff << "  " << actualLines[i] << "\n";
            } else {
                diff << RED << "- " << actualLines[i] << RESET << "\n";
                diff << GREEN << "+ " << expectedLines[i] << RESET << "\n";
            }
        } else if (hasActual) {
            diff << RED << "- " << actualLines[i] << RESET << "\n";
        } else {
            diff << GREEN << "+ " << expectedLines[i] << RESET << "\n";
        }
    }

    return diff.str();
}

/// @brief Run XMR elimination and compare output with expected golden output
inline void testXMRElimination(const std::string &input, const std::string &expectedOutput, const std::string &testName, const slang_common::xmr::XMREliminateConfig &config = {}) {
    const std::string testFile = testName + ".sv";
    createTestFile(testFile, input);

    slang_common::xmr::XMREliminateConfig cfg = config;
    if (cfg.modules.empty()) {
        cfg.modules = {"top"};
    }

    auto result = slang_common::xmr::xmrEliminate({testFile}, cfg);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    std::string actual   = normalizeOutput(result.modifiedFiles[0]);
    std::string expected = normalizeOutput(expectedOutput);

    if (actual != expected) {
        std::string diff = generateColoredDiff(actual, expected);
        std::cerr << "\n" << diff << std::endl;
        UNSCOPED_INFO(diff);
    }
    REQUIRE(actual == expected);

    cleanupTestFile(testFile);
}

} // namespace test_xmr
