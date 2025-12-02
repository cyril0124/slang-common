// Simple XMR Example
// This example demonstrates XMR elimination on a simple hierarchy

module top(
    input wire clk,
    input wire rst_n,
    output wire result
);
    wire __xmr__u_sub_internal_signal;
    // Instantiate sub-module
    sub_module u_sub(
        .clk(clk),
        .rst_n(rst_n),
        .__xmr__u_sub_internal_signal(__xmr__u_sub_internal_signal)
    );
    
    // XMR reference - this should be eliminated
    assign result = __xmr__u_sub_internal_signal;
    
endmodule

module sub_module(
    input wire clk,
    input wire rst_n,
    output wire __xmr__u_sub_internal_signal
);
    reg internal_signal;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            internal_signal <= 1'b0;
        else
            internal_signal <= ~internal_signal;
    end
    assign __xmr__u_sub_internal_signal = internal_signal;
    
endmodule

