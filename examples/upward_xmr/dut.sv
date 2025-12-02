module dut(
    input wire clock,
    input wire reset
);

    reg [3:0] counter;

    always_ff @(posedge clock or posedge reset) begin
        if (reset) begin
            counter <= 4'b0;
        end else begin
            counter <= counter + 1;
        end
    end

    reg another_reg;
    always_ff @(posedge clock or posedge reset) begin
        if (reset) begin
            another_reg <= 1'b0;
        end else begin
            another_reg <= ~another_reg;
        end
    end

endmodule