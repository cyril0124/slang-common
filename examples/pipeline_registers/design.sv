// Pipeline Register Example
// This example demonstrates XMR elimination with pipeline registers

module top(
    input wire clk,
    input wire rst_n,
    output wire fast_data
);
    // Instantiate sub-module
    sub_module u_sub(
        .clk(clk),
        .rst_n(rst_n)
    );
    
    // XMR reference to a fast signal
    // Pipeline registers will be added to meet timing
    assign fast_data = u_sub.fast_signal;
    
endmodule

module sub_module(
    input wire clk,
    input wire rst_n
);
    reg fast_signal;
    
    // Fast toggling signal
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            fast_signal <= 1'b0;
        else
            fast_signal <= ~fast_signal;
    end
    
endmodule
