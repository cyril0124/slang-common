/*
 * test_xmr_config.cpp - Tests for XMR configuration classes
 *
 * Tests XMRPipeRegConfig, XMRInfo, and XMREliminateConfig functionality.
 */

#include "../XMREliminate.h"
#include <catch2/catch_test_macros.hpp>

using namespace slang_common::xmr;

//==============================================================================
// XMRPipeRegConfig Tests
//==============================================================================

TEST_CASE("XMRPipeRegConfig basic functionality", "[XMRPipeRegConfig]") {
    SECTION("Default config has no pipeline registers") {
        XMRPipeRegConfig config;
        REQUIRE(config.mode == PipeRegMode::None);
        REQUIRE_FALSE(config.isEnabled());
        REQUIRE(config.getRegCountForModule("any_module") == 0);
    }

    SECTION("Global pipeline register mode") {
        auto config = XMRPipeRegConfig::createGlobal(3);
        REQUIRE(config.mode == PipeRegMode::Global);
        REQUIRE(config.isEnabled());
        REQUIRE(config.globalRegCount == 3);
        REQUIRE(config.getRegCountForModule("any_module") == 3);
        REQUIRE(config.getRegCountForModule("another_module") == 3);
    }

    SECTION("Per-module pipeline register mode") {
        auto config = XMRPipeRegConfig::createPerModule();
        REQUIRE(config.mode == PipeRegMode::PerModule);
        REQUIRE(config.isEnabled());
        REQUIRE(config.getRegCountForModule("any_module") == 1);
    }

    SECTION("Selective pipeline register mode") {
        std::vector<PipeRegEntry> entries = {{"moduleA", 2, {}}, {"moduleB", 3, {"sig1", "sig2"}}};
        auto config                       = XMRPipeRegConfig::createSelective(entries);

        REQUIRE(config.mode == PipeRegMode::Selective);
        REQUIRE(config.isEnabled());
        REQUIRE(config.getRegCountForModule("moduleA") == 2);
        REQUIRE(config.getRegCountForModule("moduleB", "sig1") == 3);
        REQUIRE(config.getRegCountForModule("moduleB", "sig3") == 0); // Not in list
        REQUIRE(config.getRegCountForModule("moduleC") == 0);         // Not configured
    }
}

//==============================================================================
// XMRInfo Tests
//==============================================================================

TEST_CASE("XMRInfo functionality", "[XMRInfo]") {
    SECTION("Generate unique ID") {
        XMRInfo info;
        info.sourceModule = "top";
        info.fullPath     = "sub.signal";

        auto id = info.getUniqueId();
        REQUIRE(id == "top_sub.signal");
    }

    SECTION("Generate port name from path") {
        XMRInfo info;
        info.fullPath = "top.mid.bottom.sig";

        auto portName = info.getPortName();
        REQUIRE(portName == "__xmr__top_mid_bottom_sig");
    }

    SECTION("Port name with simple path") {
        XMRInfo info;
        info.fullPath = "inst.data";

        auto portName = info.getPortName();
        REQUIRE(portName == "__xmr__inst_data");
    }
}

//==============================================================================
// XMREliminateConfig Tests
//==============================================================================

TEST_CASE("XMREliminateConfig defaults", "[XMREliminateConfig]") {
    XMREliminateConfig config;

    REQUIRE(config.modules.empty());
    REQUIRE(config.pipeRegConfigMap.empty());
    REQUIRE(config.clockName == "clk");
    REQUIRE(config.resetName == "rst_n");
    REQUIRE(config.resetActiveLow == true);
}
