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
    auto generate_p1689(const std::filesystem::path &build_dir,
                        const types::toolchain &tc,
                        const std::vector<types::build_target> &targets)
    {
        auto out = fmt::output_file((build_dir / "p1689.json").string());
        const std::string cxxflags_str = tc.cxxflags.empty() ? "" : fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        out.print("[\n");
        bool first = true;
        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
            {
                if (sg.kind == "header_unit")
                {
                    continue;
                }
                auto ext = sg.kind == "named_module" ? ".pcm.o" : ".o";
                for (const auto &src : sg.srcs)
                {
                    if (src.extension() != ".c")
                    {
                        const auto &compiler = tc.cxx_compiler;
                        const auto &flags = cxxflags_str;
                        if (compiler.empty())
                        {
                            continue;
                        }
                        auto obj = build_dir / (src.filename().string() + ext);
                        if (!first)
                        {
                            out.print(",\n");
                        }
                        first = false;
                        const auto src_str = src.string();
                        out.print("  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {}-c {} -o "
                                  "{}\",\"output\":\"{}\"}}",
                                  build_dir.string(),
                                  src_str,
                                  compiler,
                                  flags,
                                  src_str,
                                  obj.string(),
                                  obj.string());
                    }
                }
            }
        }
        out.print("\n]\n");
    }

    auto generate_compile_commands(const std::filesystem::path &build_dir,
                                   const types::toolchain &tc,
                                   const std::vector<types::build_target> &targets)
    {
        auto out = fmt::output_file((build_dir / "compile_commands.json").string());
        const std::string cflags_str = tc.cflags.empty() ? "" : fmt::format("{} ", fmt::join(tc.cflags, " "));
        const std::string cxxflags_str = tc.cxxflags.empty() ? "" : fmt::format("{} ", fmt::join(tc.cxxflags, " "));
        out.print("[\n");
        bool first = true;
        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
            {
                auto ext = sg.kind == "named_module" ? ".pcm.o" : ".o";
                for (const auto &src : sg.srcs)
                {
                    const bool is_c = src.extension() == ".c";
                    const bool is_header_unit = sg.kind == "header_unit";
                    const auto &compiler = is_c ? tc.c_compiler : tc.cxx_compiler;
                    const auto &flags = is_c ? cflags_str : cxxflags_str;
                    if (compiler.empty())
                        continue;
                    auto obj = build_dir / (src.filename().string() + ext);
                    auto rsp = build_dir / (src.filename().string() + ".rsp");
                    if (!first)
                        out.print(",\n");
                    first = false;
                    const auto src_str = src.string();
                    if (is_c)
                    {
                        out.print("  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {}-c {} -o {}\"}}",
                                  build_dir.string(),
                                  src_str,
                                  compiler,
                                  flags,
                                  src_str,
                                  obj.string());
                    }
                    else if (is_header_unit)
                    {
                        out.print("  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {}--precompile -fmodule-header=user -xc++-user-header {} -o {}\"}}",
                                  build_dir.string(),
                                  src_str,
                                  compiler,
                                  cxxflags_str,
                                  src_str,
                                  (build_dir / (src.filename().string() + ".pcm")).string());
                    }
                    else
                    {
                        out.print("  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {}-c {} -o {} @{}\"}}",
                                  build_dir.string(),
                                  src_str,
                                  compiler,
                                  flags,
                                  src_str,
                                  obj.string(),
                                  rsp.string());
                    }
                }
            }
        }
        out.print("\n]\n");
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
    auto scan_srcs(const std::filesystem::path &build_dir) -> const std::vector<dyndep_entry>
    {
        const auto module_commands = (build_dir / "p1689.json").string();
        const char *command_line[] = {
            "clang-scan-deps", "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
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

        // index compdb by output -> file
        std::unordered_map<std::string, std::string> output_to_file;
        std::unordered_map<std::string, std::string> file_to_output;
        for (auto entry : compdb)
        {
            std::string file = std::string(entry["file"].get_string().value());
            std::string output = std::string(entry["output"].get_string().value());
            output_to_file[output] = file;
            file_to_output[file] = output;
        }
        graaf::directed_graph<cxx_module, int> graph;
        std::unordered_map<std::string, graaf::vertex_id_t> id_map;

        // pass 1: insert vertices
        for (auto rule : scan_doc["rules"])
        {
            cxx_module src {};
            std::string primary_output = std::string(rule["primary-output"].get_string().value());
            auto it = output_to_file.find(primary_output);
            if (it != output_to_file.end())
            {
                src.source_path = it->second;
            }
            simdjson::dom::array provides_arr;
            if (!rule["provides"].get(provides_arr))
            {
                for (auto provides : provides_arr)
                {
                    src.logical_name = std::string(provides["logical-name"].get_string().value());
                }
            }

            id_map[primary_output] = graph.add_vertex(src);
        }
        //
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
        //
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
        // map source path -> dyndep_entry for quick lookup
        std::unordered_map<std::string, dyndep_entry const *> src_to_entry;
        for (auto const &entry : entries)
            src_to_entry[entry.src.source_path.string()] = &entry;

        for (auto const &bt : targets)
        {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    auto out = fmt::output_file((build_dir / (src.filename().string() + ".rsp")).string());
                    if (src.extension() == ".c")
                        out.print("{}\n", fmt::join(tc.cflags, "\n"));
                    else
                        out.print("{}\n", fmt::join(tc.cxxflags, "\n"));

                    auto it = src_to_entry.find(src.string());
                    if (it == src_to_entry.end())
                        continue;
                    for (auto const &dep : it->second->deps)
                    {
                        auto pcm = build_dir / (dep.source_path.filename().string() + ".pcm");
                        out.print("-fmodule-file={}={}\n", dep.logical_name, pcm.string());
                    }
                }
            }
        }
    }
} // namespace cppbuild
