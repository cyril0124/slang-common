/*
 * test_xmr_sva_upward.cpp - SVA and upward reference tests
 *
 * Tests SVA formal verification scenarios and upward XMR references.
 */

#include "../SlangCommon.h"
#include "../XMREliminate.h"
#include "slang/ast/ASTVisitor.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace slang;
using namespace slang::ast;
using namespace test_xmr;

//==============================================================================
// Complex SVA Formal Verification Testbench Test
//==============================================================================

TEST_CASE("XMR Elimination - Complex SVA Formal Testbench", "[XMREliminate][SVA][Formal]") {
    const std::string input = R"(
// Formal verification testbench with SVA assertions using XMR
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
        (u_dut.u_fifo_ctrl.wr_ptr - u_dut.u_fifo_ctrl.rd_ptr) <= 8'd16;
    endproperty
    assert property (p_fifo_no_overflow) else $error("FIFO overflow detected!");

    // Property 2: When valid is high, data should be stable
    property p_data_stable_when_valid;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_datapath.valid |=> $stable(u_dut.u_datapath.data);
    endproperty
    assert property (p_data_stable_when_valid) else $error("Data not stable when valid!");

    // Property 3: FSM should not be in illegal state
    property p_fsm_legal_state;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_ctrl.state inside {2'b00, 2'b01, 2'b10, 2'b11};
    endproperty
    assert property (p_fsm_legal_state) else $error("FSM in illegal state!");

    // Property 4: Request-acknowledge handshake
    property p_req_ack_handshake;
        @(posedge clk) disable iff (!rst_n)
        u_dut.u_ctrl.req |-> ##[1:3] u_dut.u_ctrl.ack;
    endproperty
    assert property (p_req_ack_handshake) else $error("Handshake timeout!");

    // Cover property for FIFO full condition
    cover property (@(posedge clk) u_dut.u_fifo_ctrl.full);

endmodule

// DUT with multiple submodules
module dut(
    input clk,
    input rst_n
);
    // Control FSM submodule
    ctrl_fsm u_ctrl(.clk(clk), .rst_n(rst_n));

    // FIFO controller submodule
    fifo_ctrl u_fifo_ctrl(.clk(clk), .rst_n(rst_n));

    // Datapath submodule
    datapath u_datapath(.clk(clk), .rst_n(rst_n));
endmodule

// Control FSM module
module ctrl_fsm(
    input clk,
    input rst_n
);
    reg [1:0] state;
    reg req;
    reg ack;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= 2'b00;
            req <= 1'b0;
            ack <= 1'b0;
        end else begin
            case (state)
                2'b00: state <= 2'b01;
                2'b01: state <= 2'b10;
                2'b10: state <= 2'b11;
                2'b11: state <= 2'b00;
            endcase
            ack <= req;
        end
    end
endmodule

// FIFO controller module
module fifo_ctrl(
    input clk,
    input rst_n
);
    reg [7:0] wr_ptr;
    reg [7:0] rd_ptr;
    reg full;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr <= 8'd0;
            rd_ptr <= 8'd0;
            full <= 1'b0;
        end else begin
            wr_ptr <= wr_ptr + 8'd1;
            rd_ptr <= rd_ptr + 8'd1;
            full <= (wr_ptr - rd_ptr) >= 8'd15;
        end
    end
endmodule

// Datapath module
module datapath(
    input clk,
    input rst_n
);
    reg valid;
    reg [31:0] data;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid <= 1'b0;
            data <= 32'd0;
        end else begin
            valid <= ~valid;
            if (!valid)
                data <= data + 32'd1;
        end
    end
endmodule
)";

    const std::string expected = R"(
// Formal verification testbench with SVA assertions using XMR
module tb_formal(
    input clk,
    input rst_n
);
    logic [7:0] __xmr__u_dut_u_fifo_ctrl_wr_ptr;
    logic [7:0] __xmr__u_dut_u_fifo_ctrl_rd_ptr;
    logic __xmr__u_dut_u_datapath_valid;
    logic [31:0] __xmr__u_dut_u_datapath_data;
    logic [1:0] __xmr__u_dut_u_ctrl_state;
    logic __xmr__u_dut_u_ctrl_req;
    logic __xmr__u_dut_u_ctrl_ack;
    logic __xmr__u_dut_u_fifo_ctrl_full;
    // Instantiate DUT
    dut u_dut(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_fifo_ctrl_wr_ptr(__xmr__u_dut_u_fifo_ctrl_wr_ptr),
        .__xmr__u_dut_u_fifo_ctrl_rd_ptr(__xmr__u_dut_u_fifo_ctrl_rd_ptr),
        .__xmr__u_dut_u_datapath_valid(__xmr__u_dut_u_datapath_valid),
        .__xmr__u_dut_u_datapath_data(__xmr__u_dut_u_datapath_data),
        .__xmr__u_dut_u_ctrl_state(__xmr__u_dut_u_ctrl_state),
        .__xmr__u_dut_u_ctrl_req(__xmr__u_dut_u_ctrl_req),
        .__xmr__u_dut_u_ctrl_ack(__xmr__u_dut_u_ctrl_ack),
        .__xmr__u_dut_u_fifo_ctrl_full(__xmr__u_dut_u_fifo_ctrl_full));

    // =========================================================================
    // SVA Properties using XMR to access DUT internal signals
    // =========================================================================

    // Property 1: FIFO should not overflow
    // Access fifo_ctrl.wr_ptr and fifo_ctrl.rd_ptr via XMR
    property p_fifo_no_overflow;
        @(posedge clk) disable iff (!rst_n)
        ( __xmr__u_dut_u_fifo_ctrl_wr_ptr - __xmr__u_dut_u_fifo_ctrl_rd_ptr) <= 8'd16;
    endproperty
    assert property (p_fifo_no_overflow) else $error("FIFO overflow detected!");

    // Property 2: When valid is high, data should be stable
    property p_data_stable_when_valid;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_datapath_valid |=> $stable( __xmr__u_dut_u_datapath_data);
    endproperty
    assert property (p_data_stable_when_valid) else $error("Data not stable when valid!");

    // Property 3: FSM should not be in illegal state
    property p_fsm_legal_state;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_ctrl_state inside {2'b00, 2'b01, 2'b10, 2'b11};
    endproperty
    assert property (p_fsm_legal_state) else $error("FSM in illegal state!");

    // Property 4: Request-acknowledge handshake
    property p_req_ack_handshake;
        @(posedge clk) disable iff (!rst_n) __xmr__u_dut_u_ctrl_req |-> ##[1:3] __xmr__u_dut_u_ctrl_ack;
    endproperty
    assert property (p_req_ack_handshake) else $error("Handshake timeout!");

    // Cover property for FIFO full condition
    cover property (@(posedge clk) __xmr__u_dut_u_fifo_ctrl_full);

endmodule

// DUT with multiple submodules
module dut(
    input clk,
    input rst_n,
    output wire [7:0] __xmr__u_dut_u_fifo_ctrl_wr_ptr,
    output wire [7:0] __xmr__u_dut_u_fifo_ctrl_rd_ptr,
    output wire __xmr__u_dut_u_datapath_valid,
    output wire [31:0] __xmr__u_dut_u_datapath_data,
    output wire [1:0] __xmr__u_dut_u_ctrl_state,
    output wire __xmr__u_dut_u_ctrl_req,
    output wire __xmr__u_dut_u_ctrl_ack,
    output wire __xmr__u_dut_u_fifo_ctrl_full
);
    // Control FSM submodule
    ctrl_fsm u_ctrl(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_ctrl_state(__xmr__u_dut_u_ctrl_state),
        .__xmr__u_dut_u_ctrl_req(__xmr__u_dut_u_ctrl_req),
        .__xmr__u_dut_u_ctrl_ack(__xmr__u_dut_u_ctrl_ack));

    // FIFO controller submodule
    fifo_ctrl u_fifo_ctrl(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_fifo_ctrl_wr_ptr(__xmr__u_dut_u_fifo_ctrl_wr_ptr),
        .__xmr__u_dut_u_fifo_ctrl_rd_ptr(__xmr__u_dut_u_fifo_ctrl_rd_ptr),
        .__xmr__u_dut_u_fifo_ctrl_full(__xmr__u_dut_u_fifo_ctrl_full));

    // Datapath submodule
    datapath u_datapath(.clk(clk), .rst_n(rst_n),
        .__xmr__u_dut_u_datapath_valid(__xmr__u_dut_u_datapath_valid),
        .__xmr__u_dut_u_datapath_data(__xmr__u_dut_u_datapath_data));
endmodule

// Control FSM module
module ctrl_fsm(
    input clk,
    input rst_n,
    output wire [1:0] __xmr__u_dut_u_ctrl_state,
    output wire __xmr__u_dut_u_ctrl_req,
    output wire __xmr__u_dut_u_ctrl_ack
);
    reg [1:0] state;
    reg req;
    reg ack;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= 2'b00;
            req <= 1'b0;
            ack <= 1'b0;
        end else begin
            case (state)
                2'b00: state <= 2'b01;
                2'b01: state <= 2'b10;
                2'b10: state <= 2'b11;
                2'b11: state <= 2'b00;
            endcase
            ack <= req;
        end
    end
    assign __xmr__u_dut_u_ctrl_state = state;
    assign __xmr__u_dut_u_ctrl_req = req;
    assign __xmr__u_dut_u_ctrl_ack = ack;
endmodule

// FIFO controller module
module fifo_ctrl(
    input clk,
    input rst_n,
    output wire [7:0] __xmr__u_dut_u_fifo_ctrl_wr_ptr,
    output wire [7:0] __xmr__u_dut_u_fifo_ctrl_rd_ptr,
    output wire __xmr__u_dut_u_fifo_ctrl_full
);
    reg [7:0] wr_ptr;
    reg [7:0] rd_ptr;
    reg full;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr <= 8'd0;
            rd_ptr <= 8'd0;
            full <= 1'b0;
        end else begin
            wr_ptr <= wr_ptr + 8'd1;
            rd_ptr <= rd_ptr + 8'd1;
            full <= (wr_ptr - rd_ptr) >= 8'd15;
        end
    end
    assign __xmr__u_dut_u_fifo_ctrl_wr_ptr = wr_ptr;
    assign __xmr__u_dut_u_fifo_ctrl_rd_ptr = rd_ptr;
    assign __xmr__u_dut_u_fifo_ctrl_full = full;
endmodule

// Datapath module
module datapath(
    input clk,
    input rst_n,
    output wire __xmr__u_dut_u_datapath_valid,
    output wire [31:0] __xmr__u_dut_u_datapath_data
);
    reg valid;
    reg [31:0] data;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid <= 1'b0;
            data <= 32'd0;
        end else begin
            valid <= ~valid;
            if (!valid)
                data <= data + 32'd1;
        end
    end
    assign __xmr__u_dut_u_datapath_valid = valid;
    assign __xmr__u_dut_u_datapath_data = data;
endmodule
)";

    XMREliminateConfig config;
    config.modules = {"tb_formal"};

    testXMRElimination(input, expected, "sva_formal_tb", config);
}

//==============================================================================
// Debug Test - Hierarchical Reference Analysis
//==============================================================================

class HierRefDebugger : public ASTVisitor<HierRefDebugger, true, true> {
  public:
    const InstanceSymbol *currentInstance = nullptr;
    std::vector<std::string> debugOutput;

    void handle(const InstanceSymbol &inst) {
        auto prev       = currentInstance;
        currentInstance = &inst;
        visitDefault(inst);
        currentInstance = prev;
    }

    void handle(const HierarchicalValueExpression &expr) {
        if (!currentInstance)
            return;

        std::stringstream ss;
        ss << "In module: " << currentInstance->getDefinition().name << "\n";
        ss << "  Target symbol: " << expr.symbol.name << "\n";
        ss << "  Syntax: " << (expr.syntax ? expr.syntax->toString() : "null") << "\n";
        ss << "  Path elements:\n";
        for (const auto &elem : expr.ref.path) {
            ss << "    - name: " << elem.symbol->name << ", kind: " << toString(elem.symbol->kind) << "\n";
            if (elem.symbol->kind == SymbolKind::Instance) {
                auto &inst = elem.symbol->as<InstanceSymbol>();
                ss << "      def: " << inst.getDefinition().name << "\n";
            }
        }
        ss << "  Upward count: " << expr.ref.upwardCount << "\n";
        debugOutput.push_back(ss.str());
    }
};

TEST_CASE("Debug - Absolute path XMR analysis", "[XMREliminate][Debug]") {
    const std::string input = R"(
module tb_top;
    logic clock;
    logic reset;
    dut uut(.clock(clock), .reset(reset));
    others other_inst();
endmodule

module dut(input wire clock, input wire reset);
    reg [3:0] counter;
    reg another_reg;
endmodule

module others;
    default clocking @(posedge tb_top.clock);
    endclocking
    property TestProperty;
        disable iff(tb_top.reset) tb_top.uut.counter[0] && tb_top.uut.another_reg;
    endproperty
    cover_test: cover property (TestProperty);
endmodule
)";

    const std::string testFile = "test_absolute_xmr_debug.sv";
    createTestFile(testFile, input);

    slang_common::Driver driver("DebugDriver");
    driver.addStandardArgs();
    driver.addFile(testFile);
    driver.loadAllSources();
    driver.processOptions(true);
    driver.parseAllSources();

    auto comp = driver.createCompilation();
    REQUIRE(comp != nullptr);

    HierRefDebugger debugger;
    comp->getRoot().visit(debugger);

    std::cout << "\n=== DEBUG OUTPUT ===" << std::endl;
    for (const auto &s : debugger.debugOutput) {
        std::cout << s << std::endl;
    }
    std::cout << "====================" << std::endl;

    cleanupTestFile(testFile);
}

//==============================================================================
// Upward Reference XMR Test (Absolute Path XMRs)
//==============================================================================

TEST_CASE("XMR Eliminate - Upward references (absolute path XMRs)", "[XMREliminate][UpwardRef]") {
    const std::string testFile1 = "test_upward_ref_tb_top.sv";
    const std::string testFile2 = "test_upward_ref_dut.sv";
    const std::string testFile3 = "test_upward_ref_others.sv";

    const std::string tb_top = R"(
module tb_top;
    logic clock;
    logic reset;
    dut uut(.clock(clock), .reset(reset));
    others other_inst();
    
    initial begin
        clock = 0;
        forever #5 clock = ~clock;
    end
    
    initial begin
        reset = 1;
        #15 reset = 0;
    end
endmodule
)";

    const std::string dut = R"(
module dut(input wire clock, input wire reset);
    reg [3:0] counter;
    reg another_reg;
    
    always_ff @(posedge clock or posedge reset) begin
        if (reset) counter <= 4'b0;
        else counter <= counter + 1;
    end
    
    always_ff @(posedge clock or posedge reset) begin
        if (reset) another_reg <= 1'b0;
        else another_reg <= ~another_reg;
    end
endmodule
)";

    const std::string others = R"(
module others;
    default clocking @(posedge tb_top.clock);
    endclocking
    property TestProperty;
        disable iff(tb_top.reset) tb_top.uut.counter[0] && tb_top.uut.another_reg;
    endproperty
    cover_test: cover property (TestProperty);
endmodule
)";

    createTestFile(testFile1, tb_top);
    createTestFile(testFile2, dut);
    createTestFile(testFile3, others);

    XMREliminateConfig config;

    auto result = xmrEliminate({testFile1, testFile2, testFile3}, config);

    REQUIRE(result.success());

    REQUIRE(result.eliminatedXMRs.size() == 4);

    for (const auto &xmr : result.eliminatedXMRs) {
        REQUIRE(xmr.sourceModule == "others");
    }

    std::string summary = result.getSummary();
    REQUIRE(summary.find("XMRs Eliminated: 4") != std::string::npos);
    REQUIRE(summary.find("tb_top.clock") != std::string::npos);
    REQUIRE(summary.find("tb_top.reset") != std::string::npos);
    REQUIRE(summary.find("tb_top.uut.counter") != std::string::npos);
    REQUIRE(summary.find("tb_top.uut.another_reg") != std::string::npos);

    REQUIRE(result.modifiedFiles.size() == 3);

    bool foundOthersWithPorts = false;
    for (const auto &content : result.modifiedFiles) {
        if (content.find("module others;") != std::string::npos || content.find("module others(") != std::string::npos) {
            if (content.find("input wire __xmr__tb_top_clock") != std::string::npos) {
                foundOthersWithPorts = true;
                REQUIRE(content.find("input wire __xmr__tb_top_reset") != std::string::npos);
                REQUIRE(content.find("input wire [3:0] __xmr__tb_top_uut_counter") != std::string::npos);
                REQUIRE(content.find("input wire __xmr__tb_top_uut_another_reg") != std::string::npos);
            }
        }
    }
    REQUIRE(foundOthersWithPorts);

    cleanupTestFile(testFile1);
    cleanupTestFile(testFile2);
    cleanupTestFile(testFile3);
}
