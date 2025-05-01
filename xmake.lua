---@diagnostic disable
local prj_dir = os.curdir()
local build_dir = path.join(prj_dir, "build")
local conan_install_dir = path.join(prj_dir, "conan_installed")
local conan_libs_dir = path.join(conan_install_dir, "lib")
local conan_include_dir = path.join(conan_install_dir, "include")

target("init")
    set_kind("phony")
    on_run(function (target)
        local conan_cmd = "conan"
        local has_conan = try { function () return os.iorun("conan --version") end }

        if not has_conan then
            os.mkdir(build_dir)
            os.cd(build_dir)
            os.exec("wget https://github.com/conan-io/conan/releases/download/2.14.0/conan-2.14.0-linux-x86_64.tgz")
            os.mkdir("./conan")
            os.exec("tar -xvf conan-2.14.0-linux-x86_64.tgz -C ./conan")
            conan_cmd = build_dir .. "/conan/bin/conan"
        end

        os.cd(path.join(prj_dir, "scripts", "conan", "slang"))
        os.exec(conan_cmd .. " create . --build=missing")

        os.cd(prj_dir)
        os.exec(conan_cmd .. " install . --output-folder=%s --build=missing", conan_install_dir)
    end)

target("slang-syntax-viewer")
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

    after_build(function (target)
        local bin_dir = path.join(prj_dir, "bin")
        os.mkdir(bin_dir)
        os.cp(target:targetfile(), bin_dir)
    end)