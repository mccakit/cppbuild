module;
export module cppbuild:compdb;
import std;
import graaf;
import fmt;
import subprocess;
import glaze;
import bs.thread_pool;
import lmdb;
import :types;
import :cache;
import :helpers;

export namespace cppbuild
{
    auto generate_compdb_per_source(const std::filesystem::path &src_root,
                                    const std::filesystem::path &build_dir,
                                    const types::toolchain &tc,
                                    const std::vector<types::build_target> &targets) -> void
    {
        const auto cxxflags_str = fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        const auto dir = build_dir.string();

        auto write_db = [&](const std::filesystem::path &src, std::string_view kind, const std::string &flags) {
            if (src.extension() == ".c")
            {
                return;
            }

            const auto ext = kind == "named_module" ? ".pcm.o" : ".o";
            const auto src_str = src.string();
            auto mirror = calc_output_path(src, src_root, build_dir);
            const auto obj = mirror.string() + ext;
            const auto db_path = mirror.string() + ".p1689.json";

            std::filesystem::create_directories(std::filesystem::path(db_path).parent_path());
            auto out = fmt::output_file(db_path);
            out.print("[\n"
                      "  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {} -c {} -o {}\",\"output\":\"{}\"}}\n"
                      "]\n",
                      dir,
                      src_str,
                      tc.cxx_compiler,
                      flags,
                      src_str,
                      obj,
                      obj);
        };

        for (const auto &bt : targets)
        {
            const auto flags = fmt::format(
                "{} {} {} ", cxxflags_str, fmt::join(bt.cxxflags.public_, " "), fmt::join(bt.cxxflags.private_, " "));

            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    write_db(src, sg.kind, flags);
                }
            }

            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    write_db(go.path, go.kind, flags);
                }
            }
        }
    }

    auto generate_compile_commands(const std::filesystem::path &build_dir,
                                   const types::toolchain &tc,
                                   const std::vector<types::build_target> &targets) -> void
    {
        auto out = fmt::output_file((build_dir / "compile_commands.json").string());
        const auto dir = build_dir.string();
        auto entries = std::vector<std::string> {};

        auto add_entry = [&](const std::filesystem::path &src, std::string_view kind) {
            const bool is_c = src.extension() == ".c";
            const auto &compiler = is_c ? tc.c_compiler : tc.cxx_compiler;
            if (compiler.empty())
            {
                return;
            }

            const auto src_str = src.string();
            const auto ext = kind == "named_module" ? ".pcm.o" : ".o";
            const auto obj = (build_dir / (src.filename().string() + ext)).string();
            const auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();

            entries.push_back(fmt::format(
                "  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} -c {} -o {} @{}\",\"output\":\"{}\"}}",
                dir,
                src_str,
                compiler,
                src_str,
                obj,
                rsp,
                obj));
        };

        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    add_entry(src, sg.kind);
                }
            }

            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    add_entry(go.path, go.kind);
                }
            }
        }

        out.print("[\n{}\n]\n", fmt::join(entries, ",\n"));
    }

    auto scan_single_source(const std::filesystem::path &db_path,
                            const std::filesystem::path &build_dir,
                            lmdb::env &deps_env,
                            lmdb::env &config_env) -> void
    {
        auto txn = lmdb::txn::begin(config_env, nullptr, lmdb::env_flags::rdonly);
        auto dbi = lmdb::dbi::open(txn);

        auto src_root = *db_get_raw(dbi, txn, "src_root");
        auto tc = *db_get_struct<types::toolchain>(dbi, txn, "toolchain");

        auto scan =
            subprocess::run_builder_t({tc.cxx_scanner, "--format=p1689", "-compilation-database", db_path.string()})
                .cout(subprocess::pipe_option_t::pipe)
                .cerr(subprocess::pipe_option_t::pipe)
                .run();

        if (!scan)
        {
            throw std::runtime_error(fmt::format(
                "{} failed ({}) scanning {}:\n{}", tc.cxx_scanner, scan.returncode, db_path.string(), scan.cerr));
        }

        glz::generic doc {};
        glz::read_json(doc, scan.cout);

        auto &obj = doc.get<glz::generic::object_t>();
        types::dyndep_entry entry {};

        for (auto &rule : obj["rules"].get<glz::generic::array_t>())
        {
            auto &rule_obj = rule.get<glz::generic::object_t>();

            if (auto it = rule_obj.find("provides"); it != rule_obj.end())
            {
                for (auto &provides : it->second.get<glz::generic::array_t>())
                {
                    auto &prov_obj = provides.get<glz::generic::object_t>();
                    entry.src.logical_name = prov_obj["logical-name"].get<std::string>();

                    if (auto sp_it = prov_obj.find("source-path"); sp_it != prov_obj.end())
                    {
                        entry.src.source_path = sp_it->second.get<std::string>();
                    }
                }
            }

            if (entry.src.source_path.empty())
            {
                std::filesystem::path po = rule_obj["primary-output"].get<std::string>();
                auto src_in_build = po.replace_extension("");
                if (src_in_build.extension() == ".pcm")
                {
                    src_in_build.replace_extension("");
                }
                entry.src.source_path = (src_root / src_in_build.lexically_relative(build_dir)).string();
            }

            if (auto it = rule_obj.find("requires"); it != rule_obj.end())
            {
                for (auto &req : it->second.get<glz::generic::array_t>())
                {
                    entry.deps.push_back(req.get<glz::generic::object_t>()["logical-name"].get<std::string>());
                }
            }
        }

        auto deps_txn = lmdb::txn::begin(deps_env);
        auto deps_dbi = lmdb::dbi::open(deps_txn);
        db_put_struct(deps_dbi, deps_txn, entry.src.source_path.string(), entry);
        deps_txn.commit();

        auto stamp_path = calc_output_path(entry.src.source_path, src_root, build_dir);
        stamp_path += ".scan.stamp";
        std::filesystem::create_directories(stamp_path.parent_path());

        // FIXED LINE: Use brace initialization to create a temporary stream
        // that creates the file and immediately closes it.
        std::ofstream {stamp_path};
    }

    auto collect_allowed_sources(const std::filesystem::path &src_path, lmdb::env &config_env)
        -> std::tuple<std::filesystem::path, types::toolchain, types::build_target, std::unordered_set<std::string>>
    {
        std::unordered_set<std::string> allowed_sources;

        auto txn = lmdb::txn::begin(config_env, nullptr, lmdb::env_flags::rdonly);
        auto dbi = lmdb::dbi::open(txn);

        auto src_root = *db_get_raw(dbi, txn, "src_root");
        auto tc = *db_get_struct<types::toolchain>(dbi, txn, "toolchain");

        auto owner_name_opt = db_get_raw(dbi, txn, "src:" + src_path.string());
        auto owner_name = owner_name_opt
                              ? *owner_name_opt
                              : *db_get_raw(dbi, txn, "src:" + src_path.lexically_relative(src_root).string());
        auto owner = *db_get_struct<types::build_target>(dbi, txn, "bt:" + owner_name);

        auto collect = [&](const types::build_target &bt) {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    allowed_sources.insert(src.string());
                }
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    allowed_sources.insert(go.path.string());
                }
            }
        };

        collect(owner);

        std::unordered_set<std::string> visited_targets {owner_name};
        std::queue<std::string> frontier;

        for (auto const &dep_name : owner.deps)
        {
            if (visited_targets.insert(dep_name).second)
            {
                frontier.push(dep_name);
            }
        }

        while (!frontier.empty())
        {
            auto name = std::move(frontier.front());
            frontier.pop();

            if (auto bt = db_get_struct<types::build_target>(dbi, txn, "bt:" + name))
            {
                collect(*bt);
                for (auto const &dep_name : bt->deps)
                {
                    if (visited_targets.insert(dep_name).second)
                    {
                        frontier.push(dep_name);
                    }
                }
            }
        }

        return {std::move(src_root), std::move(tc), std::move(owner), std::move(allowed_sources)};
    }

    auto find_direct_deps(const std::filesystem::path &src_path,
                          const std::filesystem::path &src_root,
                          const std::unordered_set<std::string> &allowed_sources,
                          lmdb::env &deps_env) -> std::pair<types::dyndep_entry, std::vector<std::filesystem::path>>
    {
        auto txn = lmdb::txn::begin(deps_env, nullptr, lmdb::env_flags::rdonly);
        auto dbi = lmdb::dbi::open(txn);

        auto load_entry = [&](const std::filesystem::path &p) -> types::dyndep_entry {
            if (auto e = db_get_struct<types::dyndep_entry>(dbi, txn, p.string()))
            {
                return *e;
            }
            return *db_get_struct<types::dyndep_entry>(dbi, txn, p.lexically_relative(src_root).string());
        };

        auto entry = load_entry(src_path);
        std::unordered_map<std::string, std::filesystem::path> name_to_src;

        for (auto const &candidate_str : allowed_sources)
        {
            std::filesystem::path candidate(candidate_str);
            if (auto e = db_get_struct<types::dyndep_entry>(dbi, txn, candidate.string());
                e && !e->src.logical_name.empty())
            {
                name_to_src.emplace(e->src.logical_name, candidate);
            }
        }

        std::vector<std::filesystem::path> direct;
        for (auto const &logical_name : entry.deps)
        {
            if (auto it = name_to_src.find(logical_name); it != name_to_src.end())
            {
                direct.emplace_back(it->second);
            }
        }

        return {std::move(entry), std::move(direct)};
    }

    auto build_flag_string(const std::filesystem::path &src_path,
                           const types::toolchain &tc,
                           const types::build_target &owner) -> std::string
    {
        std::vector<std::string> all_flags;
        auto add_flags = [&](const std::vector<std::string> &flags) {
            for (const auto &flag : flags)
            {
                if (!flag.empty())
                {
                    all_flags.push_back(flag);
                }
            }
        };

        if (src_path.extension() == ".c")
        {
            add_flags(tc.cflags);
            add_flags(owner.cflags.public_);
            add_flags(owner.cflags.private_);
        }
        else
        {
            add_flags(tc.cxxflags);
            add_flags(owner.cxxflags.public_);
            add_flags(owner.cxxflags.private_);
        }

        if (all_flags.empty())
        {
            return "";
        }

        return fmt::format("{}\n", fmt::join(all_flags, "\n"));
    }

    auto find_transitive_deps(const std::filesystem::path &src_path,
                              const std::filesystem::path &src_root,
                              const std::filesystem::path &build_dir,
                              const std::unordered_set<std::string> &allowed_sources,
                              lmdb::env &deps_env) -> std::string
    {
        auto txn = lmdb::txn::begin(deps_env, nullptr, lmdb::env_flags::rdonly);
        auto dbi = lmdb::dbi::open(txn);

        auto load_entry = [&](const std::filesystem::path &src) {
            return db_get_struct<types::dyndep_entry>(dbi, txn, src.string());
        };

        std::unordered_map<std::string, std::filesystem::path> name_to_src;
        for (auto const &candidate : allowed_sources)
        {
            if (auto e = load_entry(candidate); e && !e->src.logical_name.empty())
            {
                name_to_src.emplace(e->src.logical_name, candidate);
            }
        }

        std::unordered_set<std::string> visited;
        std::queue<std::string> frontier;

        if (auto self_entry = load_entry(src_path))
        {
            for (auto const &logical_name : self_entry->deps)
            {
                if (visited.insert(logical_name).second)
                {
                    frontier.push(logical_name);
                }
            }
        }

        std::string out;
        while (!frontier.empty())
        {
            auto name = std::move(frontier.front());
            frontier.pop();

            auto it = name_to_src.find(name);
            if (it == name_to_src.end())
            {
                continue;
            }

            auto dep_src = it->second;
            auto dep_pcm = calc_output_path(dep_src, src_root, build_dir);
            dep_pcm += ".pcm";
            fmt::format_to(std::back_inserter(out), "-fmodule-file={}={}\n", name, dep_pcm.string());

            if (auto dep_entry = load_entry(dep_src))
            {
                for (auto const &next_name : dep_entry->deps)
                {
                    if (visited.insert(next_name).second)
                    {
                        frontier.push(next_name);
                    }
                }
            }
        }
        return out;
    }

    auto generate_single_dyndep(const std::filesystem::path &src_path,
                                const std::filesystem::path &build_dir,
                                lmdb::env &deps_env,
                                lmdb::env &config_env) -> void
    {
        auto [src_root, tc, owner, allowed_sources] = collect_allowed_sources(src_path, config_env);
        auto [entry, direct] = find_direct_deps(src_path, src_root, allowed_sources, deps_env);

        auto base_output_path = calc_output_path(entry.src.source_path, src_root, build_dir);
        auto dd_path = base_output_path;
        dd_path += ".dd";

        std::filesystem::create_directories(dd_path.parent_path());
        std::ofstream dd(dd_path);

        dd << "ninja_dyndep_version = 1\n";

        std::string dep_pcms;
        for (auto const &dep_src : direct)
        {
            auto dep_pcm = calc_output_path(dep_src, src_root, build_dir);
            dep_pcm += ".pcm";
            dep_pcms += " " + dep_pcm.string();
        }

        auto target = base_output_path;
        if (!entry.src.logical_name.empty())
        {
            target += ".pcm";
        }
        else
        {
            target += ".o";
        }

        dd << "build " << target.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
    }

    auto generate_single_rsp(const std::filesystem::path &src_path,
                             const std::filesystem::path &build_dir,
                             lmdb::env &deps_env,
                             lmdb::env &config_env) -> void
    {
        auto [src_root, tc, owner, allowed_sources] = collect_allowed_sources(src_path, config_env);
        std::string content = build_flag_string(src_path, tc, owner);

        bool is_c = src_path.extension() == ".c";
        if (!is_c)
        {
            content += find_transitive_deps(src_path, src_root, build_dir, allowed_sources, deps_env);
        }

        auto rsp_path = calc_output_path(src_path, src_root, build_dir);
        rsp_path += ".rsp";
        std::filesystem::create_directories(rsp_path.parent_path());

        auto out = fmt::output_file(rsp_path.string());
        out.print("{}", content);
    }
} // namespace cppbuild
