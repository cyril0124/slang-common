/*
 * test_xmr_detection.cpp - Tests for XMR detection
 *
 * Tests the XMR detection and top module detection functionality.
 */

#include "../SlangCommon.h"
#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// XMR Detection Tests
//==============================================================================

TEST_CASE("XMR Detection - Simple hierarchy", "[XMRDetect]") {
    const std::string testFile = "test_xmr_detect.sv";

    createTestFile(testFile, R"(
module top;
    wire clk;
    sub_module u_sub();
    
    // This is an XMR reference
    wire data_copy;
    assign data_copy = u_sub.internal_data;
endmodule

module sub_module;
    reg internal_data;
    
    always @(*) begin
        internal_data = 1'b1;
    end
endmodule
    )");

    SECTION("Detect XMR in top module") {
        auto xmrs = detectXMRs({testFile}, {"top"});
        REQUIRE_NOTHROW(detectXMRs({testFile}, {"top"}));
    }

    SECTION("Detect XMR in all modules") {
        auto xmrs = detectXMRs({testFile}, {});
        REQUIRE_NOTHROW(detectXMRs({testFile}, {}));
    }

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Detection - Multi-level hierarchy", "[XMRDetect]") {
    const std::string testFile = "test_xmr_multilevel.sv";

    createTestFile(testFile, R"(
module top;
    mid_module u_mid();
    
    // XMR across two levels
    wire deep_data;
    assign deep_data = u_mid.u_bottom.deep_signal;
endmodule

module mid_module;
    bottom_module u_bottom();
    wire mid_signal;
endmodule

module bottom_module;
    reg deep_signal;
    initial deep_signal = 1'b0;
endmodule
    )");

    SECTION("Detect multi-level XMR") {
        auto xmrs = detectXMRs({testFile}, {"top"});
        REQUIRE_NOTHROW(detectXMRs({testFile}, {"top"}));
    }

    cleanupTestFile(testFile);
}

//==============================================================================
// Top Module Detection Tests
//==============================================================================

TEST_CASE("Top Module Detection - Single top module", "[TopModuleDetect]") {
    const std::string testFile = "test_top_detect_single.sv";

    createTestFile(testFile, R"(
module top;
    sub u_sub();
endmodule

module sub;
    reg data;
endmodule
    )");

    slang_common::Driver driver("TestDriver");
    driver.addStandardArgs();
    driver.addFile(testFile);
    driver.loadAllSources();
    driver.processOptions(true);
    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    REQUIRE(compilation != nullptr);

    auto topModules = detectTopModules(*compilation);
    REQUIRE(topModules.size() == 1);
    REQUIRE(topModules[0] == "top");

    cleanupTestFile(testFile);
}

TEST_CASE("Top Module Detection - Multiple top modules", "[TopModuleDetect]") {
    const std::string testFile = "test_top_detect_multi.sv";

    createTestFile(testFile, R"(
module top1;
    sub u_sub();
endmodule

module top2;
    sub u_sub();
endmodule

module sub;
    reg data;
endmodule
    )");

    slang_common::Driver driver("TestDriver");
    driver.addStandardArgs();
    driver.addFile(testFile);
    driver.loadAllSources();
    driver.processOptions(true);
    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    REQUIRE(compilation != nullptr);

    auto topModules = detectTopModules(*compilation);
    REQUIRE(topModules.size() == 2);
    REQUIRE(topModules[0] == "top1");
    REQUIRE(topModules[1] == "top2");

    cleanupTestFile(testFile);
}

TEST_CASE("Top Module Detection - Deep hierarchy", "[TopModuleDetect]") {
    const std::string testFile = "test_top_detect_deep.sv";

    createTestFile(testFile, R"(
module root;
    level1 u_l1();
endmodule

module level1;
    level2 u_l2();
endmodule

module level2;
    level3 u_l3();
endmodule

module level3;
    reg data;
endmodule
    )");

    slang_common::Driver driver("TestDriver");
    driver.addStandardArgs();
    driver.addFile(testFile);
    driver.loadAllSources();
    driver.processOptions(true);
    driver.parseAllSources();

    auto compilation = driver.createCompilation();
    REQUIRE(compilation != nullptr);

    auto topModules = detectTopModules(*compilation);
    REQUIRE(topModules.size() == 1);
    REQUIRE(topModules[0] == "root");

    cleanupTestFile(testFile);
}

//==============================================================================
// Auto-detection Tests
//==============================================================================

TEST_CASE("XMR Elimination - Auto-detect all XMRs without -m", "[XMREliminate][AutoDetect]") {
    const std::string input = R"(
module top;
    sub u_sub();
    wire out;
    assign out = u_sub.data;
endmodule

module sub;
    reg data;
endmodule
)";

    const std::string testFile = "test_auto_detect.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    // config.modules is empty

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.size() == 1);
    REQUIRE(result.eliminatedXMRs[0].fullPath == "u_sub.data");
    REQUIRE(result.detectedTopModules.size() == 1);
    REQUIRE(result.detectedTopModules[0] == "top");

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Auto-detect with multiple top modules", "[XMREliminate][AutoDetect]") {
    const std::string input = R"(
module tb_top;
    dut u_dut();
    wire result;
    assign result = u_dut.signal;
endmodule

module bench;
    dut u_dut2();
    wire check;
    assign check = u_dut2.signal;
endmodule

module dut;
    reg signal;
endmodule
)";

    const std::string testFile = "test_auto_detect_multi_top.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.size() == 2);
    REQUIRE(result.detectedTopModules.size() == 2);
    REQUIRE(result.warnings.size() >= 1);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Specify top module with config", "[XMREliminate][TopModule]") {
    const std::string input = R"(
module tb;
    dut u_dut();
    wire out;
    assign out = u_dut.data;
endmodule

module dut;
    reg data;
endmodule
)";

    const std::string testFile = "test_specify_top.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.topModule = "tb";

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.usedTopModule == "tb");
    REQUIRE(result.eliminatedXMRs.size() == 1);

    cleanupTestFile(testFile);
}
