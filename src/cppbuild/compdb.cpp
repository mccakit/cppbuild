module;
export module cppbuild:compdb;
import std;
import graaf;
import fmt;
import subprocess;
import glaze;
import bs.thread_pool;
import lmdbxx;
import :types;
import :cache;

export namespace cppbuild
{
    auto generate_single_p1689(const std::filesystem::path &source,
                               const std::filesystem::path &build_dir,
                               const types::toolchain &tc,
                               const std::vector<types::build_target> &targets) -> void
    {
        const auto cxxflags_str = fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        const auto dir = build_dir.string();

        std::string kind;
        std::string flags;

        for (const auto &bt : targets)
        {
            const auto bt_flags = fmt::format(
                "{} {} {} ", cxxflags_str, fmt::join(bt.cxxflags.public_, " "), fmt::join(bt.cxxflags.private_, " "));
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    if (src == source)
                    {
                        kind = sg.kind;
                        flags = bt_flags;
                    }
                }
            }
            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    if (go.path == source)
                    {
                        kind = go.kind;
                        flags = bt_flags;
                    }
                }
            }
        }

        if (kind.empty())
        {
            return;
        }

        const auto ext = kind == "named_module" ? ".pcm.o" : ".o";
        const auto src_str = source.string();
        const auto obj = (build_dir / (source.filename().string() + ext)).string();
        const auto db_path = (build_dir / (source.filename().string() + ".p1689.json")).string();

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
    }

    auto generate_compdb_per_source(const std::filesystem::path &build_dir,
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
            const auto obj = (build_dir / (src.filename().string() + ext)).string();
            const auto db_path = (build_dir / (src.filename().string() + ".p1689.json")).string();
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

    auto scan_single_source(const std::filesystem::path &db_path, const types::toolchain &tc, lmdbxx::env &env) -> void
    {
        auto proc =
            subprocess::RunBuilder({tc.cxx_scanner, "--format=p1689", "-compilation-database", db_path.string()})
                .cout(subprocess::PipeOption::pipe)
                .run();
        std::string output(proc.cout.begin(), proc.cout.end());
        glz::generic doc {};
        if (auto ec = glz::read_json(doc, output); ec)
        {
            return;
        }
        auto &obj = doc.get<glz::generic::object_t>();
        auto rules_it = obj.find("rules");
        if (rules_it == obj.end())
        {
            return;
        }
        types::dyndep_entry entry {};
        for (auto &rule : rules_it->second.get<glz::generic::array_t>())
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
            if (auto it = rule_obj.find("requires"); it != rule_obj.end())
            {
                for (auto &req : it->second.get<glz::generic::array_t>())
                {
                    auto &req_obj = req.get<glz::generic::object_t>();
                    entry.deps.push_back(req_obj["logical-name"].get<std::string>());
                }
            }
        }
        if (entry.src.source_path.empty())
        {
            return;
        }
        try
        {
            auto txn = lmdbxx::txn::begin(env);
            auto dbi = lmdbxx::dbi::open(txn);
            // Primary record
            db_put_struct(dbi, txn, entry.src.source_path.string(), entry);
            // Logical name index
            if (!entry.src.logical_name.empty())
            {
                db_put_raw(dbi, txn, "n:" + entry.src.logical_name, entry.src.source_path.string());
            }
            txn.commit();
        }
        catch (const lmdbxx::error &)
        {
            return;
        }
        auto stamp_path = db_path.parent_path() / (db_path.stem().stem().string() + ".scan.stamp");
        std::ofstream stamp(stamp_path);
    }

    auto generate_single_dyndep(const std::filesystem::path &src_path,
                                const std::filesystem::path &build_dir,
                                lmdbxx::env &deps_env,
                                lmdbxx::env &config_env) -> void
    {
        // Step 1: find owner target from config.db, collect its direct deps + own sources.
        // For .dd files, we only need direct deps — transitive walking happens in .rsp generation.
        types::build_target owner {};
        std::unordered_set<std::string> allowed_sources;
        try
        {
            auto txn = lmdbxx::txn::begin(config_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto owner_name = db_get_raw(dbi, txn, "src:" + src_path.string());
            if (!owner_name)
            {
                return;
            }

            auto owner_opt = db_get_struct<types::build_target>(dbi, txn, "bt:" + *owner_name);
            if (!owner_opt)
            {
                return;
            }
            owner = std::move(*owner_opt);

            auto collect = [&](const types::build_target &bt) {
                for (auto const &sg : bt.srcs)
                    for (auto const &src : sg.srcs)
                        allowed_sources.insert(src.string());
                for (auto const &gg : bt.gen_groups)
                    for (auto const &go : gg.outputs)
                        allowed_sources.insert(go.path.string());
            };

            collect(owner);

            // Direct deps only — no transitive walk.
            for (auto const &dep_name : owner.deps)
            {
                if (auto dep_bt = db_get_struct<types::build_target>(dbi, txn, "bt:" + dep_name))
                {
                    collect(*dep_bt);
                }
            }
        }
        catch (const lmdbxx::error &)
        {
            return;
        }

        // Step 2: fetch dyndep_entry for this source, resolve its deps via deps.db,
        // filtering by allowed_sources.
        types::dyndep_entry entry {};
        std::vector<std::filesystem::path> direct;
        try
        {
            auto txn = lmdbxx::txn::begin(deps_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto self_entry = db_get_struct<types::dyndep_entry>(dbi, txn, src_path.string());
            if (!self_entry)
            {
                return;
            }
            entry = std::move(*self_entry);

            for (auto const &logical_name : entry.deps)
            {
                if (auto src = db_get_raw(dbi, txn, "n:" + logical_name))
                {
                    if (allowed_sources.contains(*src))
                    {
                        direct.emplace_back(*src);
                    }
                }
            }
        }
        catch (const lmdbxx::error &)
        {
            return;
        }

        // Step 3: generate the Ninja dyndep file.
        auto dd_path = build_dir / (entry.src.source_path.filename().string() + ".dd");
        std::ofstream dd(dd_path);
        dd << "ninja_dyndep_version = 1\n";
        std::string dep_pcms;
        for (auto const &dep_src : direct)
        {
            auto dep_pcm = build_dir / (dep_src.filename().string() + ".pcm");
            dep_pcms += " " + dep_pcm.string();
        }
        if (!entry.src.logical_name.empty())
        {
            auto pcm = build_dir / (entry.src.source_path.filename().string() + ".pcm");
            dd << "build " << pcm.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
        }
        else
        {
            auto obj = build_dir / (entry.src.source_path.filename().string() + ".o");
            dd << "build " << obj.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
        }
    }

    auto generate_single_rsp(const std::filesystem::path &src_path,
                             const std::filesystem::path &build_dir,
                             lmdbxx::env &deps_env,
                             lmdbxx::env &config_env) -> void
    {
        auto tc = types::toolchain::load(build_dir / "tc.cache");

        // Step 1: find owner target, BFS deps transitively, collect allowed sources.
        types::build_target owner {};
        std::unordered_set<std::string> allowed_sources;
        try
        {
            auto txn = lmdbxx::txn::begin(config_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto owner_name = db_get_raw(dbi, txn, "src:" + src_path.string());
            if (!owner_name)
            {
                return;
            }

            auto owner_opt = db_get_struct<types::build_target>(dbi, txn, "bt:" + *owner_name);
            if (!owner_opt)
            {
                return;
            }
            owner = std::move(*owner_opt);

            auto collect = [&](const types::build_target &bt) {
                for (auto const &sg : bt.srcs)
                    for (auto const &src : sg.srcs)
                        allowed_sources.insert(src.string());
                for (auto const &gg : bt.gen_groups)
                    for (auto const &go : gg.outputs)
                        allowed_sources.insert(go.path.string());
            };

            collect(owner);

            std::unordered_set<std::string> visited_targets;
            std::queue<std::string> frontier;
            visited_targets.insert(*owner_name);
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

                auto bt = db_get_struct<types::build_target>(dbi, txn, "bt:" + name);
                if (!bt)
                {
                    continue;
                }
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
        catch (const lmdbxx::error &)
        {
            return;
        }

        // Step 2: build flag string from owner + toolchain.
        bool is_c = src_path.extension() == ".c";
        std::string content;
        if (is_c)
        {
            content = fmt::format("{}\n{}\n{}\n",
                                  fmt::join(tc.cflags, "\n"),
                                  fmt::join(owner.cflags.public_, "\n"),
                                  fmt::join(owner.cflags.private_, "\n"));
        }
        else
        {
            content = fmt::format("{}\n{}\n{}\n",
                                  fmt::join(tc.cxxflags, "\n"),
                                  fmt::join(owner.cxxflags.public_, "\n"),
                                  fmt::join(owner.cxxflags.private_, "\n"));
        }

        // Step 3: BFS deps.db from src_path, filtering resolution by allowed_sources.
        if (!is_c)
        {
            try
            {
                auto txn = lmdbxx::txn::begin(deps_env, nullptr, lmdbxx::env_flags::rdonly);
                auto dbi = lmdbxx::dbi::open(txn);

                auto load_entry = [&](const std::filesystem::path &src) -> std::optional<types::dyndep_entry> {
                    return db_get_struct<types::dyndep_entry>(dbi, txn, src.string());
                };

                auto lookup_name = [&](const std::string &name) -> std::filesystem::path {
                    auto src = db_get_raw(dbi, txn, "n:" + name);
                    if (!src || !allowed_sources.contains(*src))
                    {
                        return {};
                    }
                    return *src;
                };

                std::unordered_set<std::string> visited;
                std::queue<std::string> frontier;

                if (auto self_entry = load_entry(src_path); self_entry)
                {
                    for (auto const &logical_name : self_entry->deps)
                    {
                        if (visited.insert(logical_name).second)
                        {
                            frontier.push(logical_name);
                        }
                    }
                }

                while (!frontier.empty())
                {
                    auto name = std::move(frontier.front());
                    frontier.pop();

                    auto dep_src = lookup_name(name);
                    if (dep_src.empty())
                    {
                        continue;
                    }

                    fmt::format_to(std::back_inserter(content),
                                   "-fmodule-file={}={}/{}.pcm\n",
                                   name,
                                   build_dir.string(),
                                   dep_src.filename().string());

                    if (auto dep_entry = load_entry(dep_src); dep_entry)
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
            }
            catch (const lmdbxx::error &)
            {
                // Swallow db errors and proceed without module flags.
            }
        }

        auto rsp_path = build_dir / (src_path.filename().string() + ".rsp");
        auto out = fmt::output_file(rsp_path.string());
        out.print("{}", content);
    }
} // namespace cppbuild
