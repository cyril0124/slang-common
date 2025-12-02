---@diagnostic disable

local prj_dir = os.curdir()
local build_dir = path.join(prj_dir, "build")
local conan_install_dir = path.join(prj_dir, "conan_installed")
local conan_libs_dir = path.join(conan_install_dir, "lib")
local conan_include_dir = path.join(conan_install_dir, "include")

target("init", function()
    set_kind("phony")
    on_run(function(target)
        local conan_cmd = "conan"
        local has_conan = try { function() return os.iorun("conan --version") end }

        if not has_conan then
            os.mkdir(build_dir)
            os.cd(build_dir)
            os.exec("wget https://github.com/conan-io/conan/releases/download/2.14.0/conan-2.14.0-linux-x86_64.tgz")
            os.mkdir("conan")
            os.exec("tar -xvf conan-2.14.0-linux-x86_64.tgz -C ./conan")
            conan_cmd = path.join(build_dir, "conan", "bin", "conan")
        end

        os.cd(path.join(prj_dir, "scripts", "conan", "slang"))
        try {
            function()
                os.exec(conan_cmd .. " create . --build=missing")
            end,
            catch
            {
                function(errors)
                    os.exec(conan_cmd .. " profile detect --force")
                    os.exec(conan_cmd .. " create . --build=missing")
                end
            }
        }

        os.cd(prj_dir)
        os.exec(conan_cmd .. " install . --output-folder=%s --build=missing", conan_install_dir)
    end)
end)

target("slang-syntax-viewer", function()
    set_kind("binary")

    set_languages("c++20")

    add_files(
        path.join(prj_dir, "slang-syntax-viewer", "*.cpp"),
        path.join(prj_dir, "*.cpp")
    )

    add_includedirs(
        prj_dir,
        conan_include_dir
    )

    add_defines("SLANG_BOOST_SINGLE_HEADER")

    add_links("svlang", "fmt", "mimalloc")
    add_linkdirs(conan_libs_dir)
    add_rpathdirs(conan_libs_dir)

    after_build(function(target)
        local bin_dir = path.join(prj_dir, "bin")
        os.mkdir(bin_dir)
        os.cp(target:targetfile(), bin_dir)
    end)
end)

target("tests", function()
    set_kind("binary")
    set_default(false)

    set_languages("c++20")

    add_files(
        path.join(prj_dir, "tests", "*.cpp"),
        path.join(prj_dir, "SlangCommon.cpp"),
        path.join(prj_dir, "SemanticModel.cpp"),
        path.join(prj_dir, "XMREliminate", "*.cpp")
    )

    add_includedirs(
        prj_dir,
        conan_include_dir
    )

    add_defines("SLANG_BOOST_SINGLE_HEADER")

    add_links("Catch2Main", "Catch2", "svlang", "fmt", "mimalloc")
    add_linkdirs(conan_libs_dir)
    add_rpathdirs(conan_libs_dir)

    after_build(function(target)
        print("Tests built successfully at: " .. target:targetfile())
    end)
end)

target("xmr-eliminate", function()
    set_kind("binary")
    set_default(false)

    set_languages("c++20")

    add_files(
        path.join(prj_dir, "xmr-eliminate", "*.cpp"),
        path.join(prj_dir, "SlangCommon.cpp"),
        path.join(prj_dir, "SemanticModel.cpp"),
        path.join(prj_dir, "XMREliminate", "*.cpp")
    )

    add_includedirs(
        prj_dir,
        conan_include_dir
    )

    add_defines("SLANG_BOOST_SINGLE_HEADER")

    add_links("svlang", "fmt", "mimalloc")
    add_linkdirs(conan_libs_dir)
    add_rpathdirs(conan_libs_dir)

    after_build(function(target)
        local bin_dir = path.join(prj_dir, "bin")
        os.mkdir(bin_dir)
        os.cp(target:targetfile(), bin_dir)
        print("xmr-eliminate built successfully at: " .. path.join(bin_dir, "xmr-eliminate"))
    end)
end)

target("format", function()
    set_kind("phony")
    on_run(function(target)
        -- Find all C++ source and header files
        local cpp_files = os.files(path.join(prj_dir, "*.cpp"))
        local h_files = os.files(path.join(prj_dir, "*.h"))
        local test_files = os.files(path.join(prj_dir, "tests", "*.cpp"))
        local test_h_files = os.files(path.join(prj_dir, "tests", "*.h"))
        local viewer_files = os.files(path.join(prj_dir, "slang-syntax-viewer", "*.cpp"))
        local xmr_files = os.files(path.join(prj_dir, "xmr-eliminate", "*.cpp"))
        local xmr_elim_cpp = os.files(path.join(prj_dir, "XMREliminate", "*.cpp"))
        local xmr_elim_h = os.files(path.join(prj_dir, "XMREliminate", "*.h"))
        
        -- Combine all files
        local all_files = {}
        for _, f in ipairs(cpp_files) do table.insert(all_files, f) end
        for _, f in ipairs(h_files) do table.insert(all_files, f) end
        for _, f in ipairs(test_files) do table.insert(all_files, f) end
        for _, f in ipairs(test_h_files) do table.insert(all_files, f) end
        for _, f in ipairs(viewer_files) do table.insert(all_files, f) end
        for _, f in ipairs(xmr_files) do table.insert(all_files, f) end
        for _, f in ipairs(xmr_elim_cpp) do table.insert(all_files, f) end
        for _, f in ipairs(xmr_elim_h) do table.insert(all_files, f) end
        
        -- Format each file
        print("Formatting C++ files with clang-format...")
        for _, file in ipairs(all_files) do
            print("  Formatting: " .. path.relative(file, prj_dir))
            os.exec("clang-format -i %s", file)
        end
        print("Formatting complete! Total files formatted: " .. #all_files)
    end)
end)
