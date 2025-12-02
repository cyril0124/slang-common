// Simple XMR Example
// This example demonstrates XMR elimination on a simple hierarchy

module top(
    input wire clk,
    input wire rst_n,
    output wire result
);
    // Instantiate sub-module
    sub_module u_sub(
        .clk(clk),
        .rst_n(rst_n)
    );
    
    // XMR reference - this should be eliminated
    assign result = u_sub.internal_signal;
    
endmodule

module sub_module(
    input wire clk,
    input wire rst_n
);
    reg internal_signal;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            internal_signal <= 1'b0;
        else
            internal_signal <= ~internal_signal;
    end
    
endmodule
