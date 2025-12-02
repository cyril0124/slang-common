// Deep Hierarchy XMR Example
// This example demonstrates XMR elimination across multiple hierarchy levels

module top(
    input wire clk,
    input wire rst_n,
    output wire [7:0] data_out
);
    // Instantiate mid-level module
    mid_module u_mid(
        .clk(clk),
        .rst_n(rst_n)
    );
    
    // XMR reference across 2 levels of hierarchy
    assign data_out = u_mid.u_bottom.counter_value;
    
endmodule

module mid_module(
    input wire clk,
    input wire rst_n
);
    // Instantiate bottom-level module
    bottom_module u_bottom(
        .clk(clk),
        .rst_n(rst_n)
    );
    
    // Some mid-level logic
    wire mid_signal;
    assign mid_signal = u_bottom.counter_value[0];
    
endmodule

module bottom_module(
    input wire clk,
    input wire rst_n
);
    reg [7:0] counter_value;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            counter_value <= 8'h00;
        else
            counter_value <= counter_value + 8'h01;
    end
    
endmodule
