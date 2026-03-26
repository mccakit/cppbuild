#include <CLI/CLI.hpp>
import std;
import umkacxx;
import cppbuild;
using CLI::App;

auto split_str(const std::string &s) -> std::vector<std::string>
{
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

auto main(int argc, char **argv) -> int
{
    App app {"cppbuild — C++ Build System"};


    std::filesystem::path script_path {};
    std::filesystem::path toolchain_path {};
    std::filesystem::path build_dir {};
    std::filesystem::path install_dir {};
    std::filesystem::path c_compiler {};
    std::filesystem::path cxx_compiler {};
    std::vector<std::string> cflags {};
    std::vector<std::string> cxxflags {};
    std::vector<std::filesystem::path> c_sources {};
    std::vector<std::filesystem::path> cxx_sources {};

    std::string script_path_in {};
    std::string toolchain_path_in {};
    std::string build_dir_in {};
    std::string install_dir_in {};
    std::string c_compiler_in {};
    std::string cxx_compiler_in {};
    std::string cflags_in {};
    std::string cxxflags_in {};
    std::string c_sources_in {};
    std::string cxx_sources_in {};

    // Configure Subcommand
    auto config = app.add_subcommand("configure", "Configure the project");
    config->add_option("-S,--script-path", script_path_in, "Path to build script");
    config->add_option("-T,--toolchain-path", toolchain_path_in, "Compiler toolchain to use");
    config->add_option("-B,--build-dir", build_dir_in, "Build directory");
    // Build Subcommand
    auto build = app.add_subcommand("build", "Execute build targets");
    build->add_option("-B,--build-dir", build_dir_in, "Build directory");
    // Install Subcommand
    auto install = app.add_subcommand("install", "Install build artifacts");
    install->add_option("-B,--build-dir", build_dir_in, "Source build directory");
    install->add_option("-I,--install-dir", install_dir_in, "Installation prefix");
    // Scan Subcommand
    auto scan = app.add_subcommand("scan", "Scan for source changes/dependencies");
    scan->add_option("-B,--build-dir", build_dir_in, "Build directory");
    // Generate P1689 Subcommand
    auto gen_p1689 = app.add_subcommand("gen_p1689", "Generate P1689 database");
    gen_p1689->add_option("-B,--build-dir", build_dir_in, "Build directory");
    gen_p1689->add_option("--c-compiler", c_compiler_in, "C compiler");
    gen_p1689->add_option("--cxx-compiler", cxx_compiler_in, "C++ compiler");
    gen_p1689->add_option("--cflags", cflags_in, "C flags");
    gen_p1689->add_option("--cxxflags", cxxflags_in, "C++ flags");
    gen_p1689->add_option("--c-sources", c_sources_in, "C source files")->default_val("")->expected(0, 1);;
    gen_p1689->add_option("--cxx-sources", cxx_sources_in, "C++ source files")->default_val("")->expected(0, 1);;
    // Rsp Gen Subcommand
    auto rsp_gen = app.add_subcommand("rsp_gen", "Generate per-source response files");
    rsp_gen->add_option("-B,--build-dir", build_dir_in, "Build directory");
    rsp_gen->add_option("--cflags", cflags_in, "C flags");
    rsp_gen->add_option("--cxxflags", cxxflags_in, "C++ flags");
    rsp_gen->add_option("--c-sources", c_sources_in, "C source files")->default_val("")->expected(0, 1);
    rsp_gen->add_option("--cxx-sources", cxx_sources_in, "C++ source files")->default_val("")->expected(0, 1);
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        return app.exit(e);
    }

    if (config->parsed())
    {
        script_path = std::filesystem::weakly_canonical(script_path_in);
        toolchain_path = std::filesystem::weakly_canonical(toolchain_path_in);
        build_dir = std::filesystem::weakly_canonical(build_dir_in);
        std::filesystem::create_directories(build_dir);
        cppbuild::types::toolchain tc {};
        tc.parse(toolchain_path);
        cppbuild::script script {script_path};
        auto result = script.run();
        cppbuild::types::build_graph graph {};
        graph.parse(result, script_path.string(), build_dir.string());
        graph.order();
        cppbuild::write_ninja_build({.graph = graph,
                                     .toolchain = tc,
                                     .build_dir = build_dir.string(),
                                     .self_path = std::filesystem::weakly_canonical(argv[0])});
    }
    else if (build->parsed())
    {
        std::println("Building from: {}", build_dir.string());
    }
    else if (install->parsed())
    {
        std::println("Installing from {} to {}", build_dir.string(), install_dir.string());
    }
    else if (scan->parsed())
    {
        build_dir = std::filesystem::weakly_canonical(build_dir_in);
        cppbuild::generate_dyndep(build_dir.string());
    }
    else if (gen_p1689->parsed())
    {
        build_dir = std::filesystem::weakly_canonical(build_dir_in);
        c_compiler = c_compiler_in;
        cxx_compiler = cxx_compiler_in;
        std::vector<std::string> cflags = split_str(cflags_in);
        std::vector<std::string> cxxflags = split_str(cxxflags_in);
        std::vector<std::filesystem::path> c_sources;
        std::vector<std::filesystem::path> cxx_sources;
        for (const auto &s : split_str(c_sources_in))
        {
            c_sources.emplace_back(s);
        }
        for (const auto &s : split_str(cxx_sources_in))
        {
            cxx_sources.emplace_back(s);
        }
        cppbuild::generate_p1689({
            .build_dir = build_dir,
            .c_compiler = c_compiler,
            .cxx_compiler = cxx_compiler,
            .cflags = cflags,
            .cxxflags = cxxflags,
            .c_sources = c_sources,
            .cxx_sources = cxx_sources,
        });
    }
    else if (rsp_gen->parsed())
    {
        build_dir = std::filesystem::weakly_canonical(build_dir_in);
        std::vector<std::string> cflags = split_str(cflags_in);
        std::vector<std::string> cxxflags = split_str(cxxflags_in);
        std::vector<std::filesystem::path> c_sources;
        std::vector<std::filesystem::path> cxx_sources;
        for (const auto &s : split_str(c_sources_in))
            c_sources.emplace_back(s);
        for (const auto &s : split_str(cxx_sources_in))
            cxx_sources.emplace_back(s);
        cppbuild::generate_rsp(build_dir, cflags, cxxflags, c_sources, cxx_sources);
    }

    return 0;
}
