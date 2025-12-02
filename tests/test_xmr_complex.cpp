/*
 * test_xmr_complex.cpp - Complex XMR elimination tests
 *
 * Tests complex scenarios including deep hierarchy, multiple XMRs,
 * XMRs in expressions, and various code contexts.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// Complex Scenario Tests
//==============================================================================

TEST_CASE("XMR Elimination - Multiple XMRs in same module", "[XMREliminate][Complex]") {
    const std::string input = R"(
module top(
    input clk,
    output wire out1,
    output wire out2,
    output wire out3
);
    sub u_sub1();
    sub u_sub2();
    assign out1 = u_sub1.sig_a;
    assign out2 = u_sub1.sig_b;
    assign out3 = u_sub2.sig_a;
endmodule

module sub;
    reg sig_a;
    reg sig_b;
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    output wire out1,
    output wire out2,
    output wire out3
);
    wire __xmr__u_sub1_sig_a;
    wire __xmr__u_sub1_sig_b;
    wire __xmr__u_sub2_sig_a;
    sub u_sub1(
        .__xmr__u_sub1_sig_a(__xmr__u_sub1_sig_a),
        .__xmr__u_sub1_sig_b(__xmr__u_sub1_sig_b));
    sub u_sub2(
        .__xmr__u_sub2_sig_a(__xmr__u_sub2_sig_a));
    assign out1 = __xmr__u_sub1_sig_a;
    assign out2 = __xmr__u_sub1_sig_b;
    assign out3 = __xmr__u_sub2_sig_a;
endmodule

module sub;
    output wire __xmr__u_sub1_sig_a;
    output wire __xmr__u_sub1_sig_b;
    output wire __xmr__u_sub2_sig_a;
    reg sig_a;
    reg sig_b;
    assign __xmr__u_sub1_sig_a = sig_a;
    assign __xmr__u_sub1_sig_b = sig_b;
    assign __xmr__u_sub2_sig_a = sig_a;
endmodule
)";

    testXMRElimination(input, expected, "multi_xmr");
}

TEST_CASE("XMR Elimination - XMR in expression", "[XMREliminate][Complex]") {
    const std::string input = R"(
module top(
    input clk,
    output wire result
);
    sub u_sub1();
    sub u_sub2();
    assign result = u_sub1.data & u_sub2.data;
endmodule

module sub;
    reg data;
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    output wire result
);
    wire __xmr__u_sub1_data;
    wire __xmr__u_sub2_data;
    sub u_sub1(
        .__xmr__u_sub1_data(__xmr__u_sub1_data));
    sub u_sub2(
        .__xmr__u_sub2_data(__xmr__u_sub2_data));
    assign result = __xmr__u_sub1_data & __xmr__u_sub2_data;
endmodule

module sub;
    output wire __xmr__u_sub1_data;
    output wire __xmr__u_sub2_data;
    reg data;
    assign __xmr__u_sub1_data = data;
    assign __xmr__u_sub2_data = data;
endmodule
)";

    testXMRElimination(input, expected, "xmr_expr");
}

TEST_CASE("XMR Elimination - Deep hierarchy", "[XMREliminate][Complex]") {
    const std::string input = R"(
module top(
    output wire result
);
    level1 u_l1();
    assign result = u_l1.u_l2.u_l3.deep_signal;
endmodule

module level1;
    level2 u_l2();
endmodule

module level2;
    level3 u_l3();
endmodule

module level3;
    reg deep_signal;
endmodule
)";

    const std::string expected = R"(
module top(
    output wire result
);
    wire __xmr__u_l1_u_l2_u_l3_deep_signal;
    level1 u_l1(
        .__xmr__u_l1_u_l2_u_l3_deep_signal(__xmr__u_l1_u_l2_u_l3_deep_signal));
    assign result = __xmr__u_l1_u_l2_u_l3_deep_signal;
endmodule

module level1;
    output wire __xmr__u_l1_u_l2_u_l3_deep_signal;
    level2 u_l2(
        .__xmr__u_l1_u_l2_u_l3_deep_signal(__xmr__u_l1_u_l2_u_l3_deep_signal));
endmodule

module level2;
    output wire __xmr__u_l1_u_l2_u_l3_deep_signal;
    level3 u_l3(
        .__xmr__u_l1_u_l2_u_l3_deep_signal(__xmr__u_l1_u_l2_u_l3_deep_signal));
endmodule

module level3;
    output wire __xmr__u_l1_u_l2_u_l3_deep_signal;
    reg deep_signal;
    assign __xmr__u_l1_u_l2_u_l3_deep_signal = deep_signal;
endmodule
)";

    testXMRElimination(input, expected, "deep_hier");
}

TEST_CASE("XMR Elimination - Vector signal", "[XMREliminate][Complex]") {
    const std::string input = R"(
module top(
    output wire [31:0] data_out
);
    sub u_sub();
    assign data_out = u_sub.wide_bus;
endmodule

module sub;
    reg [31:0] wide_bus;
endmodule
)";

    const std::string expected = R"(
module top(
    output wire [31:0] data_out
);
    wire [31:0] __xmr__u_sub_wide_bus;
    sub u_sub(
        .__xmr__u_sub_wide_bus(__xmr__u_sub_wide_bus));
    assign data_out = __xmr__u_sub_wide_bus;
endmodule

module sub;
    output wire [31:0] __xmr__u_sub_wide_bus;
    reg [31:0] wide_bus;
    assign __xmr__u_sub_wide_bus = wide_bus;
endmodule
)";

    testXMRElimination(input, expected, "vector_sig");
}

//==============================================================================
// XMR in Different Contexts Tests
//==============================================================================

TEST_CASE("XMR Elimination in Various Contexts", "[XMREliminate][Contexts]") {
    SECTION("Always block") {
        const std::string input = R"(
module top(
    input clk,
    input rst_n,
    output reg result
);
    sub u_sub();
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            result <= 1'b0;
        else
            result <= u_sub.data;
    end
endmodule

module sub;
    reg data;
endmodule
)";

        const std::string expected = R"(
module top(
    input clk,
    input rst_n,
    output reg result
);
    wire __xmr__u_sub_data;
    sub u_sub(
        .__xmr__u_sub_data(__xmr__u_sub_data));
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            result <= 1'b0;
        else
            result <= __xmr__u_sub_data;
    end
endmodule

module sub;
    output wire __xmr__u_sub_data;
    reg data;
    assign __xmr__u_sub_data = data;
endmodule
)";
        testXMRElimination(input, expected, "ctx_always");
    }

    SECTION("Conditional expression") {
        const std::string input = R"(
module top(
    input sel,
    output wire result
);
    sub u_sub_a();
    sub u_sub_b();
    assign result = sel ? u_sub_a.data : u_sub_b.data;
endmodule

module sub;
    reg data;
endmodule
)";

        const std::string expected = R"(
module top(
    input sel,
    output wire result
);
    wire __xmr__u_sub_a_data;
    wire __xmr__u_sub_b_data;
    sub u_sub_a(
        .__xmr__u_sub_a_data(__xmr__u_sub_a_data));
    sub u_sub_b(
        .__xmr__u_sub_b_data(__xmr__u_sub_b_data));
    assign result = sel ? __xmr__u_sub_a_data : __xmr__u_sub_b_data;
endmodule

module sub;
    output wire __xmr__u_sub_a_data;
    output wire __xmr__u_sub_b_data;
    reg data;
    assign __xmr__u_sub_a_data = data;
    assign __xmr__u_sub_b_data = data;
endmodule
)";
        testXMRElimination(input, expected, "ctx_conditional");
    }

    SECTION("Function call arguments") {
        const std::string input = R"(
module top(
    output wire [7:0] result
);
    sub u_sub();
    function automatic logic [7:0] invert_byte;
        input logic [7:0] in;
        return ~in;
    endfunction
    assign result = invert_byte(u_sub.byte_data);
endmodule

module sub;
    reg [7:0] byte_data;
endmodule
)";

        const std::string expected = R"(
module top(
    output wire [7:0] result
);
    wire [7:0] __xmr__u_sub_byte_data;
    sub u_sub(
        .__xmr__u_sub_byte_data(__xmr__u_sub_byte_data));
    function automatic logic [7:0] invert_byte;
        input logic [7:0] in;
        return ~in;
    endfunction
    assign result = invert_byte( __xmr__u_sub_byte_data);
endmodule

module sub;
    output wire [7:0] __xmr__u_sub_byte_data;
    reg [7:0] byte_data;
    assign __xmr__u_sub_byte_data = byte_data;
endmodule
)";
        testXMRElimination(input, expected, "ctx_function");
    }

    SECTION("Case statement") {
        const std::string input = R"(
module top(
    input clk,
    output reg [1:0] state
);
    ctrl u_ctrl();
    always @(posedge clk) begin
        case (u_ctrl.mode)
            2'b00: state <= 2'b01;
            2'b01: state <= 2'b10;
            default: state <= 2'b00;
        endcase
    end
endmodule

module ctrl;
    reg [1:0] mode;
endmodule
)";

        const std::string expected = R"(
module top(
    input clk,
    output reg [1:0] state
);
    wire [1:0] __xmr__u_ctrl_mode;
    ctrl u_ctrl(
        .__xmr__u_ctrl_mode(__xmr__u_ctrl_mode));
    always @(posedge clk) begin
        case ( __xmr__u_ctrl_mode)
            2'b00: state <= 2'b01;
            2'b01: state <= 2'b10;
            default: state <= 2'b00;
        endcase
    end
endmodule

module ctrl;
    output wire [1:0] __xmr__u_ctrl_mode;
    reg [1:0] mode;
    assign __xmr__u_ctrl_mode = mode;
endmodule
)";
        testXMRElimination(input, expected, "ctx_case");
    }

    SECTION("Generate block") {
        const std::string input    = R"(
module top(
    output wire [3:0] results
);
    genvar i;
    generate
        for (i = 0; i < 4; i = i + 1) begin : gen_block
            sub u_sub();
            assign results[i] = u_sub.data;
        end
    endgenerate
endmodule

module sub;
    reg data;
endmodule
)";
        const std::string testFile = "ctx_generate.sv";
        createTestFile(testFile, input);

        XMREliminateConfig config;
        config.modules = {"top"};

        auto result = xmrEliminate({testFile}, config);
        REQUIRE(result.success());

        cleanupTestFile(testFile);
    }
}
