module;
#include <simdjson.h>
#include <subprocess.h>
export module cppbuild:compdb;
import std;
import graaf;
import fmt;
import :types;

export namespace cppbuild
{
    auto collect_header_unit_flags(const std::vector<types::build_target> &build_targets,
                                   const std::filesystem::path &build_dir)
        -> std::unordered_map<std::string, std::string>
    {
        std::unordered_map<std::string, const types::build_target *> name_to_target;
        name_to_target.reserve(build_targets.size());
        for (const auto &bt : build_targets)
        {
            name_to_target[bt.name] = &bt;
        }

        auto get_hu_flags_for_target = [&](const types::build_target &bt) -> std::string
        {
            std::string flags;
            for (const auto &sg : bt.srcs)
            {
                if (sg.kind == "header_unit")
                {
                    for (const auto &src : sg.srcs)
                    {
                        flags += fmt::format("-fmodule-file={} ", (build_dir / (src.filename().string() + ".pcm")).string());
                    }
                }
            }
            return flags;
        };

        std::unordered_map<std::string, std::string> result;
        result.reserve(build_targets.size());

        for (const auto &bt : build_targets)
        {
            std::string flags = get_hu_flags_for_target(bt);

            // walk deps transitively via BFS
            std::unordered_set<std::string> visited;
            std::queue<std::string> queue;
            for (const auto &dep : bt.deps)
            {
                queue.push(dep);
            }
            while (!queue.empty())
            {
                auto dep_name = std::move(queue.front());
                queue.pop();
                if (!visited.insert(dep_name).second)
                {
                    continue;
                }
                auto it = name_to_target.find(dep_name);
                if (it == name_to_target.end())
                {
                    continue;
                }
                flags += get_hu_flags_for_target(*it->second);
                for (const auto &transitive_dep : it->second->deps)
                {
                    queue.push(transitive_dep);
                }
            }

            result[bt.name] = std::move(flags);
        }

        return result;
    }

    auto generate_p1689(const std::filesystem::path &build_dir,
                        const types::toolchain &tc,
                        const std::vector<types::build_target> &targets)
    {
        const auto hu_flags = collect_header_unit_flags(targets, build_dir);

        auto out = fmt::output_file((build_dir / "p1689.json").string());
        const auto cxxflags_str = fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        const auto dir = build_dir.string();
        auto entries = std::vector<std::string> {};

        auto add_entry = [&](const std::filesystem::path &src,
                             std::string_view kind,
                             const std::string &flags,
                             const std::string &extra_flags)
        {
            if (kind == "header_unit" || src.extension() == ".c")
            {
                return;
            }
            const auto ext = kind == "named_module" ? ".pcm.o" : ".o";
            const auto src_str = src.string();
            const auto obj = (build_dir / (src.filename().string() + ext)).string();
            entries.push_back(fmt::format(
                "  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {} {} -c {} -o {}\",\"output\":\"{}\"}}",
                dir,
                src_str,
                tc.cxx_compiler,
                flags,
                extra_flags,
                src_str,
                obj,
                obj));
        };

        for (const auto &bt : targets)
        {
            const auto flags = fmt::format("{} {} {} ",
                                           cxxflags_str,
                                           fmt::join(bt.cxxflags.public_, " "),
                                           fmt::join(bt.cxxflags.private_, " "));
            const auto &extra_flags = hu_flags.at(bt.name);
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    add_entry(src, sg.kind, flags, extra_flags);
                }
            }
            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &go : gg.outputs)
                {
                    add_entry(go.path, go.kind, flags, extra_flags);
                }
            }
        }

        out.print("[\n{}\n]\n", fmt::join(entries, ",\n"));
    }

    auto generate_compile_commands(const std::filesystem::path &build_dir,
                                   const types::toolchain &tc,
                                   const std::vector<types::build_target> &targets)
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
                dir, src_str, compiler, src_str, obj, rsp, obj));
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

    class cxx_module
    {
        public:
            std::string logical_name {};
            std::filesystem::path source_path {};
            auto print() const -> void
            {
                fmt::println("{} -> {}", logical_name, source_path.string());
            }
    };

    class dyndep_entry
    {
        public:
            cxx_module src;
            std::vector<cxx_module> deps;
            auto print() const -> void
            {
                src.print();
                for (auto const &dep : deps)
                {
                    fmt::print("  ");
                    dep.print();
                }
            }
    };

    auto scan_srcs(const std::filesystem::path &build_dir, const types::toolchain &tc) -> const std::vector<dyndep_entry>
    {
        const auto module_commands = (build_dir / "p1689.json").string();
        const char *command_line[] = {
            tc.cxx_scanner.c_str(), "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
        struct subprocess_s subprocess;
        int options =
            subprocess_option_search_user_path | subprocess_option_inherit_environment | subprocess_option_enable_async;
        if (subprocess_create(command_line, options, &subprocess) != 0)
        {
            return {};
        }
        std::string output;
        char buf[4096];
        unsigned bytes_read;
        while ((bytes_read = subprocess_read_stdout(&subprocess, buf, sizeof(buf))) != 0)
        {
            output.append(buf, bytes_read);
        }
        int process_return;
        subprocess_join(&subprocess, &process_return);
        subprocess_destroy(&subprocess);

        simdjson::dom::parser scan_parser;
        simdjson::dom::parser db_parser;
        std::string db_path = build_dir / "p1689.json";
        auto scan_doc = scan_parser.parse(output);
        auto compdb = db_parser.load(db_path);

        std::unordered_map<std::string, std::string> output_to_file;
        std::unordered_map<std::string, std::string> file_to_output;
        for (auto entry : compdb)
        {
            std::string file = std::string(entry["file"].get_string().value());
            std::string out = std::string(entry["output"].get_string().value());
            output_to_file[out] = file;
            file_to_output[file] = out;
        }

        graaf::directed_graph<cxx_module, int> graph;
        std::unordered_map<std::string, graaf::vertex_id_t> id_map;

        // pass 1: insert vertices
        for (auto rule : scan_doc["rules"])
        {
            cxx_module src {};
            std::string primary_output = std::string(rule["primary-output"].get_string().value());

            simdjson::dom::array provides_arr;
            if (!rule["provides"].get(provides_arr))
            {
                for (auto provides : provides_arr)
                {
                    src.logical_name = std::string(provides["logical-name"].get_string().value());
                    std::string_view sp;
                    if (provides["source-path"].get(sp) == simdjson::SUCCESS)
                    {
                        src.source_path = std::string(sp);
                    }
                }
            }

            if (src.source_path.empty())
            {
                auto it = output_to_file.find(primary_output);
                if (it != output_to_file.end())
                {
                    src.source_path = it->second;
                }
            }

            id_map[primary_output] = graph.add_vertex(src);
        }

        // pass 2: add edges
        for (auto rule : scan_doc["rules"])
        {
            std::string primary_output = std::string(rule["primary-output"].get_string().value());
            simdjson::dom::array requires_arr;
            if (!rule["requires"].get(requires_arr))
            {
                for (auto req : requires_arr)
                {
                    std::string dep_file = std::string(req["source-path"].get_string().value());
                    auto dep_output_it = file_to_output.find(dep_file);
                    if (dep_output_it == file_to_output.end())
                    {
                        continue;
                    }
                    auto src_it = id_map.find(primary_output);
                    auto dst_it = id_map.find(dep_output_it->second);
                    if (src_it != id_map.end() && dst_it != id_map.end())
                    {
                        graph.add_edge(src_it->second, dst_it->second, 1);
                    }
                }
            }
        }

        std::vector<dyndep_entry> result {};
        for (auto const &[id, value] : graph.get_vertices())
        {
            dyndep_entry entry {};
            entry.src = value;
            graaf::algorithm::breadth_first_traverse(graph, id, [&](graaf::edge_id_t e) {
                auto [src, dst] = e;
                entry.deps.push_back(graph.get_vertex(dst));
            });
            result.push_back(std::move(entry));
        }

        return result;
    }

    auto generate_dyndep(const std::filesystem::path &build_dir, const std::vector<dyndep_entry> &entries) -> void
    {
        std::ofstream out(build_dir / "deps.dd");
        out << "ninja_dyndep_version = 1\n";
        for (auto const &entry : entries)
        {
            auto obj = build_dir / (entry.src.source_path.filename().string() + ".o");
            std::string dep_pcms;
            for (auto const &dep : entry.deps)
            {
                auto dep_pcm = build_dir / (dep.source_path.filename().string() + ".pcm");
                dep_pcms += " " + dep_pcm.string();
            }
            if (!entry.src.logical_name.empty())
            {
                auto pcm = build_dir / (entry.src.source_path.filename().string() + ".pcm");
                out << "build " << pcm.string() << " | " << obj.string() << ": dyndep"
                    << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
            }
            else
            {
                out << "build " << obj.string() << ": dyndep" << (dep_pcms.empty() ? "" : " |" + dep_pcms) << "\n";
            }
        }
    }

    auto generate_rsp(const std::filesystem::path &build_dir,
                      const types::toolchain &tc,
                      const std::vector<types::build_target> &targets,
                      const std::vector<dyndep_entry> &entries) -> void
    {
        std::unordered_map<std::string, dyndep_entry const *> src_to_entry;
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
                    if (go.kind != "header_unit")
                    {
                        write_rsp(go.path);
                    }
                }
            }
        }
    }
} // namespace cppbuild
