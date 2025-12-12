/*
 * test_xmr_procedural.cpp - XMR elimination tests for procedural contexts
 *
 * Tests XMR elimination scenarios where the replaced signals are used
 * in procedural contexts (always blocks, DPI function calls, etc.)
 * These cases require 'logic' instead of 'wire' declarations.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// DPI Function Context Tests - Input Arguments (Read XMR)
//==============================================================================

TEST_CASE("XMR Elimination - DPI function input arguments", "[XMREliminate][DPI]") {
    const std::string input = R"(
module top(input wire clk);
    sub_module u_sub(.clk(clk));
    import "DPI-C" function void dpi_func(input bit data, input bit [15:0] value);
    always @(negedge clk) begin
        dpi_func(u_sub.data, u_sub.value);
    end
endmodule

module sub_module(input wire clk);
    reg data;
    reg [15:0] value;
    always @(posedge clk) begin
        data <= ~data;
        value <= value + 1;
    end
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic __xmr__u_sub_data;
    logic [15:0] __xmr__u_sub_value;
    sub_module u_sub(.clk(clk),
        .__xmr__u_sub_data(__xmr__u_sub_data),
        .__xmr__u_sub_value(__xmr__u_sub_value));
    import "DPI-C" function void dpi_func(input bit data, input bit [15:0] value);
    always @(negedge clk) begin
        dpi_func( __xmr__u_sub_data, __xmr__u_sub_value);
    end
endmodule

module sub_module(input wire clk,
    output wire __xmr__u_sub_data,
    output wire [15:0] __xmr__u_sub_value);
    reg data;
    reg [15:0] value;
    always @(posedge clk) begin
        data <= ~data;
        value <= value + 1;
    end
    assign __xmr__u_sub_data = data;
    assign __xmr__u_sub_value = value;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_input");
}

TEST_CASE("XMR Elimination - Mixed DPI input and local signals", "[XMREliminate][DPI]") {
    const std::string input = R"(
module top(input wire clk);
    reg [31:0] local_counter;
    sub_module u_sub(.clk(clk));
    import "DPI-C" function void dpi_mixed(input bit [31:0] local_val, input bit sub_data);
    always @(negedge clk) begin
        dpi_mixed(local_counter, u_sub.data);
    end
endmodule

module sub_module(input wire clk);
    reg data;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic __xmr__u_sub_data;
    reg [31:0] local_counter;
    sub_module u_sub(.clk(clk),
        .__xmr__u_sub_data(__xmr__u_sub_data));
    import "DPI-C" function void dpi_mixed(input bit [31:0] local_val, input bit sub_data);
    always @(negedge clk) begin
        dpi_mixed(local_counter, __xmr__u_sub_data);
    end
endmodule

module sub_module(input wire clk,
    output wire __xmr__u_sub_data);
    reg data;
    assign __xmr__u_sub_data = data;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_mixed");
}

TEST_CASE("XMR Elimination - Multiple DPI calls with XMRs", "[XMREliminate][DPI]") {
    const std::string input = R"(
module top(input wire clk);
    sub_a u_a(.clk(clk));
    sub_b u_b(.clk(clk));
    
    import "DPI-C" function void dpi_func_a(input bit data);
    import "DPI-C" function void dpi_func_b(input bit [7:0] value);
    
    always @(negedge clk) begin
        dpi_func_a(u_a.flag);
        dpi_func_b(u_b.counter);
    end
endmodule

module sub_a(input wire clk);
    reg flag;
endmodule

module sub_b(input wire clk);
    reg [7:0] counter;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic __xmr__u_a_flag;
    logic [7:0] __xmr__u_b_counter;
    sub_a u_a(.clk(clk),
        .__xmr__u_a_flag(__xmr__u_a_flag));
    sub_b u_b(.clk(clk),
        .__xmr__u_b_counter(__xmr__u_b_counter));
    
    import "DPI-C" function void dpi_func_a(input bit data);
    import "DPI-C" function void dpi_func_b(input bit [7:0] value);
    
    always @(negedge clk) begin
        dpi_func_a( __xmr__u_a_flag);
        dpi_func_b( __xmr__u_b_counter);
    end
endmodule

module sub_a(input wire clk,
    output wire __xmr__u_a_flag);
    reg flag;
    assign __xmr__u_a_flag = flag;
endmodule

module sub_b(input wire clk,
    output wire [7:0] __xmr__u_b_counter);
    reg [7:0] counter;
    assign __xmr__u_b_counter = counter;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_multiple");
}

//==============================================================================
// Always Block Context Tests (non-DPI)
//==============================================================================

TEST_CASE("XMR Elimination - XMR read in always_ff", "[XMREliminate][Procedural]") {
    const std::string input = R"(
module top(
    input wire clk,
    input wire rst_n,
    output reg result
);
    sub u_sub();
    always_ff @(posedge clk or negedge rst_n) begin
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
    input wire clk,
    input wire rst_n,
    output reg result
);
    logic __xmr__u_sub_data;
    sub u_sub(
        .__xmr__u_sub_data(__xmr__u_sub_data));
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            result <= 1'b0;
        else
            result <= __xmr__u_sub_data;
    end
endmodule

module sub( __xmr__u_sub_data);
    output wire __xmr__u_sub_data;
    reg data;
    assign __xmr__u_sub_data = data;
endmodule
)";

    testXMRElimination(input, expected, "test_always_ff");
}

TEST_CASE("XMR Elimination - XMR in always_comb", "[XMREliminate][Procedural]") {
    const std::string input = R"(
module top(
    output logic [7:0] result
);
    sub u_sub();
    always_comb begin
        result = u_sub.data + 8'h01;
    end
endmodule

module sub;
    reg [7:0] data;
endmodule
)";

    const std::string expected = R"(
module top(
    output logic [7:0] result
);
    logic [7:0] __xmr__u_sub_data;
    sub u_sub(
        .__xmr__u_sub_data(__xmr__u_sub_data));
    always_comb begin
        result = __xmr__u_sub_data + 8'h01;
    end
endmodule

module sub( __xmr__u_sub_data);
    output wire [7:0] __xmr__u_sub_data;
    reg [7:0] data;
    assign __xmr__u_sub_data = data;
endmodule
)";

    testXMRElimination(input, expected, "test_always_comb");
}

//==============================================================================
// Initial Block Context Tests
//==============================================================================

TEST_CASE("XMR Elimination - XMR in initial block", "[XMREliminate][Procedural]") {
    const std::string input = R"(
module top;
    sub u_sub();
    reg captured_value;
    
    initial begin
        #10;
        captured_value = u_sub.init_data;
    end
endmodule

module sub;
    reg init_data;
    initial init_data = 1'b1;
endmodule
)";

    const std::string expected = R"(
module top;
    logic __xmr__u_sub_init_data;
    sub u_sub(
        .__xmr__u_sub_init_data(__xmr__u_sub_init_data));
    reg captured_value;
    
    initial begin
        #10;
        captured_value = __xmr__u_sub_init_data;
    end
endmodule

module sub( __xmr__u_sub_init_data);
    output wire __xmr__u_sub_init_data;
    reg init_data;
    initial init_data = 1'b1;
    assign __xmr__u_sub_init_data = init_data;
endmodule
)";

    testXMRElimination(input, expected, "test_initial");
}

//==============================================================================
// Task Context Tests
//==============================================================================

TEST_CASE("XMR Elimination - XMR in task", "[XMREliminate][Procedural]") {
    const std::string input = R"(
module top(input wire clk);
    sub u_sub();
    reg [7:0] local_data;
    
    task automatic process_data;
        local_data = u_sub.value + 1;
    endtask
    
    always @(posedge clk) begin
        process_data();
    end
endmodule

module sub;
    reg [7:0] value;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic [7:0] __xmr__u_sub_value;
    sub u_sub(
        .__xmr__u_sub_value(__xmr__u_sub_value));
    reg [7:0] local_data;
    
    task automatic process_data;
        local_data = __xmr__u_sub_value + 1;
    endtask
    
    always @(posedge clk) begin
        process_data();
    end
endmodule

module sub( __xmr__u_sub_value);
    output wire [7:0] __xmr__u_sub_value;
    reg [7:0] value;
    assign __xmr__u_sub_value = value;
endmodule
)";

    testXMRElimination(input, expected, "test_task");
}

//==============================================================================
// Complex Combined Scenarios
//==============================================================================

TEST_CASE("XMR Elimination - XMR in both continuous and procedural", "[XMREliminate][Mixed]") {
    const std::string input = R"(
module top(
    input wire clk,
    output wire cont_out,
    output reg proc_out
);
    sub u_sub();
    
    // Continuous assignment - uses wire context
    assign cont_out = u_sub.sig_a;
    
    // Procedural assignment - uses logic context
    always @(posedge clk) begin
        proc_out <= u_sub.sig_b;
    end
endmodule

module sub;
    reg sig_a;
    reg sig_b;
endmodule
)";

    const std::string expected = R"(
module top(
    input wire clk,
    output wire cont_out,
    output reg proc_out
);
    logic __xmr__u_sub_sig_a;
    logic __xmr__u_sub_sig_b;
    sub u_sub(
        .__xmr__u_sub_sig_a(__xmr__u_sub_sig_a),
        .__xmr__u_sub_sig_b(__xmr__u_sub_sig_b));
    
    // Continuous assignment - uses wire context
    assign cont_out = __xmr__u_sub_sig_a;
    
    // Procedural assignment - uses logic context
    always @(posedge clk) begin
        proc_out <= __xmr__u_sub_sig_b;
    end
endmodule

module sub( __xmr__u_sub_sig_a, __xmr__u_sub_sig_b);
    output wire __xmr__u_sub_sig_a;
    output wire __xmr__u_sub_sig_b;
    reg sig_a;
    reg sig_b;
    assign __xmr__u_sub_sig_a = sig_a;
    assign __xmr__u_sub_sig_b = sig_b;
endmodule
)";

    testXMRElimination(input, expected, "test_mixed_context");
}

TEST_CASE("XMR Elimination - Deep hierarchy with DPI", "[XMREliminate][DPI][Complex]") {
    const std::string input = R"(
module top(input wire clk);
    level1 u_l1(.clk(clk));
    
    import "DPI-C" function void dpi_deep(input bit [31:0] deep_val);
    
    always @(negedge clk) begin
        dpi_deep(u_l1.u_l2.u_l3.counter);
    end
endmodule

module level1(input wire clk);
    level2 u_l2(.clk(clk));
endmodule

module level2(input wire clk);
    level3 u_l3(.clk(clk));
endmodule

module level3(input wire clk);
    reg [31:0] counter;
    always @(posedge clk) counter <= counter + 1;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic [31:0] __xmr__u_l1_u_l2_u_l3_counter;
    level1 u_l1(.clk(clk),
        .__xmr__u_l1_u_l2_u_l3_counter(__xmr__u_l1_u_l2_u_l3_counter));
    
    import "DPI-C" function void dpi_deep(input bit [31:0] deep_val);
    
    always @(negedge clk) begin
        dpi_deep( __xmr__u_l1_u_l2_u_l3_counter);
    end
endmodule

module level1(input wire clk,
    output wire [31:0] __xmr__u_l1_u_l2_u_l3_counter);
    level2 u_l2(.clk(clk),
        .__xmr__u_l1_u_l2_u_l3_counter(__xmr__u_l1_u_l2_u_l3_counter));
endmodule

module level2(input wire clk,
    output wire [31:0] __xmr__u_l1_u_l2_u_l3_counter);
    level3 u_l3(.clk(clk),
        .__xmr__u_l1_u_l2_u_l3_counter(__xmr__u_l1_u_l2_u_l3_counter));
endmodule

module level3(input wire clk,
    output wire [31:0] __xmr__u_l1_u_l2_u_l3_counter);
    reg [31:0] counter;
    always @(posedge clk) counter <= counter + 1;
    assign __xmr__u_l1_u_l2_u_l3_counter = counter;
endmodule
)";

    testXMRElimination(input, expected, "test_deep_dpi");
}

//==============================================================================
// DPI Function Context Tests - Output Arguments (Write XMR)
// NOTE: When XMR is used as DPI output argument, the DPI function writes to it.
// Current implementation treats it as a read (output from target module),
// which compiles but may not have correct runtime semantics.
// This test documents the current behavior.
//==============================================================================

TEST_CASE("XMR Elimination - DPI function output argument", "[XMREliminate][DPI][Output]") {
    // This test case covers the scenario where XMR is used as DPI output argument
    // The DPI function writes to the XMR (c_function writes to u_sub.v)
    const std::string input = R"(
module top(
    input wire clk
);

import "DPI-C" function void c_function(output bit a);

sub u_sub ();

always@(negedge clk) begin
    c_function(u_sub.v);
end

endmodule


module sub(
    output reg v
);

wire v1;

assign v1 = v;

reg [7:0] count;

always @(posedge v1) begin
    count <= count + 1;
end

endmodule
)";

    // NOTE: Current behavior creates output port from sub (treating as read)
    // For true DPI output semantics, we'd need input port to sub (write direction)
    // But the generated code compiles successfully
    const std::string expected = R"(
module top(
    input wire clk
);
    logic __xmr__u_sub_v;

import "DPI-C" function void c_function(output bit a);

sub u_sub (
        .__xmr__u_sub_v(__xmr__u_sub_v));

always@(negedge clk) begin
    c_function( __xmr__u_sub_v);
end

endmodule


module sub(
    output reg v,
    input wire __xmr__u_sub_v
);

wire v1;

assign v1 = v;

reg [7:0] count;

always @(posedge v1) begin
    count <= count + 1;
end
    assign v = __xmr__u_sub_v;

endmodule
)";

    testXMRElimination(input, expected, "test_dpi_output_arg");
}

TEST_CASE("XMR Elimination - DPI with mixed input and output XMR args", "[XMREliminate][DPI][Output]") {
    // DPI function with both input and output XMR arguments
    const std::string input = R"(
module top(input wire clk);
    sub u_sub();
    import "DPI-C" function void dpi_inout(input bit in_val, output bit out_val);
    always @(negedge clk) begin
        dpi_inout(u_sub.data_in, u_sub.data_out);
    end
endmodule

module sub;
    reg data_in;
    reg data_out;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic __xmr__u_sub_data_in;
    logic __xmr__u_sub_data_out;
    sub u_sub(
        .__xmr__u_sub_data_in(__xmr__u_sub_data_in),
        .__xmr__u_sub_data_out(__xmr__u_sub_data_out));
    import "DPI-C" function void dpi_inout(input bit in_val, output bit out_val);
    always @(negedge clk) begin
        dpi_inout( __xmr__u_sub_data_in, __xmr__u_sub_data_out);
    end
endmodule

module sub( __xmr__u_sub_data_in, __xmr__u_sub_data_out);
    output wire __xmr__u_sub_data_in;
    input wire __xmr__u_sub_data_out;
    reg data_in;
    reg data_out;
    assign __xmr__u_sub_data_in = data_in;
    assign data_out = __xmr__u_sub_data_out;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_mixed_args");
}

TEST_CASE("XMR Elimination - DPI output to internal reg (not module port)", "[XMREliminate][DPI][Output]") {
    // DPI function writes to internal reg (not output port) via XMR
    // This is the common case where a submodule internal state is modified
    const std::string input = R"(
module top(input wire clk);
    sub u_sub(.clk(clk));
    import "DPI-C" function void dpi_write(output bit [7:0] value);
    always @(negedge clk) begin
        dpi_write(u_sub.internal_reg);
    end
endmodule

module sub(input wire clk);
    reg [7:0] internal_reg;
    wire [7:0] used_value;
    assign used_value = internal_reg + 8'h1;
endmodule
)";

    // The internal_reg is driven by DPI output, so it becomes an input to sub
    // and an assign drives internal_reg from the port
    const std::string expected = R"(
module top(input wire clk);
    logic [7:0] __xmr__u_sub_internal_reg;
    sub u_sub(.clk(clk),
        .__xmr__u_sub_internal_reg(__xmr__u_sub_internal_reg));
    import "DPI-C" function void dpi_write(output bit [7:0] value);
    always @(negedge clk) begin
        dpi_write( __xmr__u_sub_internal_reg);
    end
endmodule

module sub(input wire clk,
    input wire [7:0] __xmr__u_sub_internal_reg);
    reg [7:0] internal_reg;
    wire [7:0] used_value;
    assign used_value = internal_reg + 8'h1;
    assign internal_reg = __xmr__u_sub_internal_reg;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_output_internal_reg");
}

TEST_CASE("XMR Elimination - DPI inout argument", "[XMREliminate][DPI][Output]") {
    // DPI function with inout argument (both reads and writes)
    // Treated same as output since the function can write to it
    const std::string input = R"(
module top(input wire clk);
    sub u_sub();
    import "DPI-C" function void dpi_modify(inout bit [3:0] data);
    always @(negedge clk) begin
        dpi_modify(u_sub.bidirectional);
    end
endmodule

module sub;
    reg [3:0] bidirectional;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic [3:0] __xmr__u_sub_bidirectional;
    sub u_sub(
        .__xmr__u_sub_bidirectional(__xmr__u_sub_bidirectional));
    import "DPI-C" function void dpi_modify(inout bit [3:0] data);
    always @(negedge clk) begin
        dpi_modify( __xmr__u_sub_bidirectional);
    end
endmodule

module sub( __xmr__u_sub_bidirectional);
    input wire [3:0] __xmr__u_sub_bidirectional;
    reg [3:0] bidirectional;
    assign bidirectional = __xmr__u_sub_bidirectional;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_inout_arg");
}

TEST_CASE("XMR Elimination - Multiple DPI outputs to same submodule", "[XMREliminate][DPI][Output]") {
    // Multiple DPI calls writing to different signals in same submodule
    const std::string input = R"(
module top(input wire clk);
    sub u_sub();
    import "DPI-C" function void dpi_write_a(output bit flag);
    import "DPI-C" function void dpi_write_b(output bit [15:0] counter);
    always @(negedge clk) begin
        dpi_write_a(u_sub.ready);
        dpi_write_b(u_sub.count);
    end
endmodule

module sub;
    reg ready;
    reg [15:0] count;
endmodule
)";

    const std::string expected = R"(
module top(input wire clk);
    logic __xmr__u_sub_ready;
    logic [15:0] __xmr__u_sub_count;
    sub u_sub(
        .__xmr__u_sub_ready(__xmr__u_sub_ready),
        .__xmr__u_sub_count(__xmr__u_sub_count));
    import "DPI-C" function void dpi_write_a(output bit flag);
    import "DPI-C" function void dpi_write_b(output bit [15:0] counter);
    always @(negedge clk) begin
        dpi_write_a( __xmr__u_sub_ready);
        dpi_write_b( __xmr__u_sub_count);
    end
endmodule

module sub( __xmr__u_sub_ready, __xmr__u_sub_count);
    input wire __xmr__u_sub_ready;
    input wire [15:0] __xmr__u_sub_count;
    reg ready;
    reg [15:0] count;
    assign ready = __xmr__u_sub_ready;
    assign count = __xmr__u_sub_count;
endmodule
)";

    testXMRElimination(input, expected, "test_dpi_multi_output");
}
