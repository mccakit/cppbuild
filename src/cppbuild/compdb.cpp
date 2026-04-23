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
                            const types::toolchain &tc,
                            lmdbxx::env &deps_env,
                            lmdbxx::env &config_env) -> void
    {
        // Read src_root from config.db first, since we might need it to reconstruct the source path
        std::filesystem::path src_root;
        try
        {
            auto txn = lmdbxx::txn::begin(config_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);
            auto sr = db_get_raw(dbi, txn, "src_root");
            if (!sr)
            {
                std::cerr << "FATAL: Could not find 'src_root' in config.db\n";
                std::exit(1);
            }
            src_root = *sr;
        }
        catch (const lmdbxx::error &e)
        {
            std::cerr << "FATAL: LMDB error reading config_env: " << e.what() << "\n";
            std::exit(1);
        }

        auto proc =
            subprocess::RunBuilder({tc.cxx_scanner, "--format=p1689", "-compilation-database", db_path.string()})
                .cout(subprocess::PipeOption::pipe)
                .run();
        std::string output(proc.cout.begin(), proc.cout.end());

        glz::generic doc {};
        if (auto ec = glz::read_json(doc, output); ec)
        {
            std::cerr << "FATAL: Failed to parse p1689 JSON output from scanner for " << db_path.string() << "\n";
            std::exit(1);
        }

        auto &obj = doc.get<glz::generic::object_t>();
        auto rules_it = obj.find("rules");
        if (rules_it == obj.end())
        {
            std::cerr << "FATAL: No 'rules' found in p1689 output for " << db_path.string() << "\n";
            std::exit(1);
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

            // If this file does NOT provide a module, deduce the source path from primary-output
            if (entry.src.source_path.empty())
            {
                if (auto po_it = rule_obj.find("primary-output"); po_it != rule_obj.end())
                {
                    std::filesystem::path po = po_it->second.get<std::string>();
                    // po is something like: ".../build/target_a/main.cpp.o"
                    // Strip the ".o" (or ".pcm.o") to get back to ".../build/target_a/main.cpp"
                    auto src_in_build = po.replace_extension("");
                    if (src_in_build.extension() == ".pcm")
                    {
                        src_in_build.replace_extension("");
                    }

                    // Map it from the build directory back to the source root
                    auto relative_to_build = src_in_build.lexically_relative(build_dir);
                    entry.src.source_path = (src_root / relative_to_build).string();
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
            std::cerr << "FATAL: Could not determine source_path for scan of " << db_path.string() << "\n";
            std::exit(1);
        }

        try
        {
            auto txn = lmdbxx::txn::begin(deps_env);
            auto dbi = lmdbxx::dbi::open(txn);
            db_put_struct(dbi, txn, entry.src.source_path.string(), entry);
            txn.commit();
        }
        catch (const lmdbxx::error &e)
        {
            std::cerr << "FATAL: LMDB error writing to deps_env: " << e.what() << "\n";
            std::exit(1);
        }

        auto stamp_path = calc_output_path(entry.src.source_path, src_root, build_dir);
        stamp_path += ".scan.stamp";

        std::error_code ec;
        std::filesystem::create_directories(stamp_path.parent_path(), ec);
        if (ec)
        {
            std::cerr << "FATAL: Failed to create directory for stamp: " << ec.message() << "\n";
            std::exit(1);
        }

        std::ofstream stamp(stamp_path);
        if (!stamp)
        {
            std::cerr << "FATAL: Failed to write stamp file: " << stamp_path << "\n";
            std::exit(1);
        }
    }

    auto generate_single_dyndep(const std::filesystem::path &src_path,
                                const std::filesystem::path &build_dir,
                                lmdbxx::env &deps_env,
                                lmdbxx::env &config_env) -> void
    {
        // Step 1: find owner target from config.db, collect its direct deps + own sources.
        types::build_target owner {};
        std::unordered_set<std::string> allowed_sources;
        std::filesystem::path src_root;

        try
        {
            auto txn = lmdbxx::txn::begin(config_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto src_root_str = db_get_raw(dbi, txn, "src_root");
            if (!src_root_str)
            {
                std::cerr << "FATAL: Could not find 'src_root' in config.db\n";
                std::exit(1);
            }
            src_root = *src_root_str;

            auto owner_name = db_get_raw(dbi, txn, "src:" + src_path.string());
            if (!owner_name)
            {
                auto rel_path = src_path.lexically_relative(src_root);
                owner_name = db_get_raw(dbi, txn, "src:" + rel_path.string());
                if (!owner_name)
                {
                    std::cerr << "FATAL: Could not find owner target in config.db for: " << src_path.string() << "\n";
                    std::exit(1);
                }
            }

            auto owner_opt = db_get_struct<types::build_target>(dbi, txn, "bt:" + *owner_name);
            if (!owner_opt)
            {
                std::cerr << "FATAL: Could not find target struct in config.db for: bt:" << *owner_name << "\n";
                std::exit(1);
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

            for (auto const &dep_name : owner.deps)
            {
                if (auto dep_bt = db_get_struct<types::build_target>(dbi, txn, "bt:" + dep_name))
                {
                    collect(*dep_bt);
                }
            }
        }
        catch (const lmdbxx::error &e)
        {
            std::cerr << "LMDB error in config_env (Step 1): " << e.what() << "\n";
            std::exit(1);
        }

        // Step 2: fetch dyndep_entry, build local name mapping, resolve direct deps
        types::dyndep_entry entry {};
        std::vector<std::filesystem::path> direct;
        try
        {
            auto txn = lmdbxx::txn::begin(deps_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto load_entry = [&](const std::filesystem::path &p) -> std::optional<types::dyndep_entry> {
                if (auto e = db_get_struct<types::dyndep_entry>(dbi, txn, p.string()))
                    return e;
                auto rel = p.lexically_relative(src_root);
                if (auto e = db_get_struct<types::dyndep_entry>(dbi, txn, rel.string()))
                    return e;
                return std::nullopt;
            };

            auto self_entry = load_entry(src_path);
            if (!self_entry)
            {
                std::cerr << "FATAL: Could not find dyndep_entry in deps.db for: " << src_path.string() << "\n";
                std::exit(1);
            }
            entry = std::move(*self_entry);

            // FIX: Build the same local logical_name -> source_path map used by RSP generation!
            std::unordered_map<std::string, std::filesystem::path> name_to_src;
            for (auto const &candidate_str : allowed_sources)
            {
                std::filesystem::path candidate(candidate_str);
                if (auto e = load_entry(candidate); e && !e->src.logical_name.empty())
                {
                    name_to_src.emplace(e->src.logical_name, candidate);
                }
            }

            for (auto const &logical_name : entry.deps)
            {
                if (auto it = name_to_src.find(logical_name); it != name_to_src.end())
                {
                    direct.emplace_back(it->second);
                }
            }
        }
        catch (const lmdbxx::error &e)
        {
            std::cerr << "LMDB error in deps_env (Step 2): " << e.what() << "\n";
            std::exit(1);
        }

        // Step 3: generate the Ninja dyndep file.
        auto base_output_path = calc_output_path(entry.src.source_path, src_root, build_dir);

        auto dd_path = base_output_path;
        dd_path += ".dd";

        std::error_code ec;
        std::filesystem::create_directories(dd_path.parent_path(), ec);
        if (ec)
        {
            std::cerr << "FATAL: Failed to create directories for " << dd_path << ": " << ec.message() << "\n";
            std::exit(1);
        }

        std::ofstream dd(dd_path);
        if (!dd)
        {
            std::cerr << "FATAL: Failed to open file for writing: " << dd_path << "\n";
            std::exit(1);
        }

        dd << "ninja_dyndep_version = 1\n";

        std::string dep_pcms;
        for (auto const &dep_src : direct)
        {
            auto dep_pcm = calc_output_path(dep_src, src_root, build_dir);
            dep_pcm += ".pcm";
            dep_pcms += " " + dep_pcm.string();
        }

        if (!entry.src.logical_name.empty())
        {
            auto pcm = base_output_path;
            pcm += ".pcm";
            dd << "build " << pcm.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
        }
        else
        {
            auto obj = base_output_path;
            obj += ".o";
            dd << "build " << obj.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
        }
    }

    auto generate_single_rsp(const std::filesystem::path &src_path,
                             const std::filesystem::path &build_dir,
                             lmdbxx::env &deps_env,
                             lmdbxx::env &config_env) -> void
    {
        auto tc = types::toolchain::load(build_dir / "tc.cache");

        // Step 1: find owner, BFS deps transitively, collect allowed sources + src_root.
        types::build_target owner {};
        std::unordered_set<std::string> allowed_sources;
        std::filesystem::path src_root;
        try
        {
            auto txn = lmdbxx::txn::begin(config_env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto sr = db_get_raw(dbi, txn, "src_root");
            if (!sr)
                return;
            src_root = *sr;

            auto owner_name = db_get_raw(dbi, txn, "src:" + src_path.string());
            if (!owner_name)
                return;

            auto owner_opt = db_get_struct<types::build_target>(dbi, txn, "bt:" + *owner_name);
            if (!owner_opt)
                return;
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
                    continue;
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

        // Step 2: build flag string from owner + toolchain, without empty gaps.
        bool is_c = src_path.extension() == ".c";
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

        if (is_c)
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

        std::string content;
        if (!all_flags.empty())
        {
            content = fmt::format("{}\n", fmt::join(all_flags, "\n"));
        }

        // Step 3: build a local logical_name -> source_path map restricted to allowed_sources,
        // then BFS from src_path resolving via that map.
        if (!is_c)
        {
            try
            {
                auto txn = lmdbxx::txn::begin(deps_env, nullptr, lmdbxx::env_flags::rdonly);
                auto dbi = lmdbxx::dbi::open(txn);

                auto load_entry = [&](const std::filesystem::path &src) -> std::optional<types::dyndep_entry> {
                    return db_get_struct<types::dyndep_entry>(dbi, txn, src.string());
                };

                // Local name -> source map, scoped to reachable targets only.
                std::unordered_map<std::string, std::filesystem::path> name_to_src;
                for (auto const &candidate : allowed_sources)
                {
                    if (auto e = load_entry(candidate); e && !e->src.logical_name.empty())
                    {
                        name_to_src.emplace(e->src.logical_name, candidate);
                    }
                }

                auto lookup_name = [&](const std::string &name) -> std::filesystem::path {
                    auto it = name_to_src.find(name);
                    if (it == name_to_src.end())
                    {
                        return {};
                    }
                    return it->second;
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

                    auto dep_pcm = calc_output_path(dep_src, src_root, build_dir);
                    dep_pcm += ".pcm";
                    fmt::format_to(std::back_inserter(content), "-fmodule-file={}={}\n", name, dep_pcm.string());

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

        auto rsp_path = calc_output_path(src_path, src_root, build_dir);
        rsp_path += ".rsp";
        std::filesystem::create_directories(rsp_path.parent_path());
        auto out = fmt::output_file(rsp_path.string());
        out.print("{}", content);
    }
} // namespace cppbuild
