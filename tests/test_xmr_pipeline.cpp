/*
 * test_xmr_pipeline.cpp - Pipeline register tests
 *
 * Tests pipeline register generation and configuration.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;
using namespace test_xmr;

//==============================================================================
// Pipeline Register Configuration Tests
//==============================================================================

TEST_CASE("Pipeline Register Configuration", "[PipeReg]") {
    SECTION("Global mode applies to all modules") {
        XMREliminateConfig config;
        config.modules                 = {"top"};
        config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(2);

        auto pipeConfig = config.pipeRegConfigMap["top"];
        REQUIRE(pipeConfig.getRegCountForModule("any") == 2);
    }

    SECTION("Selective mode for specific signals") {
        XMREliminateConfig config;
        config.modules = {"top", "mid"};

        std::vector<PipeRegEntry> entries = {{"mid", 2, {"critical_signal"}}};
        config.pipeRegConfigMap["top"]    = XMRPipeRegConfig::createSelective(entries);

        auto pipeConfig = config.pipeRegConfigMap["top"];
        REQUIRE(pipeConfig.getRegCountForModule("mid", "critical_signal") == 2);
        REQUIRE(pipeConfig.getRegCountForModule("mid", "other_signal") == 0);
    }
}

//==============================================================================
// Pipeline Register Generation Tests
//==============================================================================

TEST_CASE("Pipeline Register - generatePipelineRegisters function", "[PipeReg][Unit]") {
    std::string pipeCode = generatePipelineRegisters("input_sig", "output_port", 8, 3, "clk", "rst_n", true);

    REQUIRE(pipeCode.find("reg [7:0] output_port_pipe_0") != std::string::npos);
    REQUIRE(pipeCode.find("reg [7:0] output_port_pipe_1") != std::string::npos);
    REQUIRE(pipeCode.find("reg [7:0] output_port_pipe_2") != std::string::npos);
    REQUIRE(pipeCode.find("always @(posedge clk or negedge rst_n)") != std::string::npos);
    REQUIRE(pipeCode.find("!rst_n") != std::string::npos);
    REQUIRE(pipeCode.find("output_port_pipe_0 <= input_sig") != std::string::npos);
    REQUIRE(pipeCode.find("output_port_pipe_1 <= output_port_pipe_0") != std::string::npos);
    REQUIRE(pipeCode.find("output_port_pipe_2 <= output_port_pipe_1") != std::string::npos);
    REQUIRE(pipeCode.find("assign output_port = output_port_pipe_2") != std::string::npos);
}

TEST_CASE("Pipeline Register - generatePipelineRegisters active high reset", "[PipeReg][Unit]") {
    std::string pipeCode = generatePipelineRegisters("in", "out", 1, 2, "clock", "reset", false);

    REQUIRE(pipeCode.find("always @(posedge clock or posedge reset)") != std::string::npos);
    REQUIRE(pipeCode.find("if (reset)") != std::string::npos);
}

TEST_CASE("Pipeline Register - generatePipelineRegisters zero stages", "[PipeReg][Unit]") {
    std::string pipeCode = generatePipelineRegisters("in", "out", 8, 0, "clk", "rst", true);

    REQUIRE(pipeCode.empty());
}

//==============================================================================
// Pipeline Register Mode Tests
//==============================================================================

TEST_CASE("Pipeline Register - Global Mode with 2 stages", "[PipeReg]") {
    const std::string input = R"(
module top(
    input clk,
    input rst_n,
    output wire result
);
    sub u_sub(.clk(clk), .rst_n(rst_n));
    assign result = u_sub.fast_signal;
endmodule

module sub(
    input clk,
    input rst_n
);
    reg fast_signal;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            fast_signal <= 1'b0;
        else
            fast_signal <= ~fast_signal;
    end
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    input rst_n,
    output wire result
);
    logic __xmr__u_sub_fast_signal;
    sub u_sub(.clk(clk), .rst_n(rst_n),
        .__xmr__u_sub_fast_signal(__xmr__u_sub_fast_signal));
    assign result = __xmr__u_sub_fast_signal;
endmodule

module sub(
    input clk,
    input rst_n,
    output wire __xmr__u_sub_fast_signal
);
    reg fast_signal;
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
)";

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.resetActiveLow          = true;
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(2);

    testXMRElimination(input, expected, "pipereg_global_2stage", config);
}

TEST_CASE("Pipeline Register - Global Mode with multi-bit signal", "[PipeReg]") {
    const std::string input = R"(
module top(
    input clk,
    input rst_n,
    output wire [7:0] result
);
    sub u_sub(.clk(clk), .rst_n(rst_n));
    assign result = u_sub.data_bus;
endmodule

module sub(
    input clk,
    input rst_n
);
    reg [7:0] data_bus;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            data_bus <= 8'h00;
        else
            data_bus <= data_bus + 8'h01;
    end
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    input rst_n,
    output wire [7:0] result
);
    logic [7:0] __xmr__u_sub_data_bus;
    sub u_sub(.clk(clk), .rst_n(rst_n),
        .__xmr__u_sub_data_bus(__xmr__u_sub_data_bus));
    assign result = __xmr__u_sub_data_bus;
endmodule

module sub(
    input clk,
    input rst_n,
    output wire [7:0] __xmr__u_sub_data_bus
);
    reg [7:0] data_bus;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            data_bus <= 8'h00;
        else
            data_bus <= data_bus + 8'h01;
    end
    reg [7:0] __xmr__u_sub_data_bus_pipe_0;
    reg [7:0] __xmr__u_sub_data_bus_pipe_1;
    reg [7:0] __xmr__u_sub_data_bus_pipe_2;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            __xmr__u_sub_data_bus_pipe_0 <= 8'h0;
            __xmr__u_sub_data_bus_pipe_1 <= 8'h0;
            __xmr__u_sub_data_bus_pipe_2 <= 8'h0;
        end else begin
            __xmr__u_sub_data_bus_pipe_0 <= data_bus;
            __xmr__u_sub_data_bus_pipe_1 <= __xmr__u_sub_data_bus_pipe_0;
            __xmr__u_sub_data_bus_pipe_2 <= __xmr__u_sub_data_bus_pipe_1;
        end
    end
    assign __xmr__u_sub_data_bus = __xmr__u_sub_data_bus_pipe_2;

endmodule
)";

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.resetActiveLow          = true;
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(3);

    testXMRElimination(input, expected, "pipereg_multibit", config);
}

TEST_CASE("Pipeline Register - Per Module Mode", "[PipeReg]") {
    const std::string testFile = "test_pipereg_permodule.sv";

    createTestFile(testFile, R"(
module top(
    input wire clk,
    input wire rst_n,
    output wire result
);
    mid_module u_mid(.clk(clk), .rst_n(rst_n));
    
    assign result = u_mid.u_bottom.deep_signal;
endmodule

module mid_module(
    input wire clk,
    input wire rst_n
);
    bottom_module u_bottom(.clk(clk), .rst_n(rst_n));
endmodule

module bottom_module(
    input wire clk,
    input wire rst_n
);
    reg deep_signal;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            deep_signal <= 1'b0;
        else
            deep_signal <= ~deep_signal;
    end
endmodule
    )");

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.resetActiveLow          = true;
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createPerModule();

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];
    REQUIRE(output.find("_pipe_") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("Pipeline Register - Selective Mode", "[PipeReg]") {
    const std::string testFile = "test_pipereg_selective.sv";

    createTestFile(testFile, R"(
module top(
    input wire clk,
    input wire rst_n,
    output wire sig_a,
    output wire sig_b
);
    sub_module u_sub(.clk(clk), .rst_n(rst_n));
    
    assign sig_a = u_sub.critical_signal;
    assign sig_b = u_sub.normal_signal;
endmodule

module sub_module(
    input wire clk,
    input wire rst_n
);
    reg critical_signal;
    reg normal_signal;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            critical_signal <= 1'b0;
            normal_signal <= 1'b0;
        end else begin
            critical_signal <= ~critical_signal;
            normal_signal <= ~normal_signal;
        end
    end
endmodule
    )");

    std::vector<PipeRegEntry> entries = {{"sub_module", 2, {"critical_signal"}}};

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.resetActiveLow          = true;
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createSelective(entries);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());

    cleanupTestFile(testFile);
}

//==============================================================================
// Clock/Reset Validation Tests
//==============================================================================

TEST_CASE("XMR Elimination - Missing clock signal error", "[XMREliminate][ClockReset]") {
    const std::string testFile = "test_missing_clock.sv";

    createTestFile(testFile, R"(
module top(
    input wire rst_n,
    output wire result
);
    sub_module u_sub(.rst_n(rst_n));
    assign result = u_sub.data;
endmodule

module sub_module(
    input wire rst_n
);
    reg data;
    initial data = 1'b0;
endmodule
    )");

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(2);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.errors.size() > 0);
    REQUIRE(result.errors[0].find("clock") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Missing reset signal error", "[XMREliminate][ClockReset]") {
    const std::string testFile = "test_missing_reset.sv";

    createTestFile(testFile, R"(
module top(
    input wire clk,
    output wire result
);
    sub_module u_sub(.clk(clk));
    assign result = u_sub.data;
endmodule

module sub_module(
    input wire clk
);
    reg data;
    initial data = 1'b0;
endmodule
    )");

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(2);

    auto result = xmrEliminate({testFile}, config);

    REQUIRE_FALSE(result.success());
    REQUIRE(result.errors.size() > 0);
    REQUIRE(result.errors[0].find("reset") != std::string::npos);

    cleanupTestFile(testFile);
}

//==============================================================================
// Pipeline Register Integration Test
//==============================================================================

TEST_CASE("XMR Elimination with Pipeline Registers Integration", "[XMREliminate][PipeReg][Integration]") {
    const std::string input = R"(
module top(
    input clk,
    input rst_n,
    output wire result
);
    sub u_sub(.clk(clk), .rst_n(rst_n));
    assign result = u_sub.fast_signal;
endmodule

module sub(
    input clk,
    input rst_n
);
    reg fast_signal;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            fast_signal <= 1'b0;
        else
            fast_signal <= ~fast_signal;
    end
endmodule
)";

    const std::string expected = R"(
module top(
    input clk,
    input rst_n,
    output wire result
);
    logic __xmr__u_sub_fast_signal;
    sub u_sub(.clk(clk), .rst_n(rst_n),
        .__xmr__u_sub_fast_signal(__xmr__u_sub_fast_signal));
    assign result = __xmr__u_sub_fast_signal;
endmodule

module sub(
    input clk,
    input rst_n,
    output wire __xmr__u_sub_fast_signal
);
    reg fast_signal;
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
)";

    XMREliminateConfig config;
    config.modules                 = {"top"};
    config.clockName               = "clk";
    config.resetName               = "rst_n";
    config.resetActiveLow          = true;
    config.pipeRegConfigMap["top"] = XMRPipeRegConfig::createGlobal(2);

    testXMRElimination(input, expected, "pipereg_integration", config);
}
