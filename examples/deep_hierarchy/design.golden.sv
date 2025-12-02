// Deep Hierarchy XMR Example
// This example demonstrates XMR elimination across multiple hierarchy levels

module top(
    input wire clk,
    input wire rst_n,
    output wire [7:0] data_out
);
    wire [7:0] __xmr__u_mid_u_bottom_counter_value;
    // Instantiate mid-level module
    mid_module u_mid(
        .clk(clk),
        .rst_n(rst_n),
        .__xmr__u_mid_u_bottom_counter_value(__xmr__u_mid_u_bottom_counter_value)
    );
    
    // XMR reference across 2 levels of hierarchy
    assign data_out = __xmr__u_mid_u_bottom_counter_value;
    
endmodule

module mid_module(
    input wire clk,
    input wire rst_n,
    output wire [7:0] __xmr__u_mid_u_bottom_counter_value
);
    wire [7:0] __xmr__u_bottom_counter_value;
    // Instantiate bottom-level module
    bottom_module u_bottom(
        .clk(clk),
        .rst_n(rst_n),
        .__xmr__u_bottom_counter_value(__xmr__u_bottom_counter_value),
        .__xmr__u_mid_u_bottom_counter_value(__xmr__u_mid_u_bottom_counter_value)
    );
    
    // Some mid-level logic
    wire mid_signal;
    assign mid_signal = __xmr__u_bottom_counter_value[0];
    
endmodule

module bottom_module(
    input wire clk,
    input wire rst_n,
    output wire [7:0] __xmr__u_bottom_counter_value,
    output wire [7:0] __xmr__u_mid_u_bottom_counter_value
);
    reg [7:0] counter_value;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            counter_value <= 8'h00;
        else
            counter_value <= counter_value + 8'h01;
    end
    assign __xmr__u_bottom_counter_value = counter_value;
    assign __xmr__u_mid_u_bottom_counter_value = counter_value;
    
endmodule

