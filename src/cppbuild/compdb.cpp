module;
#include <lmdb.h>
export module cppbuild:compdb;
import std;
import graaf;
import fmt;
import subprocess;
import glaze;
import bs.thread_pool;

import :types;

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

    auto scan_single_source(const std::filesystem::path &db_path, const types::toolchain &tc) -> void
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
                    types::cxx_module dep {};
                    dep.logical_name = req_obj["logical-name"].get<std::string>();
                    entry.deps.push_back(std::move(dep));
                }
            }
        }
        if (entry.src.source_path.empty())
        {
            return;
        }
        auto [data, out] = zpp::bits::data_out();
        out(entry).or_throw();
        auto lmdb_path = db_path.parent_path() / "deps.db";
        MDB_env *env = nullptr;
        if (mdb_env_create(&env) != 0)
        {
            return;
        }
        mdb_env_set_mapsize(env, static_cast<std::size_t>(1) << 30);
        if (mdb_env_open(env, lmdb_path.string().c_str(), MDB_NOSUBDIR, 0644) != 0)
        {
            mdb_env_close(env);
            return;
        }
        MDB_txn *txn = nullptr;
        if (mdb_txn_begin(env, nullptr, 0, &txn) != 0)
        {
            mdb_env_close(env);
            return;
        }
        MDB_dbi dbi {};
        if (mdb_dbi_open(txn, nullptr, 0, &dbi) != 0)
        {
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return;
        }
        auto key_str = entry.src.source_path.string();
        MDB_val key {key_str.size(), key_str.data()};
        MDB_val value {data.size(), data.data()};
        if (mdb_put(txn, dbi, &key, &value, 0) != 0)
        {
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return;
        }
        if (!entry.src.logical_name.empty())
        {
            auto name_key_str = "n:" + entry.src.logical_name;
            auto name_val_str = entry.src.source_path.string();
            MDB_val name_key {name_key_str.size(), name_key_str.data()};
            MDB_val name_val {name_val_str.size(), name_val_str.data()};
            if (mdb_put(txn, dbi, &name_key, &name_val, 0) != 0)
            {
                mdb_txn_abort(txn);
                mdb_env_close(env);
                return;
            }
        }
        mdb_txn_commit(txn);
        mdb_env_close(env);
        auto stamp_path = db_path.parent_path() / (db_path.stem().stem().string() + ".scan.stamp");
        std::ofstream stamp(stamp_path);
    }

    auto generate_single_dyndep(const std::filesystem::path &src_path, const std::filesystem::path &build_dir) -> void
    {
        auto lmdb_path = build_dir / "deps.db";
        MDB_env *env = nullptr;
        if (mdb_env_create(&env) != 0)
        {
            return;
        }
        mdb_env_set_mapsize(env, static_cast<std::size_t>(1) << 30);
        if (mdb_env_open(env, lmdb_path.string().c_str(), MDB_NOSUBDIR | MDB_RDONLY, 0644) != 0)
        {
            mdb_env_close(env);
            return;
        }
        MDB_txn *txn = nullptr;
        if (mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn) != 0)
        {
            mdb_env_close(env);
            return;
        }
        MDB_dbi dbi {};
        if (mdb_dbi_open(txn, nullptr, 0, &dbi) != 0)
        {
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return;
        }
        auto self_key_str = src_path.string();
        MDB_val self_key {self_key_str.size(), self_key_str.data()};
        MDB_val self_val {};
        if (mdb_get(txn, dbi, &self_key, &self_val) != 0)
        {
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return;
        }
        std::span<const std::byte> self_bytes {static_cast<const std::byte *>(self_val.mv_data), self_val.mv_size};
        auto entry = types::dyndep_entry::load_from_buffer(self_bytes);
        std::vector<std::filesystem::path> direct {};
        for (auto const &dep : entry.deps)
        {
            auto name_key_str = "n:" + dep.logical_name;
            MDB_val name_key {name_key_str.size(), name_key_str.data()};
            MDB_val name_val {};
            if (mdb_get(txn, dbi, &name_key, &name_val) != 0)
            {
                continue;
            }
            direct.emplace_back(std::string_view(static_cast<const char *>(name_val.mv_data), name_val.mv_size));
        }
        mdb_txn_abort(txn);
        mdb_env_close(env);
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

    auto generate_single_rsp(const std::filesystem::path &src_path, const std::filesystem::path &build_dir) -> void
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
            auto lmdb_path = build_dir / "deps.db";
            MDB_env *env = nullptr;
            if (mdb_env_create(&env) == 0)
            {
                mdb_env_set_mapsize(env, static_cast<std::size_t>(1) << 30);
                if (mdb_env_open(env, lmdb_path.string().c_str(), MDB_NOSUBDIR | MDB_RDONLY, 0644) == 0)
                {
                    MDB_txn *txn = nullptr;
                    if (mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn) == 0)
                    {
                        MDB_dbi dbi {};
                        if (mdb_dbi_open(txn, nullptr, 0, &dbi) == 0)
                        {
                            auto load_entry =
                                [&](const std::filesystem::path &src) -> std::optional<types::dyndep_entry> {
                                auto key_str = src.string();
                                MDB_val key {key_str.size(), key_str.data()};
                                MDB_val value {};
                                if (mdb_get(txn, dbi, &key, &value) != 0)
                                {
                                    return std::nullopt;
                                }
                                std::span<const std::byte> bytes {static_cast<const std::byte *>(value.mv_data),
                                                                  value.mv_size};
                                return types::dyndep_entry::load_from_buffer(bytes);
                            };

                            auto lookup_name = [&](const std::string &name) -> std::filesystem::path {
                                auto key_str = "n:" + name;
                                MDB_val key {key_str.size(), key_str.data()};
                                MDB_val value {};
                                if (mdb_get(txn, dbi, &key, &value) != 0)
                                {
                                    return {};
                                }
                                return std::filesystem::path(
                                    std::string_view(static_cast<const char *>(value.mv_data), value.mv_size));
                            };

                            std::unordered_set<std::string> visited {};
                            std::queue<std::string> frontier {};

                            if (auto self_entry = load_entry(src_path); self_entry)
                            {
                                for (auto const &dep : self_entry->deps)
                                {
                                    if (visited.insert(dep.logical_name).second)
                                    {
                                        frontier.push(dep.logical_name);
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
                                    for (auto const &next : dep_entry->deps)
                                    {
                                        if (visited.insert(next.logical_name).second)
                                        {
                                            frontier.push(next.logical_name);
                                        }
                                    }
                                }
                            }
                        }
                        mdb_txn_abort(txn);
                    }
                }
                mdb_env_close(env);
            }
        }

        auto rsp_path = build_dir / (src_path.filename().string() + ".rsp");
        auto out = fmt::output_file(rsp_path.string());
        out.print("{}", content);
    }
} // namespace cppbuild
