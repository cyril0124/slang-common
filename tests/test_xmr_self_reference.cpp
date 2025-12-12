/*
 * test_xmr_self_reference.cpp - Tests for self-reference XMR handling
 *
 * Tests XMR references where the source and target are in the same module
 * (e.g., top.clock from within top), which should just replace with the
 * signal name without adding extra ports.
 *
 * This tests the bug fix for issue where self-references like `top.clock`
 * from within `top` module were incorrectly creating extra ports.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// Self-Reference XMR Tests
//==============================================================================

TEST_CASE("XMR Elimination - Self-reference in top module", "[XMREliminate][SelfRef]") {
    const std::string input = R"(
module top;
    reg clock;
    reg [31:0] data;
    
    initial begin
        clock = 0;
    end
    
    // Self-reference XMRs - should just replace with signal name, no ports
    always @(negedge top.clock) begin
        data <= data + top.data;
    end
endmodule
)";

    // Expected: self-references replaced with signal names, no extra ports on top
    const std::string expected = R"(
module top;
    reg clock;
    reg [31:0] data;

    initial begin
        clock = 0;
    end

    // Self-reference XMRs - should just replace with signal name, no ports
    always @(negedge clock) begin
        data <= data + data;
    end
endmodule
)";

    testXMRElimination(input, expected, "self_ref_simple");
}

TEST_CASE("XMR Elimination - Mixed self-reference and submodule XMR", "[XMREliminate][SelfRef]") {
    const std::string input = R"(
module top;
    reg clock;
    reg [31:0] counter;
    
    sub u_sub(.clk(clock));
    
    // Self-reference should just become 'clock'
    // Submodule XMR should get proper port connection
    always @(negedge top.clock) begin
        counter <= u_sub.value;
    end
endmodule

module sub(input clk);
    reg [31:0] value;
    always @(posedge clk) value <= value + 1;
endmodule
)";

    const std::string testFile = "test_mixed_self_ref.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // Self-reference should be replaced with 'clock', no __xmr__top_clock port
    REQUIRE(output.find("negedge clock") != std::string::npos);
    REQUIRE(output.find("__xmr__top_clock") == std::string::npos);

    // Submodule XMR should create port
    REQUIRE(output.find("__xmr__u_sub_value") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Self-reference should not create extra ports", "[XMREliminate][SelfRef]") {
    // Test case based on the original bug report: dpi_c_xmr_1.sv
    // When top.clock is referenced from within top, we should NOT add ports to top module
    const std::string input = R"(
module top;
    reg clock;
    reg [31:0] accumulator;
    wire valid;
    wire [31:0] value;

    empty u_empty(
        .clock(clock),
        .valid(valid),
        .value(value)
    );

    // Self-references: top.clock, top.accumulator, top.valid, top.value
    // These should just become clock, accumulator, valid, value
    always @(negedge top.clock) begin
        if (top.valid) begin
            accumulator <= top.accumulator + top.value;
        end
    end
endmodule

module empty(
    input wire clock,
    output reg valid,
    output reg [31:0] value
);
endmodule
)";

    const std::string testFile = "test_self_ref_no_ports.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // The top module should NOT have any __xmr__top_* ports
    // Look for module top declaration - it should not have extra ports
    REQUIRE((output.find("module top;") != std::string::npos || output.find("module top(") != std::string::npos));

    // Self-references should NOT create ports
    REQUIRE(output.find("__xmr__top_clock") == std::string::npos);
    REQUIRE(output.find("__xmr__top_accumulator") == std::string::npos);
    REQUIRE(output.find("__xmr__top_valid") == std::string::npos);
    REQUIRE(output.find("__xmr__top_value") == std::string::npos);

    // The code should use direct signal names
    REQUIRE(output.find("negedge clock") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Self-reference combined with submodule XMR in DPI call", "[XMREliminate][SelfRef][DPI]") {
    // Simulates the pattern from dpi_c_xmr_1.sv with DPI call
    const std::string input = R"(
module top;
    reg clock;
    reg [31:0] data;

    empty u_empty(.clock(clock));

    import "DPI-C" function void dpi_func(
        input bit in_clock,
        input bit [31:0] in_data,
        input bit sub_flag
    );

    // Mix of self-reference (top.clock, top.data) and submodule XMR (u_empty.internal_flag)
    always @(negedge top.clock) begin
        dpi_func(top.clock, top.data, u_empty.internal_flag);
    end
endmodule

module empty(input wire clock);
    reg internal_flag;
    always @(posedge clock) internal_flag <= ~internal_flag;
endmodule
)";

    const std::string testFile = "test_self_ref_dpi.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // Self-references should NOT create ports
    REQUIRE(output.find("__xmr__top_clock") == std::string::npos);
    REQUIRE(output.find("__xmr__top_data") == std::string::npos);

    // Submodule XMR SHOULD create port
    REQUIRE(output.find("__xmr__u_empty_internal_flag") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Module with ports and self-reference", "[XMREliminate][SelfRef]") {
    // Test that existing ports are preserved when self-references are used
    const std::string input = R"(
module top(
    input wire external_clock,
    output reg [7:0] result
);
    reg internal_reg;

    // Self-reference to internal signal
    always @(posedge external_clock) begin
        result <= top.internal_reg;
    end
endmodule
)";

    const std::string expected = R"(
module top(
    input wire external_clock,
    output reg [7:0] result
);
    reg internal_reg;

    // Self-reference to internal signal
    always @(posedge external_clock) begin
        result <= internal_reg;
    end
endmodule
)";

    testXMRElimination(input, expected, "self_ref_with_ports");
}
