// Multi XMR Example
// This example demonstrates XMR elimination with multiple XMR references
// and array/partial select access patterns

module top(
    input wire clk,
    input wire rst_n,
    output wire [7:0] data_out,
    output wire status_bit,
    output wire [3:0] partial_data
);
    // Instantiate sub-module
    sub_module u_sub(
        .clk(clk),
        .rst_n(rst_n)
    );
    
    // Multiple XMR references to the same signal
    assign data_out = u_sub.counter;
    
    // XMR with partial select (bit selection)
    assign status_bit = u_sub.counter[7];
    
    // XMR with range select
    assign partial_data = u_sub.counter[3:0];
    
endmodule

module sub_module(
    input wire clk,
    input wire rst_n
);
    reg [7:0] counter;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            counter <= 8'h00;
        else
            counter <= counter + 8'h01;
    end
    
endmodule
