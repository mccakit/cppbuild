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
    auto generate_p1689_per_source(const std::filesystem::path &build_dir,
                                   const types::toolchain &tc,
                                   const std::vector<types::build_target> &targets) -> void
    {
        const auto cxxflags_str = fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        const auto dir = build_dir.string();

        auto write_entry = [&](const std::filesystem::path &src, std::string_view kind, const std::string &flags) {
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
                    write_entry(src, sg.kind, flags);
                }
            }
            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    write_entry(go.path, go.kind, flags);
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

    auto run_scanner_per_source(const std::filesystem::path &build_dir,
                                const types::toolchain &tc,
                                const std::vector<types::build_target> &targets) -> std::vector<types::dyndep_entry>
    {
        std::unordered_map<std::string, types::cxx_module> name_to_module;
        std::unordered_map<std::string, std::vector<std::string>> file_requires;

        auto scan_one = [&](const std::filesystem::path &src, std::string_view kind) {
            if (src.extension() == ".c")
            {
                return;
            }
            const auto db_path = (build_dir / (src.filename().string() + ".p1689.json")).string();
            auto proc = subprocess::RunBuilder({tc.cxx_scanner, "--format=p1689", "-compilation-database", db_path})
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

            for (auto &rule : rules_it->second.get<glz::generic::array_t>())
            {
                auto &rule_obj = rule.get<glz::generic::object_t>();

                types::cxx_module mod {};
                mod.source_path = src;

                if (auto it = rule_obj.find("provides"); it != rule_obj.end())
                {
                    for (auto &provides : it->second.get<glz::generic::array_t>())
                    {
                        auto &prov_obj = provides.get<glz::generic::object_t>();
                        mod.logical_name = prov_obj["logical-name"].get<std::string>();
                        name_to_module[mod.logical_name] = mod;
                    }
                }

                if (auto it = rule_obj.find("requires"); it != rule_obj.end())
                {
                    for (auto &req : it->second.get<glz::generic::array_t>())
                    {
                        auto &req_obj = req.get<glz::generic::object_t>();
                        file_requires[src.string()].push_back(req_obj["logical-name"].get<std::string>());
                    }
                }
            }
        };

        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    scan_one(src, sg.kind);
                }
            }
            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    scan_one(go.path, go.kind);
                }
            }
        }

        std::unordered_map<std::string, types::cxx_module> path_to_module;
        for (auto &[name, mod] : name_to_module)
        {
            path_to_module[mod.source_path.string()] = mod;
        }

        std::vector<types::dyndep_entry> result;
        for (auto &[file, req_names] : file_requires)
        {
            types::dyndep_entry entry {};
            if (auto it = path_to_module.find(file); it != path_to_module.end())
            {
                entry.src = it->second;
            }
            else
            {
                entry.src.source_path = file;
            }
            for (auto &name : req_names)
            {
                if (auto it = name_to_module.find(name); it != name_to_module.end())
                {
                    entry.deps.push_back(it->second);
                }
            }
            result.push_back(std::move(entry));
        }

        for (auto &[name, mod] : name_to_module)
        {
            if (file_requires.find(mod.source_path.string()) == file_requires.end())
            {
                types::dyndep_entry entry {};
                entry.src = mod;
                result.push_back(std::move(entry));
            }
        }

        return result;
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

    auto generate_dyndep(const std::filesystem::path &build_dir, const std::vector<types::dyndep_entry> &entries)
        -> void
    {
        std::ofstream out(build_dir / "deps.dd");
        out << "ninja_dyndep_version = 1\n";
        for (auto const &entry : entries)
        {
            std::string dep_pcms;
            for (auto const &dep : entry.deps)
            {
                auto dep_pcm = build_dir / (dep.source_path.filename().string() + ".pcm");
                dep_pcms += " " + dep_pcm.string();
            }
            if (!entry.src.logical_name.empty())
            {
                auto pcm = build_dir / (entry.src.source_path.filename().string() + ".pcm");
                out << "build " << pcm.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
            }
            else
            {
                auto obj = build_dir / (entry.src.source_path.filename().string() + ".o");
                out << "build " << obj.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
            }
        }
    }

    auto generate_rsp(const std::filesystem::path &build_dir,
                      const types::toolchain &tc,
                      const std::vector<types::build_target> &targets,
                      const std::vector<types::dyndep_entry> &entries) -> void
    {
        std::unordered_map<std::string, types::dyndep_entry const *> src_to_entry;
        for (auto const &entry : entries)
        {
            src_to_entry[entry.src.source_path.string()] = &entry;
        }

        for (auto const &bt : targets)
        {
            auto write_rsp = [&](const std::filesystem::path &src) {
                auto out = fmt::output_file((build_dir / (src.filename().string() + ".rsp")).string());
                if (src.extension() == ".c")
                {
                    out.print("{}\n{}\n{}\n",
                              fmt::join(tc.cflags, "\n"),
                              fmt::join(bt.cflags.public_, "\n"),
                              fmt::join(bt.cflags.private_, "\n"));
                }
                else
                {
                    out.print("{}\n{}\n{}\n",
                              fmt::join(tc.cxxflags, "\n"),
                              fmt::join(bt.cxxflags.public_, "\n"),
                              fmt::join(bt.cxxflags.private_, "\n"));
                }
                auto it = src_to_entry.find(src.string());
                if (it == src_to_entry.end())
                {
                    return;
                }
                for (auto const &dep : it->second->deps)
                {
                    auto pcm = build_dir / (dep.source_path.filename().string() + ".pcm");
                    out.print("-fmodule-file={}={}\n", dep.logical_name, pcm.string());
                }
            };

            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    write_rsp(src);
                }
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    write_rsp(go.path);
                }
            }
        }
    }
} // namespace cppbuild
