module;
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
        auto cache_path = db_path.parent_path() / (db_path.stem().stem().string() + ".dyndep.bin");
        entry.save(cache_path);
    }

    auto load_dyndep_entries(const std::filesystem::path &build_dir,
                             const std::vector<types::build_target> &targets,
                             BS::thread_pool<> &pool) -> std::vector<types::dyndep_entry>
    {
        std::vector<std::filesystem::path> paths;
        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
                for (const auto &src : sg.srcs)
                {
                    if (src.extension() == ".c")
                        continue;
                    paths.push_back(build_dir / (src.filename().string() + ".dyndep.bin"));
                }
            for (const auto &gg : bt.gen_groups)
                for (const auto &go : gg.outputs)
                {
                    if (go.path.extension() == ".c")
                        continue;
                    paths.push_back(build_dir / (go.path.filename().string() + ".dyndep.bin"));
                }
        }

        std::vector<types::dyndep_entry> entries(paths.size());
        pool.submit_loop(
                std::size_t {0}, paths.size(), [&](std::size_t i) { entries[i] = types::dyndep_entry::load(paths[i]); })
            .wait();
        return entries;
    }
    auto parse_direct_deps(const std::vector<types::dyndep_entry> &scanned)
        -> graaf::directed_graph<types::cxx_module, int>
    {
        graaf::directed_graph<types::cxx_module, int> graph;
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
        std::unordered_map<std::string, graaf::vertex_id_t> path_to_id;

        // add all modules as vertices
        for (const auto &entry : scanned)
        {
            auto id = graph.add_vertex(entry.src);
            path_to_id[entry.src.source_path.string()] = id;
            if (!entry.src.logical_name.empty())
            {
                name_to_id[entry.src.logical_name] = id;
            }
        }

        // add edges for direct deps
        for (const auto &entry : scanned)
        {
            auto src_it = path_to_id.find(entry.src.source_path.string());
            if (src_it == path_to_id.end())
            {
                continue;
            }
            for (const auto &dep : entry.deps)
            {
                auto dst_it = name_to_id.find(dep.logical_name);
                if (dst_it != name_to_id.end())
                {
                    graph.add_edge(src_it->second, dst_it->second, 1);
                }
            }
        }

        return graph;
    }

    auto resolve_transitive_deps(const graaf::directed_graph<types::cxx_module, int> &graph)
        -> std::vector<types::dyndep_entry>
    {
        std::vector<types::dyndep_entry> result {};
        for (auto const &[id, value] : graph.get_vertices())
        {
            types::dyndep_entry entry {};
            entry.src = value;
            graaf::algorithm::breadth_first_traverse(graph, id, [&](graaf::edge_id_t e) {
                auto [src, dst] = e;
                entry.deps.push_back(graph.get_vertex(dst));
            });
            result.push_back(std::move(entry));
        }
        return result;
    }

    auto generate_dyndep(const std::filesystem::path &build_dir,
                         const std::vector<types::dyndep_entry> &entries,
                         BS::thread_pool<> &pool) -> void
    {
        if (entries.empty())
            return;

        std::string build_dir_str = build_dir.string();

        // Result container: one string per entry
        std::vector<std::string> results(entries.size());

        // 1. Parallel Generation
        pool.detach_blocks(std::size_t {0}, entries.size(), [&](std::size_t begin, std::size_t end) {
            // Use a reusable memory buffer per thread block to minimize allocations
            fmt::memory_buffer line_buf;

            for (std::size_t i = begin; i < end; ++i)
            {
                line_buf.clear();
                const auto &entry = entries[i];

                // Determine output file (.pcm or .o)
                std::string_view ext = entry.src.logical_name.empty() ? ".o" : ".pcm";

                // Build the main line: "build build_dir/filename.ext: dyndep"
                fmt::format_to(std::back_inserter(line_buf),
                               "build {}/{}{}: dyndep",
                               build_dir_str,
                               entry.src.source_path.filename().string(),
                               ext);

                // Add implicit dependencies if they exist
                if (!entry.deps.empty())
                {
                    line_buf.push_back(' ');
                    line_buf.push_back('|');
                    for (const auto &dep : entry.deps)
                    {
                        fmt::format_to(std::back_inserter(line_buf),
                                       " {}/{}.pcm",
                                       build_dir_str,
                                       dep.source_path.filename().string());
                    }
                }
                line_buf.push_back('\n');

                // Store the rendered block
                results[i] = fmt::to_string(line_buf);
            }
        });

        pool.wait();

        // 2. High-speed Sequential Write
        // Using fmt::output_file is significantly faster than std::ofstream
        auto out = fmt::output_file((build_dir / "deps.dd").string());
        out.print("ninja_dyndep_version = 1\n");

        for (const auto &line : results)
        {
            out.print("{}", line);
        }
    }

    auto generate_rsp(const std::filesystem::path &build_dir,
                      const types::toolchain &tc,
                      const std::vector<types::build_target> &targets,
                      const std::vector<types::dyndep_entry> &entries,
                      BS::thread_pool<> &pool) -> void
    {
        std::string tc_cflags_str = fmt::to_string(fmt::join(tc.cflags, "\n"));
        std::string tc_cxxflags_str = fmt::to_string(fmt::join(tc.cxxflags, "\n"));

        struct target_flags
        {
                std::string c_flags;
                std::string cxx_flags;
        };
        std::unordered_map<const types::build_target *, target_flags> precomputed_flags;
        precomputed_flags.reserve(targets.size());

        for (auto const &bt : targets)
        {
            precomputed_flags[&bt] = {fmt::format("{}\n{}\n{}\n",
                                                  tc_cflags_str,
                                                  fmt::join(bt.cflags.public_, "\n"),
                                                  fmt::join(bt.cflags.private_, "\n")),
                                      fmt::format("{}\n{}\n{}\n",
                                                  tc_cxxflags_str,
                                                  fmt::join(bt.cxxflags.public_, "\n"),
                                                  fmt::join(bt.cxxflags.private_, "\n"))};
        }

        std::unordered_map<std::string, types::dyndep_entry const *> src_to_entry;
        src_to_entry.reserve(entries.size());
        for (auto const &entry : entries)
        {
            src_to_entry[entry.src.source_path.string()] = &entry;
        }

        struct rsp_task
        {
                const std::filesystem::path *src;
                const std::string *flags_to_write;
                const types::dyndep_entry *dyndep;
        };

        std::vector<rsp_task> tasks;

        for (auto const &bt : targets)
        {
            auto const &flags = precomputed_flags.at(&bt);

            auto process_source = [&](const std::filesystem::path &src_path) {
                auto it = src_to_entry.find(src_path.string());
                bool is_c = src_path.extension() == ".c";
                tasks.push_back({&src_path,
                                 is_c ? &flags.c_flags : &flags.cxx_flags,
                                 it != src_to_entry.end() ? it->second : nullptr});
            };

            for (auto const &sg : bt.srcs)
                for (auto const &src : sg.srcs)
                    process_source(src);
            for (auto const &gg : bt.gen_groups)
                for (auto const &go : gg.outputs)
                    process_source(go.path);
        }

        std::string build_dir_str = build_dir.string();

        pool.detach_blocks(std::size_t {0}, tasks.size(), [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i)
            {
                auto const &task = tasks[i];

                std::string content = *task.flags_to_write;
                if (task.dyndep)
                {
                    for (auto const &dep : task.dyndep->deps)
                    {
                        fmt::format_to(std::back_inserter(content),
                                       "-fmodule-file={}={}/{}.pcm\n",
                                       dep.logical_name,
                                       build_dir_str,
                                       dep.source_path.filename().string());
                    }
                }

                std::string rsp_path = fmt::format("{}/{}.rsp", build_dir_str, task.src->filename().string());

                // Skip write if content unchanged
                std::ifstream existing(rsp_path, std::ios::binary | std::ios::ate);
                if (existing)
                {
                    auto size = existing.tellg();
                    if (static_cast<std::size_t>(size) == content.size())
                    {
                        std::string old(static_cast<std::size_t>(size), '\0');
                        existing.seekg(0);
                        existing.read(old.data(), size);
                        if (old == content)
                        {
                            continue;
                        }
                    }
                }

                auto out = fmt::output_file(rsp_path);
                out.print("{}", content);
            }
        });

        pool.wait();
    }
} // namespace cppbuild
