/*
 * test_xmr_driver_options.cpp - Tests for driver options propagation
 *
 * Tests that slang driver options (include directories, defines, etc.)
 * are properly passed through to the xmrEliminate function.
 */

#include "../XMREliminate.h"
#include "test_helpers.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace slang_common::xmr;
using namespace test_xmr;

namespace fs = std::filesystem;

//==============================================================================
// Driver Options Tests
//==============================================================================

TEST_CASE("XMR Elimination - Include directory propagation", "[XMREliminate][DriverOptions]") {
    // Create test directory structure
    const std::string testDir    = "test_incdir_tmp";
    const std::string includeDir = testDir + "/include";

    // Create directories
    fs::create_directories(includeDir);

    // Create header file in include directory
    const std::string headerFile = includeDir + "/defs.svh";
    createTestFile(headerFile, R"(
// Header file with definitions
`define DATA_WIDTH 8
`define ENABLE_FEATURE
)");

    // Create main design file that uses include
    const std::string mainFile = testDir + "/design.sv";
    createTestFile(mainFile, R"(
`include "defs.svh"

module top;
    sub u_sub();
    wire [`DATA_WIDTH-1:0] data_out;
    
`ifdef ENABLE_FEATURE
    assign data_out = u_sub.data;
`endif
endmodule

module sub;
    reg [`DATA_WIDTH-1:0] data;
endmodule
)");

    XMREliminateConfig config;
    config.modules = {"top"};

    // Pass include directory through driver options
    config.driverOptions.includeDirs.push_back(includeDir);

    auto result = xmrEliminate({mainFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // Should have processed the XMR
    REQUIRE(output.find("__xmr__u_sub_data") != std::string::npos);

    // Cleanup
    cleanupTestFile(headerFile);
    cleanupTestFile(mainFile);
    fs::remove_all(testDir);
}

TEST_CASE("XMR Elimination - Define propagation", "[XMREliminate][DriverOptions]") {
    const std::string input = R"(
module top;
    sub u_sub();
    wire result;
    
`ifdef MY_DEFINE
    assign result = u_sub.enabled_signal;
`else
    assign result = 1'b0;
`endif
endmodule

module sub;
    reg enabled_signal;
    reg disabled_signal;
endmodule
)";

    const std::string testFile = "test_define_prop.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    // Pass define through driver options
    config.driverOptions.defines.push_back("MY_DEFINE=1");

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // Should have processed the enabled_signal XMR (the ifdef branch)
    REQUIRE(output.find("__xmr__u_sub_enabled_signal") != std::string::npos);
    // Should NOT have disabled_signal port (that's in else branch)
    REQUIRE(output.find("__xmr__u_sub_disabled_signal") == std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - Multiple include directories", "[XMREliminate][DriverOptions]") {
    // Create test directory structure
    const std::string testDir     = "test_multi_incdir_tmp";
    const std::string includeDir1 = testDir + "/inc1";
    const std::string includeDir2 = testDir + "/inc2";

    // Create directories
    fs::create_directories(includeDir1);
    fs::create_directories(includeDir2);

    // Create header files
    createTestFile(includeDir1 + "/types.svh", R"(
typedef logic [7:0] byte_t;
)");

    createTestFile(includeDir2 + "/config.svh", R"(
`define USE_XMR
)");

    // Create main design file
    const std::string mainFile = testDir + "/design.sv";
    createTestFile(mainFile, R"(
`include "types.svh"
`include "config.svh"

module top;
    sub u_sub();
    byte_t result;
    
`ifdef USE_XMR
    assign result = u_sub.data;
`endif
endmodule

module sub;
    byte_t data;
endmodule
)");

    XMREliminateConfig config;
    config.modules = {"top"};

    // Pass multiple include directories
    config.driverOptions.includeDirs.push_back(includeDir1);
    config.driverOptions.includeDirs.push_back(includeDir2);

    auto result = xmrEliminate({mainFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];
    REQUIRE(output.find("__xmr__u_sub_data") != std::string::npos);

    // Cleanup
    cleanupTestFile(includeDir1 + "/types.svh");
    cleanupTestFile(includeDir2 + "/config.svh");
    cleanupTestFile(mainFile);
    fs::remove_all(testDir);
}

TEST_CASE("XMR Elimination - System include directory", "[XMREliminate][DriverOptions]") {
    // Create test directory structure
    const std::string testDir   = "test_sysincdir_tmp";
    const std::string sysIncDir = testDir + "/system";

    fs::create_directories(sysIncDir);

    // Create system header
    createTestFile(sysIncDir + "/sys_defs.svh", R"(
`define SYS_WIDTH 16
)");

    // Create main design file
    const std::string mainFile = testDir + "/design.sv";
    createTestFile(mainFile, R"(
`include <sys_defs.svh>

module top;
    sub u_sub();
    wire [`SYS_WIDTH-1:0] data_out;
    assign data_out = u_sub.wide_data;
endmodule

module sub;
    reg [`SYS_WIDTH-1:0] wide_data;
endmodule
)");

    XMREliminateConfig config;
    config.modules = {"top"};

    // Pass system include directory
    config.driverOptions.systemIncludeDirs.push_back(sysIncDir);

    auto result = xmrEliminate({mainFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];
    REQUIRE(output.find("__xmr__u_sub_wide_data") != std::string::npos);

    // Cleanup
    cleanupTestFile(sysIncDir + "/sys_defs.svh");
    cleanupTestFile(mainFile);
    fs::remove_all(testDir);
}

TEST_CASE("XMR Elimination - Undefine propagation", "[XMREliminate][DriverOptions]") {
    // Test that undefines can suppress a define passed via driverOptions
    const std::string input = R"(
module top;
    sub u_sub();
    wire result;
    
`ifdef MY_FEATURE
    // This branch should NOT be taken if undef works
    assign result = u_sub.signal_a;
`else
    // This branch should be taken after undef
    assign result = u_sub.signal_b;
`endif
endmodule

module sub;
    reg signal_a;
    reg signal_b;
endmodule
)";

    const std::string testFile = "test_undef_prop.sv";
    createTestFile(testFile, input);

    XMREliminateConfig config;
    config.modules = {"top"};

    // Define and then undefine the macro - undefine should win
    config.driverOptions.defines.push_back("MY_FEATURE=1");
    config.driverOptions.undefines.push_back("MY_FEATURE");

    auto result = xmrEliminate({testFile}, config);

    REQUIRE(result.success());
    REQUIRE(result.modifiedFiles.size() >= 1);

    const std::string &output = result.modifiedFiles[0];

    // Should have signal_b (the else branch) since MY_FEATURE was undefined
    REQUIRE(output.find("__xmr__u_sub_signal_b") != std::string::npos);

    cleanupTestFile(testFile);
}

TEST_CASE("XMR Elimination - DriverOptions defaults", "[XMREliminate][DriverOptions]") {
    // Test that empty DriverOptions work correctly
    DriverOptions opts;

    REQUIRE(opts.includeDirs.empty());
    REQUIRE(opts.systemIncludeDirs.empty());
    REQUIRE(opts.defines.empty());
    REQUIRE(opts.undefines.empty());
    REQUIRE(opts.libDirs.empty());
    REQUIRE(opts.libExts.empty());
}
