/*
 * test_xmr_upward.cpp - Tests for upward (absolute path) XMR elimination
 *
 * Tests XMR scenarios where signals are accessed using absolute hierarchical
 * paths starting from the top module, such as tb_top.clock or tb_top.uut.counter.
 */

#include "../SlangCommon.h"
#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// Upward Reference XMR Tests
//==============================================================================

TEST_CASE("XMR Eliminate - Upward references from sibling module", "[XMREliminate][Upward]") {
    // This tests the scenario from examples/upward_xmr where the "others" module
    // accesses signals from tb_top and tb_top.uut using absolute paths.
    const std::string testFile1 = "test_upward_sibling_tb_top.sv";
    const std::string testFile2 = "test_upward_sibling_dut.sv";
    const std::string testFile3 = "test_upward_sibling_others.sv";

    const std::string tb_top = R"(
module tb_top;
    logic clock;
    logic reset;
    dut uut(.clock(clock), .reset(reset));
    others other_inst();
endmodule
)";

    const std::string dut = R"(
module dut(input wire clock, input wire reset);
    reg [3:0] counter;
    reg another_reg;
    always_ff @(posedge clock or posedge reset) begin
        if (reset) counter <= 4'b0;
        else counter <= counter + 1;
    end
    always_ff @(posedge clock or posedge reset) begin
        if (reset) another_reg <= 1'b0;
        else another_reg <= ~another_reg;
    end
endmodule
)";

    const std::string others = R"(
module others;
    default clocking @(posedge tb_top.clock);
    endclocking
    property TestProperty;
        disable iff(tb_top.reset) tb_top.uut.counter[0] && tb_top.uut.another_reg;
    endproperty
    cover_test: cover property (TestProperty);
endmodule
)";

    createTestFile(testFile1, tb_top);
    createTestFile(testFile2, dut);
    createTestFile(testFile3, others);

    XMREliminateConfig config;
    config.topModule = "tb_top";

    auto result = xmrEliminate({testFile1, testFile2, testFile3}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.size() == 4);

    // All XMRs should come from "others" module
    for (const auto &xmr : result.eliminatedXMRs) {
        REQUIRE(xmr.sourceModule == "others");
    }

    // Check the summary
    std::string summary = result.getSummary();
    REQUIRE(summary.find("XMRs Eliminated: 4") != std::string::npos);
    REQUIRE(summary.find("tb_top.clock") != std::string::npos);
    REQUIRE(summary.find("tb_top.reset") != std::string::npos);
    REQUIRE(summary.find("tb_top.uut.counter") != std::string::npos);
    REQUIRE(summary.find("tb_top.uut.another_reg") != std::string::npos);

    // Verify the modified files
    REQUIRE(result.modifiedFiles.size() == 3);

    // Check that others module has input ports for all XMRs
    bool foundOthersWithPorts = false;
    for (const auto &content : result.modifiedFiles) {
        if (content.find("module others") != std::string::npos) {
            if (content.find("input wire __xmr__tb_top_clock") != std::string::npos) {
                foundOthersWithPorts = true;
                REQUIRE(content.find("input wire __xmr__tb_top_reset") != std::string::npos);
                REQUIRE(content.find("input wire [3:0] __xmr__tb_top_uut_counter") != std::string::npos);
                REQUIRE(content.find("input wire __xmr__tb_top_uut_another_reg") != std::string::npos);
            }
        }
    }
    REQUIRE(foundOthersWithPorts);

    cleanupTestFile(testFile1);
    cleanupTestFile(testFile2);
    cleanupTestFile(testFile3);
}

TEST_CASE("XMR Eliminate - Default clocking with upward XMR", "[XMREliminate][Upward][DefaultClocking]") {
    const std::string input = R"(
module top;
    logic clk;
    logic rst;
    dut u_dut(.clk(clk), .rst(rst));
    checker_module u_checker();
endmodule

module dut(input wire clk, input wire rst);
    reg [7:0] data;
endmodule

module checker_module;
    default clocking @(posedge top.clk);
    endclocking
    
    default disable iff (top.rst);
    
    property p_data_stable;
        top.u_dut.data == $past(top.u_dut.data);
    endproperty
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_default_clocking.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.size() >= 1); // At least clk

    // Verify default clocking is updated
    bool foundDefaultClocking = false;
    for (const auto &content : result.modifiedFiles) {
        if (content.find("default clocking @(posedge __xmr__top_clk)") != std::string::npos) {
            foundDefaultClocking = true;
        }
    }
    REQUIRE(foundDefaultClocking);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - Multiple sibling modules accessing same XMRs", "[XMREliminate][Upward][MultiSibling]") {
    const std::string input = R"(
module top;
    logic clk;
    logic rst;
    dut u_dut(.clk(clk), .rst(rst));
    checker1 u_checker1();
    checker2 u_checker2();
endmodule

module dut(input wire clk, input wire rst);
    reg [7:0] counter;
endmodule

module checker1;
    default clocking @(posedge top.clk);
    endclocking
    property p_counter_range;
        disable iff(top.rst) top.u_dut.counter < 100;
    endproperty
endmodule

module checker2;
    default clocking @(posedge top.clk);
    endclocking
    property p_counter_nonzero;
        disable iff(top.rst) !top.rst |-> top.u_dut.counter > 0;
    endproperty
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_multi_sibling.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    // Both checker modules should have XMRs detected
    int checker1Count = 0, checker2Count = 0;
    for (const auto &xmr : result.eliminatedXMRs) {
        if (xmr.sourceModule == "checker1")
            checker1Count++;
        if (xmr.sourceModule == "checker2")
            checker2Count++;
    }
    REQUIRE(checker1Count >= 1); // At least clk or counter
    REQUIRE(checker2Count >= 1); // At least clk or counter

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - Upward XMR with array element access", "[XMREliminate][Upward][Array]") {
    const std::string input = R"(
module top;
    logic clk;
    dut u_dut(.clk(clk));
    monitor u_mon();
endmodule

module dut(input wire clk);
    reg [7:0] data_array [0:3];
    reg [31:0] wide_data;
endmodule

module monitor;
    default clocking @(posedge top.clk);
    endclocking
    
    // Access specific array element
    wire elem0 = top.u_dut.data_array[0][0];
    wire elem1 = top.u_dut.data_array[1][7];
    
    // Access bit of wide signal
    wire bit15 = top.u_dut.wide_data[15];
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_upward_array.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.size() >= 1);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - Deeply nested upward XMR", "[XMREliminate][Upward][DeepNested]") {
    const std::string input = R"(
module top;
    logic clk;
    level1 u_l1(.clk(clk));
    leaf_checker u_checker();
endmodule

module level1(input wire clk);
    level2 u_l2(.clk(clk));
endmodule

module level2(input wire clk);
    level3 u_l3(.clk(clk));
endmodule

module level3(input wire clk);
    reg [15:0] deep_data;
endmodule

module leaf_checker;
    default clocking @(posedge top.clk);
    endclocking
    
    // Wire assignment to use the XMR (properties may not be visited)
    wire [15:0] local_deep = top.u_l1.u_l2.u_l3.deep_data;
    
    property p_deep_access;
        local_deep != 16'hFFFF;
    endproperty
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_deeply_nested_upward.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    // Should detect the deep XMR
    bool foundDeepXMR = false;
    for (const auto &xmr : result.eliminatedXMRs) {
        if (xmr.fullPath.find("deep_data") != std::string::npos) {
            foundDeepXMR = true;
        }
    }
    REQUIRE(foundDeepXMR);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - SVA sequence with upward XMR", "[XMREliminate][Upward][SVASequence]") {
    const std::string input = R"(
module top;
    logic clk;
    logic rst;
    dut u_dut(.clk(clk), .rst(rst));
    sva_checker u_sva();
endmodule

module dut(input wire clk, input wire rst);
    reg req;
    reg ack;
    reg [3:0] state;
endmodule

module sva_checker;
    default clocking @(posedge top.clk);
    endclocking
    
    sequence req_ack_seq;
        top.u_dut.req ##[1:5] top.u_dut.ack;
    endsequence
    
    property p_handshake;
        disable iff(top.rst) top.u_dut.req |-> req_ack_seq;
    endproperty
    
    assert property (p_handshake);
    cover property (req_ack_seq);
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_sva_sequence.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    // Should detect XMRs in sequence and property
    std::string summary = result.getSummary();
    REQUIRE(summary.find("top.u_dut.req") != std::string::npos);
    REQUIRE(summary.find("top.u_dut.ack") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - Property with local variable and upward XMR", "[XMREliminate][Upward][LocalVar]") {
    // This tests a scenario similar to the tmp/test_xmr issue where properties
    // have local variables combined with XMRs
    const std::string input = R"(
module top;
    logic clk;
    logic rst;
    dut u_dut(.clk(clk), .rst(rst));
    checker_mod u_checker();
endmodule

module dut(input wire clk, input wire rst);
    reg req_valid;
    reg req_ready;
    reg [7:0] req_data;
    reg ack_valid;
    reg [7:0] ack_data;
endmodule

module checker_mod;
    default clocking @(posedge top.clk);
    endclocking
    
    property p_req_ack_data_match;
        int saved_data;
        disable iff(top.rst) 
        (top.u_dut.req_valid && top.u_dut.req_ready, saved_data = top.u_dut.req_data) |->
        ##[1:10] (top.u_dut.ack_valid && (top.u_dut.ack_data == saved_data));
    endproperty
    
    assert property (p_req_ack_data_match);
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_local_var_xmr.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    // Verify all XMRs were detected (all signals exist in this case)
    REQUIRE(result.eliminatedXMRs.size() >= 5); // clk, rst, req_valid, req_ready, req_data, ack_valid, ack_data

    // Check that XMRs in the local variable assignment are replaced
    bool foundReqData = false;
    bool foundAckData = false;
    for (const auto &xmr : result.eliminatedXMRs) {
        if (xmr.fullPath.find("req_data") != std::string::npos)
            foundReqData = true;
        if (xmr.fullPath.find("ack_data") != std::string::npos)
            foundAckData = true;
    }
    REQUIRE(foundReqData);
    REQUIRE(foundAckData);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Eliminate - Cover property with upward XMR", "[XMREliminate][Upward][CoverProperty]") {
    const std::string input = R"(
module top;
    logic clk;
    logic rst;
    dut u_dut(.clk(clk), .rst(rst));
    cover_mod u_cover();
endmodule

module dut(input wire clk, input wire rst);
    reg [1:0] state;
    reg valid;
    reg ready;
endmodule

module cover_mod;
    default clocking @(posedge top.clk);
    endclocking
    
    // Multiple cover properties with various XMRs
    cover property (@(posedge top.clk) disable iff(top.rst) 
        top.u_dut.valid && top.u_dut.ready);
    
    cover property (@(posedge top.clk) disable iff(top.rst) 
        top.u_dut.state == 2'b00 ##1 top.u_dut.state == 2'b01);
    
    cover property (@(posedge top.clk) disable iff(top.rst) 
        top.u_dut.state == 2'b11);
endmodule
)";

    XMREliminateConfig config;
    config.topModule = "top";

    const std::string testFile = "test_cover_property.sv";
    createTestFile(testFile, input);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    // All cover property XMRs should be detected
    bool foundState = false;
    bool foundValid = false;
    bool foundReady = false;
    for (const auto &xmr : result.eliminatedXMRs) {
        if (xmr.fullPath.find("state") != std::string::npos)
            foundState = true;
        if (xmr.fullPath.find("valid") != std::string::npos)
            foundValid = true;
        if (xmr.fullPath.find("ready") != std::string::npos)
            foundReady = true;
    }
    REQUIRE(foundState);
    REQUIRE(foundValid);
    REQUIRE(foundReady);

    cleanupTestFile(testFile);
}
