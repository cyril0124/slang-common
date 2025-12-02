
module tb_top;

    logic clock;
    logic reset;

    dut uut (
        .clock(clock),
        .reset(reset)
    );

    initial begin
        clock = 0;
        forever #5 clock = ~clock;
    end

    initial begin
        reset = 1;
        #15;
        reset = 0;
        #100;
        $finish;
    end

    others other_inst ();

endmodule