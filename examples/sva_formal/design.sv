/**
 * @file design.sv
 * @brief Formal Verification Testbench Example with SVA using XMR
 *
 * This example demonstrates XMR elimination in a formal verification context:
 * - A testbench (tb_formal) contains SVA properties/assertions
 * - All assertions use XMR to access internal signals of the DUT
 * - The DUT has a multi-level hierarchy with various submodules
 *
 * After XMR elimination, all hierarchical references in the SVA assertions
 * are converted to explicit port connections, making the design synthesizable
 * and compatible with formal verification tools.
 */

// =============================================================================
// Formal Verification Testbench with SVA Assertions
// =============================================================================
module tb_formal(
    input clk,
    input rst_n
);
    // Instantiate DUT
    dut u_dut(.clk(clk), .rst_n(rst_n));

    // =========================================================================
    // SVA Properties using XMR to access DUT internal signals
    // =========================================================================

    // Property 1: FIFO should not overflow
    // Access fifo_ctrl.wr_ptr and fifo_ctrl.rd_ptr via XMR
    property p_fifo_no_overflow;
        @(posedge clk) disable iff (!rst_n)
        (u_dut.u_fifo.wr_ptr - u_dut.u_fifo.rd_ptr) <= 4'd8;
    endproperty
    assert property (p_fifo_no_overflow)
        else $error("FIFO overflow detected!");

    // Property 2: When valid is high, data should be stable next cycle
    property p_data_stable_when_valid;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_proc.valid |=> $stable(u_dut.u_proc.data_out);
    endproperty
    assert property (p_data_stable_when_valid)
        else $error("Data not stable when valid!");

    // Property 3: FSM should never be in illegal state (only 3 valid states)
    property p_fsm_legal_state;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_ctrl.state inside {2'b00, 2'b01, 2'b10};
    endproperty
    assert property (p_fsm_legal_state)
        else $error("FSM in illegal state!");

    // Property 4: Request should be acknowledged within 2 cycles
    property p_req_ack_handshake;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_ctrl.req |-> ##[1:2] u_dut.u_ctrl.ack;
    endproperty
    assert property (p_req_ack_handshake)
        else $error("Handshake timeout!");

    // Property 5: Counter should increment when enabled
    property p_counter_increment;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_proc.enable |=> (u_dut.u_proc.counter == $past(u_dut.u_proc.counter) + 1);
    endproperty
    assert property (p_counter_increment)
        else $error("Counter did not increment when enabled!");

    // Cover properties for functional coverage
    cover property (@(posedge clk) u_dut.u_fifo.full);
    cover property (@(posedge clk) u_dut.u_fifo.empty);
    cover property (@(posedge clk) u_dut.u_ctrl.state == 2'b10);

endmodule

// =============================================================================
// DUT - Top Level Design Under Test
// =============================================================================
module dut(
    input clk,
    input rst_n
);
    // Submodule instances
    controller u_ctrl(.clk(clk), .rst_n(rst_n));
    fifo_simple u_fifo(.clk(clk), .rst_n(rst_n));
    processor u_proc(.clk(clk), .rst_n(rst_n));
endmodule

// =============================================================================
// Controller FSM Module
// =============================================================================
module controller(
    input clk,
    input rst_n
);
    reg [1:0] state;
    reg req;
    reg ack;

    // Simple FSM: IDLE -> ACTIVE -> DONE -> IDLE
    localparam IDLE   = 2'b00;
    localparam ACTIVE = 2'b01;
    localparam DONE   = 2'b10;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            req <= 1'b0;
            ack <= 1'b0;
        end else begin
            case (state)
                IDLE: begin
                    state <= ACTIVE;
                    req <= 1'b1;
                end
                ACTIVE: begin
                    state <= DONE;
                    ack <= 1'b1;
                end
                DONE: begin
                    state <= IDLE;
                    req <= 1'b0;
                    ack <= 1'b0;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule

// =============================================================================
// Simple FIFO Module
// =============================================================================
module fifo_simple(
    input clk,
    input rst_n
);
    reg [3:0] wr_ptr;
    reg [3:0] rd_ptr;
    reg full;
    reg empty;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr <= 4'd0;
            rd_ptr <= 4'd0;
            full <= 1'b0;
            empty <= 1'b1;
        end else begin
            // Simple pointer logic
            wr_ptr <= wr_ptr + 4'd1;
            rd_ptr <= rd_ptr + 4'd1;
            full <= (wr_ptr - rd_ptr) >= 4'd7;
            empty <= (wr_ptr == rd_ptr);
        end
    end
endmodule

// =============================================================================
// Data Processor Module
// =============================================================================
module processor(
    input clk,
    input rst_n
);
    reg valid;
    reg enable;
    reg [7:0] data_out;
    reg [7:0] counter;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid <= 1'b0;
            enable <= 1'b0;
            data_out <= 8'd0;
            counter <= 8'd0;
        end else begin
            valid <= ~valid;
            enable <= valid;
            if (enable)
                counter <= counter + 8'd1;
            if (valid)
                data_out <= counter;
        end
    end
endmodule
