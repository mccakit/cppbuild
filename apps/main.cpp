import std;
import umkacxx;
import cppbuild;
import fmt;
import bs.thread_pool;
import cli11;
import lmdbxx;
import zpp.bits;
using CLI::App;

auto main(int argc, char **argv) -> int
{
    App app {"cppbuild — C++ Build System"};

    std::string script_path_in {};
    std::string toolchain_path_in {};
    std::string build_dir_in {};
    std::string install_dir_in {};
    std::string db_file_in {};
    std::string src_file_in {};

    auto configure = app.add_subcommand("configure", "Configure the project");
    configure->add_option("-S,--script-path", script_path_in, "Path to build script");
    configure->add_option("-T,--toolchain-path", toolchain_path_in, "Compiler toolchain to use");
    configure->add_option("-B,--build-dir", build_dir_in, "Build directory");
    configure->add_option("-I,--install-dir", install_dir_in, "Installation prefix");

    auto build = app.add_subcommand("build", "Execute build targets");
    build->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto install = app.add_subcommand("install", "Install build artifacts");
    install->add_option("-B,--build-dir", build_dir_in, "Source build directory");

    auto gen_p1689 = app.add_subcommand("gen_p1689", "Generate P1689 database");
    gen_p1689->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto scan_single = app.add_subcommand("scan_single", "Scan a single source for module dependencies");
    scan_single->add_option("db_file", db_file_in, "P1689 database file")->required();
    scan_single->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto gen_single_dd = app.add_subcommand("gen_single_dd", "Generate dyndep file for a single source");
    gen_single_dd->add_option("src_file", src_file_in, "Source file path")->required();
    gen_single_dd->add_option("-B,--build-dir", build_dir_in, "Build directory");

    auto gen_single_rsp = app.add_subcommand("gen_single_rsp", "Generate rsp file for a single source");
    gen_single_rsp->add_option("src_file", src_file_in, "Source file path")->required();
    gen_single_rsp->add_option("-B,--build-dir", build_dir_in, "Build directory");
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
                                     .src_root = script_path.parent_path(),
                                     .build_dir = build_dir.string(),
                                     .self_path = std::filesystem::weakly_canonical(argv[0])});
        cppbuild::write_ninja_install({.graph = graph, .src_root = script_path.parent_path(), .build_dir = build_dir, .prefix = install_dir});
        std::vector<cppbuild::types::build_target> build_targets;
        build_targets.reserve(graph.graph.vertex_count());
        for (const auto &[id, bt] : graph.graph.get_vertices())
        {
            build_targets.push_back(bt);
        }
        cppbuild::generate_compdb_per_source(script_path.parent_path(), build_dir, tc, build_targets);
        cppbuild::create_db(build_dir / "config.db");
        {
            auto env = cppbuild::load_db(build_dir / "config.db");
            auto txn = lmdbxx::txn::begin(env);
            auto dbi = lmdbxx::dbi::open(txn, nullptr);

            cppbuild::db_put_raw(dbi, txn, "src_root", script_path.parent_path().string());

            for (const auto &bt : build_targets)
            {
                cppbuild::db_put_struct(dbi, txn, "bt:" + bt.name, bt);
                for (const auto &sg : bt.srcs)
                {
                    for (const auto &src : sg.srcs)
                    {
                        cppbuild::db_put_raw(dbi, txn, "src:" + src.string(), bt.name);
                    }
                }
                for (const auto &gg : bt.gen_groups)
                {
                    for (const auto &go : gg.outputs)
                    {
                        cppbuild::db_put_raw(dbi, txn, "src:" + go.path.string(), bt.name);
                    }
                }
            }
            for (const auto &lt : graph.link_targets)
            {
                cppbuild::db_put_struct(dbi, txn, "lt:" + lt.name, lt);
            }
            txn.commit();
        }
        tc.save(build_dir / "tc.cache");
        cppbuild::create_db(build_dir / "deps.db");
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
    else if (scan_single->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        std::filesystem::path db_path {std::filesystem::weakly_canonical(db_file_in)};
        auto tc = cppbuild::types::toolchain::load(build_dir / "tc.cache");
        auto deps_env = cppbuild::load_db(build_dir / "deps.db");
        auto config_env = cppbuild::load_db(build_dir / "config.db", lmdbxx::env_flags::rdonly);
        cppbuild::scan_single_source(db_path, build_dir, tc, deps_env, config_env);
    }
    else if (gen_single_dd->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        std::filesystem::path src_path {std::filesystem::weakly_canonical(src_file_in)};
        auto deps_env = cppbuild::load_db(build_dir / "deps.db", lmdbxx::env_flags::rdonly);
        auto config_env = cppbuild::load_db(build_dir / "config.db", lmdbxx::env_flags::rdonly);
        cppbuild::generate_single_dyndep(src_path, build_dir, deps_env, config_env);
    }
    else if (gen_single_rsp->parsed())
    {
        std::filesystem::path build_dir {std::filesystem::weakly_canonical(build_dir_in)};
        std::filesystem::path src_path {std::filesystem::weakly_canonical(src_file_in)};
        auto deps_env = cppbuild::load_db(build_dir / "deps.db", lmdbxx::env_flags::rdonly);
        auto config_env =
            cppbuild::load_db(build_dir / "config.db", lmdbxx::env_flags::rdonly | lmdbxx::env_flags::no_lock);

        cppbuild::generate_single_rsp(src_path, build_dir, deps_env, config_env);
    }
    return 0;
}
