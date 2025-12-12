// DPI Function XMR Example
// This example demonstrates XMR elimination when XMRs are used
// as output arguments in DPI function calls inside procedural blocks.
// The challenge: output arguments require writable variables (logic/reg),
// not wires.

module top(
    input wire clk
);

// Internal signals
reg [31:0] counter;
reg valid;

initial begin
    counter = 0;
    valid = 0;
end

always @(posedge clk) begin
    counter <= counter + 1;
    valid <= (counter[3:0] == 4'hF);
end

// Instantiate sub-module
sub_module u_sub(
    .clk(clk)
);

// Declare DPI-C function with output arguments
import "DPI-C" function void dpi_process_signals(
    input bit in_clk,
    input bit [31:0] in_counter,
    input bit in_valid,
    input bit in_sub_data,
    input bit [15:0] in_sub_value,
    output bit [31:0] out_result,
    output bit out_done
);

// Wire/logic for DPI output
logic [31:0] dpi_result;
logic dpi_done;

// Call DPI function with XMRs as both input and output arguments
// The XMR references (u_sub.data, u_sub.value) will be replaced by
// XMR elimination. The challenge is that the replaced signals must
// be logic (not wire) to work as DPI output arguments.
always @(negedge clk) begin
    dpi_process_signals(clk, counter, valid, u_sub.data, u_sub.value, dpi_result, dpi_done);
end

endmodule

module sub_module(
    input wire clk
);
    reg data;
    reg [15:0] value;
    
    initial begin
        data = 0;
        value = 16'h0;
    end
    
    always @(posedge clk) begin
        data <= ~data;
        value <= value + 1;
    end
    
endmodule
