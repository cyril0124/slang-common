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
    wire [3:0] __xmr__u_dut_u_fifo_wr_ptr;
    wire [3:0] __xmr__u_dut_u_fifo_rd_ptr;
    wire __xmr__u_dut_u_proc_valid;
    wire [7:0] __xmr__u_dut_u_proc_data_out;
    wire [1:0] __xmr__u_dut_u_ctrl_state;
    wire __xmr__u_dut_u_ctrl_req;
    wire __xmr__u_dut_u_ctrl_ack;
    wire __xmr__u_dut_u_proc_enable;
    wire [7:0] __xmr__u_dut_u_proc_counter;
    wire __xmr__u_dut_u_fifo_full;
    wire __xmr__u_dut_u_fifo_empty;
    // Instantiate DUT
    dut u_dut(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_fifo_wr_ptr(__xmr__u_dut_u_fifo_wr_ptr),
        .__xmr__u_dut_u_fifo_rd_ptr(__xmr__u_dut_u_fifo_rd_ptr),
        .__xmr__u_dut_u_proc_valid(__xmr__u_dut_u_proc_valid),
        .__xmr__u_dut_u_proc_data_out(__xmr__u_dut_u_proc_data_out),
        .__xmr__u_dut_u_ctrl_state(__xmr__u_dut_u_ctrl_state),
        .__xmr__u_dut_u_ctrl_req(__xmr__u_dut_u_ctrl_req),
        .__xmr__u_dut_u_ctrl_ack(__xmr__u_dut_u_ctrl_ack),
        .__xmr__u_dut_u_proc_enable(__xmr__u_dut_u_proc_enable),
        .__xmr__u_dut_u_proc_counter(__xmr__u_dut_u_proc_counter),
        .__xmr__u_dut_u_fifo_full(__xmr__u_dut_u_fifo_full),
        .__xmr__u_dut_u_fifo_empty(__xmr__u_dut_u_fifo_empty));

    // =========================================================================
    // SVA Properties using XMR to access DUT internal signals
    // =========================================================================

    // Property 1: FIFO should not overflow
    // Access fifo_ctrl.wr_ptr and fifo_ctrl.rd_ptr via XMR
    property p_fifo_no_overflow;
        @(posedge clk) disable iff (!rst_n)
        ( __xmr__u_dut_u_fifo_wr_ptr - __xmr__u_dut_u_fifo_rd_ptr) <= 4'd8;
    endproperty
    assert property (p_fifo_no_overflow)
        else $error("FIFO overflow detected!");

    // Property 2: When valid is high, data should be stable next cycle
    property p_data_stable_when_valid;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_proc_valid |=> $stable( __xmr__u_dut_u_proc_data_out);
    endproperty
    assert property (p_data_stable_when_valid)
        else $error("Data not stable when valid!");

    // Property 3: FSM should never be in illegal state (only 3 valid states)
    property p_fsm_legal_state;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_ctrl_state inside {2'b00, 2'b01, 2'b10};
    endproperty
    assert property (p_fsm_legal_state)
        else $error("FSM in illegal state!");

    // Property 4: Request should be acknowledged within 2 cycles
    property p_req_ack_handshake;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_ctrl_req |-> ##[1:2] __xmr__u_dut_u_ctrl_ack;
    endproperty
    assert property (p_req_ack_handshake)
        else $error("Handshake timeout!");

    // Property 5: Counter should increment when enabled
    property p_counter_increment;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_proc_enable |=> ( __xmr__u_dut_u_proc_counter == $past( __xmr__u_dut_u_proc_counter) + 1);
    endproperty
    assert property (p_counter_increment)
        else $error("Counter did not increment when enabled!");

    // Cover properties for functional coverage
    cover property (@(posedge clk) __xmr__u_dut_u_fifo_full);
    cover property (@(posedge clk) __xmr__u_dut_u_fifo_empty);
    cover property (@(posedge clk) __xmr__u_dut_u_ctrl_state == 2'b10);

endmodule

// =============================================================================
// DUT - Top Level Design Under Test
// =============================================================================
module dut(
    input clk,
    input rst_n,
    output wire [3:0] __xmr__u_dut_u_fifo_wr_ptr,
    output wire [3:0] __xmr__u_dut_u_fifo_rd_ptr,
    output wire __xmr__u_dut_u_proc_valid,
    output wire [7:0] __xmr__u_dut_u_proc_data_out,
    output wire [1:0] __xmr__u_dut_u_ctrl_state,
    output wire __xmr__u_dut_u_ctrl_req,
    output wire __xmr__u_dut_u_ctrl_ack,
    output wire __xmr__u_dut_u_proc_enable,
    output wire [7:0] __xmr__u_dut_u_proc_counter,
    output wire __xmr__u_dut_u_fifo_full,
    output wire __xmr__u_dut_u_fifo_empty
);
    // Submodule instances
    controller u_ctrl(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_ctrl_state(__xmr__u_dut_u_ctrl_state),
        .__xmr__u_dut_u_ctrl_req(__xmr__u_dut_u_ctrl_req),
        .__xmr__u_dut_u_ctrl_ack(__xmr__u_dut_u_ctrl_ack));
    fifo_simple u_fifo(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_fifo_wr_ptr(__xmr__u_dut_u_fifo_wr_ptr),
        .__xmr__u_dut_u_fifo_rd_ptr(__xmr__u_dut_u_fifo_rd_ptr),
        .__xmr__u_dut_u_fifo_full(__xmr__u_dut_u_fifo_full),
        .__xmr__u_dut_u_fifo_empty(__xmr__u_dut_u_fifo_empty));
    processor u_proc(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_proc_valid(__xmr__u_dut_u_proc_valid),
        .__xmr__u_dut_u_proc_data_out(__xmr__u_dut_u_proc_data_out),
        .__xmr__u_dut_u_proc_enable(__xmr__u_dut_u_proc_enable),
        .__xmr__u_dut_u_proc_counter(__xmr__u_dut_u_proc_counter));
endmodule

// =============================================================================
// Controller FSM Module
// =============================================================================
module controller(
    input clk,
    input rst_n,
    output wire [1:0] __xmr__u_dut_u_ctrl_state,
    output wire __xmr__u_dut_u_ctrl_req,
    output wire __xmr__u_dut_u_ctrl_ack
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
    assign __xmr__u_dut_u_ctrl_state = state;
    assign __xmr__u_dut_u_ctrl_req = req;
    assign __xmr__u_dut_u_ctrl_ack = ack;
endmodule

// =============================================================================
// Simple FIFO Module
// =============================================================================
module fifo_simple(
    input clk,
    input rst_n,
    output wire [3:0] __xmr__u_dut_u_fifo_wr_ptr,
    output wire [3:0] __xmr__u_dut_u_fifo_rd_ptr,
    output wire __xmr__u_dut_u_fifo_full,
    output wire __xmr__u_dut_u_fifo_empty
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
    assign __xmr__u_dut_u_fifo_wr_ptr = wr_ptr;
    assign __xmr__u_dut_u_fifo_rd_ptr = rd_ptr;
    assign __xmr__u_dut_u_fifo_full = full;
    assign __xmr__u_dut_u_fifo_empty = empty;
endmodule

// =============================================================================
// Data Processor Module
// =============================================================================
module processor(
    input clk,
    input rst_n,
    output wire __xmr__u_dut_u_proc_valid,
    output wire [7:0] __xmr__u_dut_u_proc_data_out,
    output wire __xmr__u_dut_u_proc_enable,
    output wire [7:0] __xmr__u_dut_u_proc_counter
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
    assign __xmr__u_dut_u_proc_valid = valid;
    assign __xmr__u_dut_u_proc_data_out = data_out;
    assign __xmr__u_dut_u_proc_enable = enable;
    assign __xmr__u_dut_u_proc_counter = counter;
endmodule

