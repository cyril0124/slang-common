/*
 * test_xmr_edge_cases.cpp - Edge case XMR elimination tests
 *
 * Tests edge cases including empty modules, special characters,
 * array handling, and unusual scenarios.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// Edge Case Tests
//==============================================================================

TEST_CASE("XMR Elimination - Empty module", "[XMREliminate][EdgeCase]") {
    const std::string testFile = "test_empty_module.sv";

    createTestFile(testFile, R"(
module empty_module;
endmodule
    )");

    XMREliminateConfig config;
    config.modules = {"empty_module"};

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.eliminatedXMRs.empty());

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Same signal name in different instances", "[XMREliminate][EdgeCase]") {
    const std::string input = R"(
module top;
    sub_a u_a();
    sub_b u_b();
    wire out1, out2;
    assign out1 = u_a.data;
    assign out2 = u_b.data;
endmodule

module sub_a;
    reg data;
endmodule

module sub_b;
    reg data;
endmodule
)";

    const std::string expected = R"(
module top;
    logic __xmr__u_a_data;
    logic __xmr__u_b_data;
    sub_a u_a(
        .__xmr__u_a_data(__xmr__u_a_data));
    sub_b u_b(
        .__xmr__u_b_data(__xmr__u_b_data));
    wire out1, out2;
    assign out1 = __xmr__u_a_data;
    assign out2 = __xmr__u_b_data;
endmodule

module sub_a( __xmr__u_a_data);
    output wire __xmr__u_a_data;
    reg data;
    assign __xmr__u_a_data = data;
endmodule

module sub_b( __xmr__u_b_data);
    output wire __xmr__u_b_data;
    reg data;
    assign __xmr__u_b_data = data;
endmodule
)";

    testXMRElimination(input, expected, "same_sig_name");
}

TEST_CASE("XMR Elimination - Very deep hierarchy", "[XMREliminate][EdgeCase]") {
    const std::string input = R"(
module top;
    level1 u_l1();
    wire result;
    assign result = u_l1.u_l2.u_l3.u_l4.u_l5.deep_data;
endmodule

module level1;
    level2 u_l2();
endmodule

module level2;
    level3 u_l3();
endmodule

module level3;
    level4 u_l4();
endmodule

module level4;
    level5 u_l5();
endmodule

module level5;
    reg deep_data;
endmodule
)";

    const std::string expected = R"(
module top;
    logic __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    level1 u_l1(
        .__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data(__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data));
    wire result;
    assign result = __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
endmodule

module level1( __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data);
    output wire __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    level2 u_l2(
        .__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data(__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data));
endmodule

module level2( __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data);
    output wire __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    level3 u_l3(
        .__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data(__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data));
endmodule

module level3( __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data);
    output wire __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    level4 u_l4(
        .__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data(__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data));
endmodule

module level4( __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data);
    output wire __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    level5 u_l5(
        .__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data(__xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data));
endmodule

module level5( __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data);
    output wire __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data;
    reg deep_data;
    assign __xmr__u_l1_u_l2_u_l3_u_l4_u_l5_deep_data = deep_data;
endmodule
)";

    testXMRElimination(input, expected, "very_deep");
}

TEST_CASE("XMR Elimination - XMR to array element", "[XMREliminate][EdgeCase]") {
    const std::string testFile = "test_xmr_array.sv";

    createTestFile(testFile, R"(
module top;
    sub_module u_sub();
    wire [7:0] byte_out;
    assign byte_out = u_sub.mem_array[3];
endmodule

module sub_module;
    reg [7:0] mem_array [0:7];
    initial begin
        mem_array[3] = 8'hAB;
    end
endmodule
    )");

    XMREliminateConfig config;
    config.modules = {"top"};

    REQUIRE_NOTHROW(xmrEliminate({testFile}, config));

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Multiple references to same target", "[XMREliminate][EdgeCase]") {
    const std::string input = R"(
module top;
    sub u_sub();
    wire out1, out2, out3;
    assign out1 = u_sub.shared_signal;
    assign out2 = u_sub.shared_signal;
    assign out3 = u_sub.shared_signal & 1'b1;
endmodule

module sub;
    reg shared_signal;
endmodule
)";

    const std::string expected = R"(
module top;
    logic __xmr__u_sub_shared_signal;
    sub u_sub(
        .__xmr__u_sub_shared_signal(__xmr__u_sub_shared_signal));
    wire out1, out2, out3;
    assign out1 = __xmr__u_sub_shared_signal;
    assign out2 = __xmr__u_sub_shared_signal;
    assign out3 = __xmr__u_sub_shared_signal & 1'b1;
endmodule

module sub( __xmr__u_sub_shared_signal);
    output wire __xmr__u_sub_shared_signal;
    reg shared_signal;
    assign __xmr__u_sub_shared_signal = shared_signal;
endmodule
)";

    testXMRElimination(input, expected, "multi_ref_same_target");
}

TEST_CASE("XMR Elimination - Signal with special characters in path", "[XMREliminate][EdgeCase]") {
    const std::string input = R"(
module top;
    sub u_sub_special();
    wire out;
    assign out = u_sub_special.my_signal_123;
endmodule

module sub;
    reg my_signal_123;
endmodule
)";

    const std::string expected = R"(
module top;
    logic __xmr__u_sub_special_my_signal_123;
    sub u_sub_special(
        .__xmr__u_sub_special_my_signal_123(__xmr__u_sub_special_my_signal_123));
    wire out;
    assign out = __xmr__u_sub_special_my_signal_123;
endmodule

module sub( __xmr__u_sub_special_my_signal_123);
    output wire __xmr__u_sub_special_my_signal_123;
    reg my_signal_123;
    assign __xmr__u_sub_special_my_signal_123 = my_signal_123;
endmodule
)";

    testXMRElimination(input, expected, "special_chars");
}

//==============================================================================
// Array and Index Handling Tests
//==============================================================================

TEST_CASE("XMR Elimination - Array and Index Handling", "[XMREliminate][Array]") {
    SECTION("Bit select on vector") {
        const std::string input = R"(
module top(
    output wire result
);
    sub u_sub();
    assign result = u_sub.data[0];
endmodule

module sub;
    reg [7:0] data;
endmodule
)";

        const std::string expected = R"(
module top(
    output wire result
);
    logic [7:0] __xmr__u_sub_data;
    sub u_sub(
        .__xmr__u_sub_data(__xmr__u_sub_data));
    assign result = __xmr__u_sub_data[0];
endmodule

module sub( __xmr__u_sub_data);
    output wire [7:0] __xmr__u_sub_data;
    reg [7:0] data;
    assign __xmr__u_sub_data = data;
endmodule
)";

        testXMRElimination(input, expected, "bit_select");
    }

    SECTION("Range select on vector") {
        const std::string input = R"(
module top(
    output wire [3:0] result
);
    sub u_sub();
    assign result = u_sub.data[7:4];
endmodule

module sub;
    reg [7:0] data;
endmodule
)";

        const std::string expected = R"(
module top(
    output wire [3:0] result
);
    logic [7:0] __xmr__u_sub_data;
    sub u_sub(
        .__xmr__u_sub_data(__xmr__u_sub_data));
    assign result = __xmr__u_sub_data[7:4];
endmodule

module sub( __xmr__u_sub_data);
    output wire [7:0] __xmr__u_sub_data;
    reg [7:0] data;
    assign __xmr__u_sub_data = data;
endmodule
)";

        testXMRElimination(input, expected, "range_select");
    }

    SECTION("Multiple array indices and complex expressions") {
        const std::string input = R"(
module top(
    input [2:0] idx,
    input [2:0] row,
    output wire [7:0] out_1d,
    output wire [7:0] out_2d,
    output wire [7:0] out_3d,
    output wire [7:0] out_var,
    output wire [7:0] out_expr,
    output wire [3:0] out_mixed,
    output wire [7:0] out_multi1,
    output wire [7:0] out_multi2,
    output wire [7:0] out_multi3
);
    sub u_sub();
    
    assign out_1d = u_sub.arr1d[3];
    assign out_2d = u_sub.arr2d[2][3];
    assign out_3d = u_sub.arr3d[1][2][3];
    assign out_var = u_sub.arr1d[idx];
    assign out_expr = u_sub.arr1d[idx + row * 2];
    assign out_mixed = u_sub.matrix[row][5:2];
    assign out_multi1 = u_sub.data[0];
    assign out_multi2 = u_sub.data[1];
    assign out_multi3 = u_sub.data[7];
endmodule

module sub;
    reg [7:0] arr1d [0:7];
    reg [7:0] arr2d [0:3][0:7];
    reg [7:0] arr3d [0:3][0:3][0:7];
    reg [7:0] matrix [0:7];
    reg [7:0] data [0:7];
endmodule
)";

        const std::string testFile = "test_array_comprehensive.sv";
        createTestFile(testFile, input);

        XMREliminateConfig config;
        config.modules = {"top"};

        auto result = xmrEliminate({testFile}, config);
        REQUIRE(result.success());

        const std::string &output = result.modifiedFiles[0];

        REQUIRE(output.find("__xmr__u_sub_arr1d") != std::string::npos);
        REQUIRE(output.find("__xmr__u_sub_arr2d") != std::string::npos);
        REQUIRE(output.find("__xmr__u_sub_arr3d") != std::string::npos);
        REQUIRE(output.find("__xmr__u_sub_matrix") != std::string::npos);
        REQUIRE(output.find("__xmr__u_sub_data") != std::string::npos);

        cleanupTestFile(testFile);
    }

    SECTION("Part select with +:/:-:") {
        const std::string input = R"(
module top(
    input [2:0] base,
    output wire [3:0] result1,
    output wire [3:0] result2
);
    sub u_sub();
    assign result1 = u_sub.data[base+:4];
    assign result2 = u_sub.data[base-:4];
endmodule

module sub;
    reg [15:0] data;
endmodule
)";

        const std::string testFile = "test_part_select.sv";
        createTestFile(testFile, input);

        XMREliminateConfig config;
        config.modules = {"top"};

        auto result = xmrEliminate({testFile}, config);
        REQUIRE(result.success());

        const std::string &output = result.modifiedFiles[0];
        REQUIRE(output.find("__xmr__u_sub_data") != std::string::npos);
        REQUIRE(output.find("+:") != std::string::npos);
        REQUIRE(output.find("-:") != std::string::npos);

        cleanupTestFile(testFile);
    }

    SECTION("Packed array") {
        const std::string testFile = "test_packed_array.sv";

        createTestFile(testFile, R"(
module top(
    output wire [7:0] result
);
    sub u_sub();
    assign result = u_sub.packed_data[3];
endmodule

module sub;
    reg [3:0][7:0] packed_data;
endmodule
        )");

        XMREliminateConfig config;
        config.modules = {"top"};

        auto result = xmrEliminate({testFile}, config);
        REQUIRE(result.success());
        REQUIRE(result.modifiedFiles[0].find("__xmr__u_sub_packed_data") != std::string::npos);

        cleanupTestFile(testFile);
    }

    SECTION("Struct member access") {
        const std::string testFile = "test_struct_xmr.sv";

        createTestFile(testFile, R"(
module top(
    output wire [7:0] result
);
    sub u_sub();
    assign result = u_sub.cfg.field_a;
endmodule

module sub;
    typedef struct packed {
        logic [7:0] field_a;
        logic [7:0] field_b;
    } cfg_t;
    cfg_t cfg;
endmodule
        )");

        XMREliminateConfig config;
        config.modules = {"top"};

        REQUIRE_NOTHROW(xmrEliminate({testFile}, config));

        cleanupTestFile(testFile);
    }
}
