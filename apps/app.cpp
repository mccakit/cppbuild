import std;
import umka;
import cppbuild;
import fmt;
import bs.thread_pool;
import cli11;
import lmdb;
import zpp.bits;

struct configure_opts_t
{
        std::string script_path;
        std::string toolchain_name;
        std::string build_dir;
        std::string install_dir;
};

struct gen_p1689_opts_t
{
        std::string build_dir;
};

struct scan_single_opts_t
{
        std::string db_file;
        std::string build_dir;
};

struct gen_single_dd_opts_t
{
        std::string src_file;
        std::string build_dir;
};

struct gen_single_rsp_opts_t
{
        std::string src_file;
        std::string build_dir;
};

struct tc_add_opts_t
{
        std::string name;
        std::string toolchain_path;
};

struct tc_remove_opts_t
{
        std::string name;
};

auto run_configure(const configure_opts_t &opts, const std::filesystem::path &self_path) -> void
{
    std::filesystem::path script_path {std::filesystem::weakly_canonical(opts.script_path)};
    std::filesystem::path build_dir {std::filesystem::weakly_canonical(opts.build_dir)};
    std::filesystem::path install_dir {std::filesystem::weakly_canonical(opts.install_dir)};
    std::filesystem::create_directories(build_dir);

    const auto tc_dir = self_path.parent_path() / "cache" / "toolchains" / opts.toolchain_name;
    auto tc = cppbuild::types::toolchain::load(tc_dir / "toolchain.bin");

    const auto src_pcm = tc_dir / "std.pcm";
    const auto dst_pcm = build_dir / "std.pcm";
    if (std::filesystem::exists(src_pcm))
    {
        std::filesystem::copy_file(src_pcm, dst_pcm, std::filesystem::copy_options::overwrite_existing);
        const auto old_flag = fmt::format("-fmodule-file=std={}", src_pcm.string());
        const auto new_flag = fmt::format("-fmodule-file=std={}", dst_pcm.string());
        for (auto &f : tc.cxxflags)
        {
            if (f == old_flag)
            {
                f = new_flag;
            }
        }
    }

    cppbuild::script script {script_path};
    auto result = script.run(build_dir, script_path.parent_path());
    cppbuild::types::build_graph graph {};
    graph.parse(result, script_path.string(), build_dir.string());
    graph.order();
    cppbuild::write_ninja_build({.graph = graph,
                                 .toolchain = tc,
                                 .src_root = script_path.parent_path(),
                                 .build_dir = build_dir.string(),
                                 .self_path = self_path});
    cppbuild::write_ninja_install(
        {.graph = graph, .src_root = script_path.parent_path(), .build_dir = build_dir, .prefix = install_dir});
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
        auto txn = lmdb::txn::begin(env);
        auto dbi = lmdb::dbi::open(txn, nullptr);
        cppbuild::db_put_raw(dbi, txn, "src_root", script_path.parent_path().string());
        cppbuild::db_put_struct(dbi, txn, "toolchain", tc);
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
        for (const auto &at : graph.archive_targets)
        {
            cppbuild::db_put_struct(dbi, txn, "at:" + at.name, at);
        }
        for (const auto &lt : graph.link_targets)
        {
            cppbuild::db_put_struct(dbi, txn, "lt:" + lt.name, lt);
        }
        txn.commit();
    }
    cppbuild::create_db(build_dir / "deps.db");
}

auto run_scan_single(const scan_single_opts_t &opts) -> void
{
    std::filesystem::path build_dir {std::filesystem::weakly_canonical(opts.build_dir)};
    std::filesystem::path db_path {std::filesystem::weakly_canonical(opts.db_file)};
    auto deps_env = cppbuild::load_db(build_dir / "deps.db");
    auto config_env = cppbuild::load_db(build_dir / "config.db", lmdb::env_flags::rdonly);
    cppbuild::scan_single_source(db_path, build_dir, deps_env, config_env);
}

auto run_gen_single_dd(const gen_single_dd_opts_t &opts) -> void
{
    std::filesystem::path build_dir {std::filesystem::weakly_canonical(opts.build_dir)};
    std::filesystem::path src_path {std::filesystem::weakly_canonical(opts.src_file)};
    auto deps_env = cppbuild::load_db(build_dir / "deps.db", lmdb::env_flags::rdonly);
    auto config_env = cppbuild::load_db(build_dir / "config.db", lmdb::env_flags::rdonly);
    cppbuild::generate_single_dyndep(src_path, build_dir, deps_env, config_env);
}

auto run_gen_single_rsp(const gen_single_rsp_opts_t &opts) -> void
{
    std::filesystem::path build_dir {std::filesystem::weakly_canonical(opts.build_dir)};
    std::filesystem::path src_path {std::filesystem::weakly_canonical(opts.src_file)};
    auto deps_env = cppbuild::load_db(build_dir / "deps.db", lmdb::env_flags::rdonly);
    auto config_env = cppbuild::load_db(build_dir / "config.db", lmdb::env_flags::rdonly | lmdb::env_flags::no_lock);
    cppbuild::generate_single_rsp(src_path, build_dir, deps_env, config_env);
}

auto main(int argc, char **argv) -> int
{
    cli::app_t app {"cppbuild — C++ Build System"};

    configure_opts_t configure_opts {};
    gen_p1689_opts_t gen_p1689_opts {};
    scan_single_opts_t scan_single_opts {};
    gen_single_dd_opts_t gen_single_dd_opts {};
    gen_single_rsp_opts_t gen_single_rsp_opts {};
    tc_add_opts_t tc_add_opts {};
    tc_remove_opts_t tc_remove_opts {};

    auto configure = app.add_subcommand("configure", "Configure the project");
    configure->add_option("--script-path", configure_opts.script_path, "Path to build script");
    configure->add_option("--toolchain-name", configure_opts.toolchain_name, "Cached toolchain name")->required();
    configure->add_option("--build-dir", configure_opts.build_dir, "Build directory");
    configure->add_option("--install-dir", configure_opts.install_dir, "Installation prefix");

    auto gen_p1689 = app.add_subcommand("gen_p1689", "Generate P1689 database");
    gen_p1689->add_option("--build-dir", gen_p1689_opts.build_dir, "Build directory");

    auto scan_single = app.add_subcommand("scan_single", "Scan a single source for module dependencies");
    scan_single->add_option("db_file", scan_single_opts.db_file, "P1689 database file")->required();
    scan_single->add_option("--build-dir", scan_single_opts.build_dir, "Build directory");

    auto gen_single_dd = app.add_subcommand("gen_single_dd", "Generate dyndep file for a single source");
    gen_single_dd->add_option("src_file", gen_single_dd_opts.src_file, "Source file path")->required();
    gen_single_dd->add_option("--build-dir", gen_single_dd_opts.build_dir, "Build directory");

    auto gen_single_rsp = app.add_subcommand("gen_single_rsp", "Generate rsp file for a single source");
    gen_single_rsp->add_option("src_file", gen_single_rsp_opts.src_file, "Source file path")->required();
    gen_single_rsp->add_option("--build-dir", gen_single_rsp_opts.build_dir, "Build directory");

    auto toolchain = app.add_subcommand("toolchain", "Manage cached toolchains");
    toolchain->require_subcommand();

    auto tc_add = toolchain->add_subcommand("add", "Add a toolchain to the cache");
    tc_add->add_option("--name", tc_add_opts.name, "Toolchain name")->required();
    tc_add->add_option("--toolchain-path", tc_add_opts.toolchain_path, "Path to toolchain JSON")->required();

    auto tc_remove = toolchain->add_subcommand("remove", "Remove a toolchain from the cache");
    tc_remove->add_option("--name", tc_remove_opts.name, "Toolchain name")->required();

    auto tc_list = toolchain->add_subcommand("list", "List cached toolchains");

    try
    {
        app.parse(argc, argv);
    }
    catch (const cli::parse_error_t &e)
    {
        return app.exit(e);
    }

    std::filesystem::path self_path {std::filesystem::weakly_canonical(argv[0])};

    if (configure->parsed())
    {
        run_configure(configure_opts, self_path);
    }
    else if (scan_single->parsed())
    {
        run_scan_single(scan_single_opts);
    }
    else if (gen_single_dd->parsed())
    {
        run_gen_single_dd(gen_single_dd_opts);
    }
    else if (gen_single_rsp->parsed())
    {
        run_gen_single_rsp(gen_single_rsp_opts);
    }
    else if (tc_add->parsed())
    {
        std::filesystem::path tc_path {std::filesystem::weakly_canonical(tc_add_opts.toolchain_path)};
        cppbuild::add_toolchain(tc_add_opts.name, tc_path, self_path);
    }
    else if (tc_list->parsed())
    {
        cppbuild::list_toolchains(self_path);
    }
    else if (tc_remove->parsed())
    {
        cppbuild::remove_toolchain(tc_remove_opts.name, self_path);
    }

    return 0;
}
