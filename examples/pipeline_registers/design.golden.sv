// Pipeline Register Example
// This example demonstrates XMR elimination with pipeline registers

module top(
    input wire clk,
    input wire rst_n,
    output wire fast_data
);
    logic __xmr__u_sub_fast_signal;
    // Instantiate sub-module
    sub_module u_sub(
        .clk(clk),
        .rst_n(rst_n),
        .__xmr__u_sub_fast_signal(__xmr__u_sub_fast_signal)
    );
    
    // XMR reference to a fast signal
    // Pipeline registers will be added to meet timing
    assign fast_data = __xmr__u_sub_fast_signal;
    
endmodule

module sub_module(
    input wire clk,
    input wire rst_n,
    output wire __xmr__u_sub_fast_signal
);
    reg fast_signal;
    
    // Fast toggling signal
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            fast_signal <= 1'b0;
        else
            fast_signal <= ~fast_signal;
    end
    reg __xmr__u_sub_fast_signal_pipe_0;
    reg __xmr__u_sub_fast_signal_pipe_1;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            __xmr__u_sub_fast_signal_pipe_0 <= 1'h0;
            __xmr__u_sub_fast_signal_pipe_1 <= 1'h0;
        end else begin
            __xmr__u_sub_fast_signal_pipe_0 <= fast_signal;
            __xmr__u_sub_fast_signal_pipe_1 <= __xmr__u_sub_fast_signal_pipe_0;
        end
    end
    assign __xmr__u_sub_fast_signal = __xmr__u_sub_fast_signal_pipe_1;

    
endmodule

