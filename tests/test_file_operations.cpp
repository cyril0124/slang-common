#include "../SlangCommon.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace slang_common;

TEST_CASE("file_operations::insertBeforeFileHead", "[file_operations]") {
    const std::string testFile = "test_insert_before.txt";

    SECTION("Insert content before file head") {
        // Create a test file
        std::ofstream out(testFile);
        out << "Original Content\nLine 2\nLine 3";
        out.close();

        // Insert content at the beginning
        bool result = file_operations::insertBeforeFileHead(testFile, "New Header");
        REQUIRE(result == true);

        // Verify the content
        std::ifstream in(testFile);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        REQUIRE(content.find("New Header") != std::string::npos);
        REQUIRE(content.find("Original Content") != std::string::npos);
        REQUIRE(content.find("New Header") < content.find("Original Content"));

        in.close();
        std::filesystem::remove(testFile);
    }

    SECTION("Handle non-existent file") {
        bool result = file_operations::insertBeforeFileHead("non_existent_file.txt", "Content");
        REQUIRE(result == false);
    }

    SECTION("Macro compatibility") {
        // Create a test file
        std::ofstream out(testFile);
        out << "Original Content";
        out.close();

        // Test macro interface
        INSERT_BEFORE_FILE_HEAD(testFile, "Macro Header");

        std::ifstream in(testFile);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        REQUIRE(content.find("Macro Header") != std::string::npos);

        in.close();
        std::filesystem::remove(testFile);
    }
}

TEST_CASE("file_operations::insertAfterFileEnd", "[file_operations]") {
    const std::string testFile = "test_insert_after.txt";

    SECTION("Insert content after file end") {
        // Create a test file
        std::ofstream out(testFile);
        out << "Original Content";
        out.close();

        // Insert content at the end
        bool result = file_operations::insertAfterFileEnd(testFile, "New Footer");
        REQUIRE(result == true);

        // Verify the content
        std::ifstream in(testFile);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        REQUIRE(content.find("Original Content") != std::string::npos);
        REQUIRE(content.find("New Footer") != std::string::npos);
        REQUIRE(content.find("Original Content") < content.find("New Footer"));

        in.close();
        std::filesystem::remove(testFile);
    }

    SECTION("Handle non-existent file") {
        bool result = file_operations::insertAfterFileEnd("non_existent_file.txt", "Content");
        REQUIRE(result == false);
    }

    SECTION("Macro compatibility") {
        // Create a test file
        std::ofstream out(testFile);
        out << "Original Content";
        out.close();

        // Test macro interface
        INSERT_AFTER_FILE_END(testFile, "Macro Footer");

        std::ifstream in(testFile);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        REQUIRE(content.find("Macro Footer") != std::string::npos);

        in.close();
        std::filesystem::remove(testFile);
    }
}
