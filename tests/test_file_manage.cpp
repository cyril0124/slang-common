#include "../SlangCommon.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace slang_common;

TEST_CASE("file_manage::backupFile", "[file_manage]") {
    const std::string workdir  = "test_workdir";
    const std::string testFile = "test_source.v";

    // Create test directory and file
    std::filesystem::create_directories(workdir);
    std::ofstream out(testFile);
    out << "module test;\nendmodule\n";
    out.close();

    SECTION("Backup file successfully") {
        std::string backupPath = file_manage::backupFile(testFile, workdir);

        REQUIRE(std::filesystem::exists(backupPath));
        REQUIRE(backupPath.find(".bak") != std::string::npos);

        // Check backup content
        std::ifstream in(backupPath);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        REQUIRE(content.find("//BEGIN:") != std::string::npos);
        REQUIRE(content.find("//END:") != std::string::npos);
        REQUIRE(content.find("module test") != std::string::npos);

        in.close();
        std::filesystem::remove(backupPath);
    }

    // Clean up
    std::filesystem::remove(testFile);
    std::filesystem::remove_all(workdir);
}

TEST_CASE("file_manage::isFileNewer", "[file_manage]") {
    const std::string file1 = "test_newer.txt";
    const std::string file2 = "test_older.txt";

    SECTION("Compare file modification times") {
        // Create first file
        std::ofstream out1(file1);
        out1 << "First file";
        out1.close();

        // Wait a bit to ensure different timestamps
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Create second file (newer)
        std::ofstream out2(file2);
        out2 << "Second file";
        out2.close();

        REQUIRE(file_manage::isFileNewer(file2, file1) == true);
        REQUIRE(file_manage::isFileNewer(file1, file2) == false);

        std::filesystem::remove(file1);
        std::filesystem::remove(file2);
    }

    SECTION("Handle non-existent files") {
        bool result = file_manage::isFileNewer("non_existent1.txt", "non_existent2.txt");
        REQUIRE(result == false);
    }
}

TEST_CASE("file_manage::generateNewFile", "[file_manage]") {
    const std::string outputDir = "test_output";

    SECTION("Generate files from marked content") {
        std::string content = "//BEGIN:test1.v\n"
                              "module test1;\n"
                              "endmodule\n"
                              "//END:test1.v\n"
                              "//BEGIN:test2.v\n"
                              "module test2;\n"
                              "endmodule\n"
                              "//END:test2.v\n";

        file_manage::generateNewFile(content, outputDir);

        REQUIRE(std::filesystem::exists(outputDir + "/test1.v"));
        REQUIRE(std::filesystem::exists(outputDir + "/test2.v"));

        // Verify content
        std::ifstream in(outputDir + "/test1.v");
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string fileContent = buffer.str();

        REQUIRE(fileContent.find("module test1") != std::string::npos);
        REQUIRE(fileContent.find("//BEGIN:") == std::string::npos); // Markers should be removed

        in.close();
        std::filesystem::remove_all(outputDir);
    }
}
