#include <CLI/CLI.hpp>
import std;
import umkacxx;
import cppbuild;
using CLI::App;

auto main(int argc, char **argv) -> int
{
    App app {"cppbuild — Modern Umka-powered Build System"};
    std::string script_path {};
    std::string toolchain_path {};
    std::string build_dir {};
    std::string install_dir {};
    // Configure Subcommand
    auto config = app.add_subcommand("configure", "Configure the project");
    config->add_option("-S,--script-path", script_path, "Path to build script");
    config->add_option("-T,--toolchain-path", toolchain_path, "Compiler toolchain to use");
    config->add_option("-B,--build-dir", build_dir, "Build directory");
    // Build Subcommand
    auto build = app.add_subcommand("build", "Execute build targets");
    build->add_option("-B,--build-dir", build_dir, "Build directory");
    // Install Subcommand
    auto install = app.add_subcommand("install", "Install build artifacts");
    install->add_option("-B,--build-dir", build_dir, "Source build directory");
    install->add_option("-I,--install-dir", install_dir, "Installation prefix");
    // Scan Subcommand
    auto scan = app.add_subcommand("scan", "Scan for source changes/dependencies");
    scan->add_option("-B,--build-dir", build_dir, "Build directory");
    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        return app.exit(e);
    }

    build_dir = std::filesystem::weakly_canonical(build_dir);
    script_path = std::filesystem::weakly_canonical(script_path);
    toolchain_path = std::filesystem::weakly_canonical(toolchain_path);
    install_dir = std::filesystem::weakly_canonical(install_dir);
    if (config->parsed())
    {
        std::filesystem::create_directories(build_dir);
        cppbuild::types::toolchain tc {};
        tc.parse(toolchain_path);
        cppbuild::script script {script_path};
        auto result = script.run();
        cppbuild::types::build_graph graph {};
        graph.parse(result, script_path);
        cppbuild::write_compile_commands({.build_graph = graph, .tc = tc, .output_dir = build_dir});
        cppbuild::write_named_module_compile_commands({.build_graph = graph, .tc = tc, .output_dir = build_dir});
        graph.scan(build_dir, tc);
        graph.order();
        cppbuild::write_ninja_build({.graph = graph,
                           .toolchain = tc,
                           .build_dir = build_dir,
                           .self_path = std::filesystem::weakly_canonical(argv[0])});
    }
    else if (build->parsed())
    {
        std::println("Building from: {}", build_dir);
    }
    else if (install->parsed())
    {
        std::println("Installing from {} to {}", build_dir, install_dir);
    }
    else if (scan->parsed())
    {
        cppbuild::generate_dyndep(build_dir);
    }
    return 0;
}
