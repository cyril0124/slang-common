/*
 * test_xmr_basic.cpp - Basic XMR elimination tests
 *
 * Tests basic XMR elimination functionality, error handling, and simple scenarios.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// XMR Elimination Tests - Basic
//==============================================================================

TEST_CASE("XMR Elimination - Empty input", "[XMREliminate]") {
    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({}, config);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.errors.size() > 0);
    REQUIRE(result.errors[0] == "No input files provided");
}

TEST_CASE("XMR Elimination - No XMRs found", "[XMREliminate]") {
    const std::string testFile = "test_no_xmr.sv";

    createTestFile(testFile, R"(
module simple_module(
    input wire clk,
    input wire data_in,
    output reg data_out
);
    always @(posedge clk) begin
        data_out <= data_in;
    end
endmodule
    )");

    XMREliminateConfig config;
    config.modules = {"simple_module"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.warnings.size() > 0);
    REQUIRE(result.eliminatedXMRs.empty());
    REQUIRE(result.modifiedFiles.size() == 1);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Basic XMR replacement", "[XMREliminate]") {
    const std::string testFile = "test_basic_xmr.sv";

    createTestFile(testFile, R"(
module top;
    sub_module u_sub();
    
    wire data_out;
    assign data_out = u_sub.internal_signal;
endmodule

module sub_module;
    wire internal_signal;
    assign internal_signal = 1'b1;
endmodule
    )");

    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() == 1);

    cleanupTestFile(testFile);
}

//==============================================================================
// Error Handling Tests
//==============================================================================

TEST_CASE("XMR Elimination - Non-existent file", "[XMREliminate][Error]") {
    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({"non_existent_file.sv"}, config);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.errors.size() > 0);
    REQUIRE(result.errors[0].find("does not exist") != std::string::npos);
}

TEST_CASE("XMR Elimination - Syntax error in input", "[XMREliminate][Error]") {
    const std::string testFile = "test_syntax_error.sv";

    createTestFile(testFile, R"(
module broken_module
    // Missing port list parentheses and semicolon
    wire x
endmodule
    )");

    XMREliminateConfig config;
    config.modules = {"broken_module"};

    REQUIRE_NOTHROW(xmrEliminate({testFile}, config));

    cleanupTestFile(testFile);
}

//==============================================================================
// Output Verification Tests
//==============================================================================

TEST_CASE("XMR Elimination - Output format verification", "[XMREliminate][Output]") {
    const std::string input = R"(
module top(
    input clk,
    output wire result
);
    sub u_sub();
    assign result = u_sub.sig;
endmodule

module sub;
    reg sig;
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    output wire result
);
    wire __xmr__u_sub_sig;
    sub u_sub(
        .__xmr__u_sub_sig(__xmr__u_sub_sig));
    assign result = __xmr__u_sub_sig;
endmodule

module sub( __xmr__u_sub_sig);
    output wire __xmr__u_sub_sig;
    reg sig;
    assign __xmr__u_sub_sig = sig;
endmodule
)";

    testXMRElimination(input, expected, "output_verify");
}

//==============================================================================
// Multi-file Tests
//==============================================================================

TEST_CASE("XMR Elimination - Multi-file project", "[XMREliminate][Integration]") {
    const std::string file1 = "test_multi_top.sv";
    const std::string file2 = "test_multi_sub.sv";

    createTestFile(file1, R"(
module top(
    input wire clk,
    output wire result
);
    sub_module u_sub(.clk(clk));
    
    // XMR reference
    assign result = u_sub.computed_value;
endmodule
    )");

    createTestFile(file2, R"(
module sub_module(
    input wire clk
);
    reg computed_value;
    
    always @(posedge clk) begin
        computed_value <= ~computed_value;
    end
endmodule
    )");

    XMREliminateConfig config;
    config.modules   = {"top"};
    config.clockName = "clk";

    auto result = xmrEliminate({file1, file2}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];
    REQUIRE(output.find("__xmr__u_sub_computed_value") != std::string::npos);

    cleanupTestFile(file1);
    cleanupTestFile(file2);
}

//==============================================================================
// Summary Tests
//==============================================================================

TEST_CASE("XMR Eliminate Result - Summary generation", "[XMREliminate][Summary]") {
    XMREliminateResult result;
    result.outputDir          = "/tmp/test_output";
    result.detectedTopModules = {"top"};
    result.usedTopModule      = "top";

    XMRInfo xmr1;
    xmr1.sourceModule = "top";
    xmr1.targetModule = "sub";
    xmr1.targetSignal = "data";
    xmr1.fullPath     = "u_sub.data";
    xmr1.bitWidth     = 8;

    XMRInfo xmr2;
    xmr2.sourceModule = "top";
    xmr2.targetModule = "sub";
    xmr2.targetSignal = "counter";
    xmr2.fullPath     = "u_sub.counter";
    xmr2.bitWidth     = 32;

    result.eliminatedXMRs = {xmr1, xmr2};

    std::string summary = result.getSummary();

    REQUIRE(summary.find("XMR ELIMINATION SUMMARY") != std::string::npos);
    REQUIRE(summary.find("Detected top module(s): top") != std::string::npos);
    REQUIRE(summary.find("Used top module: top") != std::string::npos);
    REQUIRE(summary.find("XMRs Eliminated: 2") != std::string::npos);
    REQUIRE(summary.find("u_sub.data") != std::string::npos);
    REQUIRE(summary.find("u_sub.counter") != std::string::npos);
    REQUIRE(summary.find("width: 8") != std::string::npos);
    REQUIRE(summary.find("width: 32") != std::string::npos);
}

TEST_CASE("XMR Elimination - Verify summary is generated", "[XMREliminate][Summary]") {
    const std::string input = R"(
module top;
    sub1 u_sub1();
    sub2 u_sub2();
    wire a, b;
    assign a = u_sub1.signal_a;
    assign b = u_sub2.signal_b;
endmodule

module sub1;
    reg signal_a;
endmodule

module sub2;
    reg signal_b;
endmodule
)";

    const std::string testFile = "test_summary_verify.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    std::string summary = result.getSummary();
    REQUIRE(summary.find("XMRs Eliminated: 2") != std::string::npos);
    REQUIRE(summary.find("u_sub1.signal_a") != std::string::npos);
    REQUIRE(summary.find("u_sub2.signal_b") != std::string::npos);
    REQUIRE(summary.find("Target Modules Affected:") != std::string::npos);

    cleanupTestFile(testFile);
}
