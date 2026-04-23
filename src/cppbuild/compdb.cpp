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
                    // Push the logical name directly as a string
                    entry.deps.push_back(req_obj["logical-name"].get<std::string>());
                }
            }
        }
        if (entry.src.source_path.empty())
        {
            return;
        }
        auto [data, out] = zpp::bits::data_out();
        out(entry).or_throw();

        try
        {
            // Begin transaction using the provided environment and open the default database
            auto txn = lmdbxx::txn::begin(env);
            auto dbi = lmdbxx::dbi::open(txn);

            // Insert primary record
            auto key_str = entry.src.source_path.string();
            std::string_view value_sv {reinterpret_cast<const char *>(data.data()), data.size()};
            dbi.put(txn, key_str, value_sv);

            // Insert logical name index if applicable
            if (!entry.src.logical_name.empty())
            {
                auto name_key_str = "n:" + entry.src.logical_name;
                auto name_val_str = entry.src.source_path.string();
                dbi.put(txn, name_key_str, name_val_str);
            }

            txn.commit();
        }
        catch (const lmdbxx::error &)
        {
            // Silently return on error, matching the original code's behavior
            // RAII will automatically clean up the txn and dbi handles
            return;
        }

        auto stamp_path = db_path.parent_path() / (db_path.stem().stem().string() + ".scan.stamp");
        std::ofstream stamp(stamp_path);
    }

    auto generate_single_dyndep(const std::filesystem::path &src_path,
                                const std::filesystem::path &build_dir,
                                lmdbxx::env &env) -> void
    {
        types::dyndep_entry entry {};
        std::vector<std::filesystem::path> direct {};

        try
        {
            // Begin a read-only transaction using the provided environment
            auto txn = lmdbxx::txn::begin(env, nullptr, lmdbxx::env_flags::rdonly);
            auto dbi = lmdbxx::dbi::open(txn);

            auto self_key_str = src_path.string();
            std::string_view self_val_sv;

            // Fetch the primary entry
            if (!dbi.get(txn, self_key_str, self_val_sv))
            {
                return;
            }

            // Convert string_view back to std::span<const std::byte>
            std::span<const std::byte> self_bytes {reinterpret_cast<const std::byte *>(self_val_sv.data()),
                                                   self_val_sv.size()};
            entry = types::dyndep_entry::load_from_buffer(self_bytes);

            // Fetch dependencies
            for (auto const &dep_logical_name : entry.deps)
            {
                // dep_logical_name is now directly the string
                auto name_key_str = "n:" + dep_logical_name;
                std::string_view name_val_sv;
                if (dbi.get(txn, name_key_str, name_val_sv))
                {
                    direct.emplace_back(name_val_sv);
                }
            }

            // Read-only transaction is automatically aborted here by RAII, releasing the read lock
        }
        catch (const lmdbxx::error &)
        {
            // Silently return on database errors, matching original logic
            return;
        }

        // Generate the Ninja dyndep file
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
                             lmdbxx::env &env) -> void
    {
        auto tc = types::toolchain::load(build_dir / "tc.cache");
        auto build_cache = types::build_cache::load(build_dir / "build.cache");

        const types::build_target *owner = nullptr;
        for (auto const &bt : build_cache.build_targets)
        {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    if (src == src_path)
                    {
                        owner = &bt;
                        break;
                    }
                }
                if (owner)
                {
                    break;
                }
            }
            if (owner)
            {
                break;
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.path == src_path)
                    {
                        owner = &bt;
                        break;
                    }
                }
                if (owner)
                {
                    break;
                }
            }
            if (owner)
            {
                break;
            }
        }

        if (!owner)
        {
            return;
        }

        bool is_c = src_path.extension() == ".c";
        std::string content;
        if (is_c)
        {
            content = fmt::format("{}\n{}\n{}\n",
                                  fmt::join(tc.cflags, "\n"),
                                  fmt::join(owner->cflags.public_, "\n"),
                                  fmt::join(owner->cflags.private_, "\n"));
        }
        else
        {
            content = fmt::format("{}\n{}\n{}\n",
                                  fmt::join(tc.cxxflags, "\n"),
                                  fmt::join(owner->cxxflags.public_, "\n"),
                                  fmt::join(owner->cxxflags.private_, "\n"));
        }

        if (!is_c)
        {
            try
            {
                // Begin read-only transaction using the provided environment
                auto txn = lmdbxx::txn::begin(env, nullptr, lmdbxx::env_flags::rdonly);
                auto dbi = lmdbxx::dbi::open(txn);

                auto load_entry = [&](const std::filesystem::path &src) -> std::optional<types::dyndep_entry> {
                    std::string_view value_sv;
                    if (!dbi.get(txn, src.string(), value_sv))
                    {
                        return std::nullopt;
                    }
                    std::span<const std::byte> bytes {reinterpret_cast<const std::byte *>(value_sv.data()),
                                                      value_sv.size()};
                    return types::dyndep_entry::load_from_buffer(bytes);
                };

                auto lookup_name = [&](const std::string &name) -> std::filesystem::path {
                    auto key_str = "n:" + name;
                    std::string_view value_sv;
                    if (!dbi.get(txn, key_str, value_sv))
                    {
                        return {};
                    }
                    return std::filesystem::path(value_sv);
                };

                std::unordered_set<std::string> visited {};
                std::queue<std::string> frontier {};

                if (auto self_entry = load_entry(src_path); self_entry)
                {
                    for (auto const &dep_logical_name : self_entry->deps)
                    {
                        if (visited.insert(dep_logical_name).second)
                        {
                            // dep_logical_name is now a string
                            frontier.push(dep_logical_name);
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
                        for (auto const &next_logical_name : dep_entry->deps)
                        {
                            if (visited.insert(next_logical_name).second)
                            {
                                // next_logical_name is now a string
                                frontier.push(next_logical_name);
                            }
                        }
                    }
                }
            }
            catch (const lmdbxx::error &)
            {
                // Silently swallow database errors and proceed without module flags,
                // matching the behavior of the original C code.
            }
        }

        auto rsp_path = build_dir / (src_path.filename().string() + ".rsp");
        auto out = fmt::output_file(rsp_path.string());
        out.print("{}", content);
    }
} // namespace cppbuild
