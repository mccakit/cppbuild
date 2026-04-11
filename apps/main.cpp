import std;
import umkacxx;
import cppbuild;
import fmt;
import bs.thread_pool;
import cli11;
using CLI::App;

auto main(int argc, char **argv) -> int
{
    App app {"cppbuild — C++ Build System"};
    std::string script_path_in {};
    std::string toolchain_path_in {};
    std::string build_dir_in {};
    std::string install_dir_in {};

    auto configure = app.add_subcommand("configure", "Configure the project");
    configure->add_option("-S,--script-path", script_path_in, "Path to build script");
    configure->add_option("-T,--toolchain-path", toolchain_path_in, "Compiler toolchain to use");
    configure->add_option("-B,--build-dir", build_dir_in, "Build directory");
    configure->add_option("-I,--install-dir", install_dir_in, "Installation prefix");

    auto build = app.add_subcommand("build", "Execute build targets");
    build->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto install = app.add_subcommand("install", "Install build artifacts");
    install->add_option("-B,--build-dir", build_dir_in, "Source build directory");

    auto scan_srcs = app.add_subcommand("scan_srcs", "Scan for source changes/dependencies");
    scan_srcs->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto gen_p1689 = app.add_subcommand("gen_p1689", "Generate P1689 database");
    gen_p1689->add_option("-B,--build-dir", build_dir_in, "Build directory");

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError &e)
    {
        return app.exit(e);
    }

    if (configure->parsed())
    {
        std::filesystem::path script_path {std::filesystem::weakly_canonical(script_path_in)};
        std::filesystem::path toolchain_path {std::filesystem::weakly_canonical(toolchain_path_in)};
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        std::filesystem::path install_dir {std::filesystem::weakly_canonical(install_dir_in)};
        std::filesystem::create_directories(build_dir);
        cppbuild::types::toolchain tc {};
        tc.parse(toolchain_path);
        cppbuild::script script {script_path};
        auto result = script.run(build_dir, script_path.parent_path());
        cppbuild::types::build_graph graph {};
        graph.parse(result, script_path.string(), build_dir.string());
        graph.order();
        cppbuild::write_ninja_build({.graph = graph,
                                     .toolchain = tc,
                                     .build_dir = build_dir.string(),
                                     .self_path = std::filesystem::weakly_canonical(argv[0])});
        cppbuild::write_ninja_install({.graph = graph, .build_dir = build_dir, .prefix = install_dir});

        std::vector<cppbuild::types::build_target> build_targets;
        build_targets.reserve(graph.graph.vertex_count());
        for (const auto &[id, bt] : graph.graph.get_vertices())
        {
            build_targets.push_back(bt);
        }
        cppbuild::cache::save(build_dir / "cppbuild.cache", tc, build_targets, graph.link_targets, {});
    }
    else if (build->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        fmt::println("Building from: {}", build_dir.string());
    }
    else if (install->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        std::filesystem::path install_dir {std::filesystem::weakly_canonical(install_dir_in)};
        fmt::println("Installing from {} to {}", build_dir.string(), install_dir.string());
    }
    else if (scan_srcs->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        auto cache = cppbuild::cache::load(build_dir / "cppbuild.cache");
        auto scanner_output = cppbuild::run_scanner_per_source(build_dir, cache.tc, cache.build_targets);
        auto graph = cppbuild::parse_direct_deps(scanner_output);
        auto entries = cppbuild::resolve_transitive_deps(graph);
        cppbuild::generate_rsp(build_dir, cache.tc, cache.build_targets, entries);
        cppbuild::generate_dyndep(build_dir.string(), entries);
        cppbuild::cache::save(build_dir / "cppbuild.cache", cache.tc, cache.build_targets, cache.link_targets, entries);
    }
    else if (gen_p1689->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        auto cache = cppbuild::cache::load(build_dir / "cppbuild.cache");
        cppbuild::generate_p1689_per_source(build_dir, cache.tc, cache.build_targets);
        cppbuild::generate_compile_commands(build_dir, cache.tc, cache.build_targets);
    }
    return 0;
}
