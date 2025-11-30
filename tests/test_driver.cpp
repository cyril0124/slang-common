#include "../SlangCommon.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

// Only use std namespace, avoid using namespace for slang for better readability
using namespace slang_common;

TEST_CASE("Driver basic functionality", "[Driver]") {
    SECTION("Driver initialization") {
        Driver driver("TestDriver");
        REQUIRE_NOTHROW(driver.setName("UpdatedName"));
        REQUIRE_NOTHROW(driver.setVerbose(true));
    }

    SECTION("Add files") {
        Driver driver("TestDriver");
        driver.addFile("test1.v");
        driver.addFile("test2.v");

        auto &files = driver.getFiles();
        REQUIRE(files.size() == 2);
        REQUIRE(files[0] == "test1.v");
        REQUIRE(files[1] == "test2.v");
    }

    SECTION("Add multiple files") {
        Driver driver("TestDriver");
        std::vector<std::string> files = {"test1.v", "test2.v", "test3.v"};
        driver.addFiles(files);

        auto &driverFiles = driver.getFiles();
        REQUIRE(driverFiles.size() == 3);
    }
}

TEST_CASE("checkDiagsError", "[diagnostics]") {
    SECTION("Empty diagnostics should return false") {
        slang::Diagnostics diags;
        REQUIRE(slang_common::checkDiagsError(diags) == false);
    }
}

// Note: rebuildSyntaxTree tests are disabled due to fmt library dependency issues
// These tests require proper linking with fmt library which has ABI compatibility issues
// with the current libsvlang.a build
