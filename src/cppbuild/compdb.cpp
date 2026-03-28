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
        auto out = fmt::output_file((build_dir / "compile_commands.json").string());
        const std::string cflags_str = tc.cflags.empty() ? "" : fmt::format("{} ", fmt::join(tc.cflags, " "));
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
                for (const auto &src : sg.srcs)
                {
                    const bool is_c = src.extension() == ".c";
                    const auto &compiler = is_c ? tc.c_compiler : tc.cxx_compiler;
                    const auto &flags = is_c ? cflags_str : cxxflags_str;
                    if (compiler.empty())
                    {
                        continue;
                    }
                    auto obj = build_dir / src.filename();
                    obj.replace_extension(".o");
                    if (!first)
                    {
                        out.print(",\n");
                    }
                    first = false;
                    const auto src_str = src.string();
                    out.print("  {{\"directory\":\"{}\",\"file\":\"{}\",\"command\":\"{} {}-c {} -o {}\"}}",
                              build_dir.string(),
                              src_str,
                              compiler.string(),
                              flags,
                              src_str,
                              obj.string());
                }
            }
        }
        out.print("\n]\n");
    }

    auto generate_dyndep(const std::filesystem::path &build_dir, const std::string_view scan_output) -> void
    {
        simdjson::dom::parser parser;
        auto doc = parser.parse(scan_output);
        // Map: logical-name -> source-filename.pcm
        std::unordered_map<std::string, std::string> name_to_pcm;
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }
            for (auto p : provides)
            {
                std::string name = std::string(p["logical-name"].get_string().value());
                std::filesystem::path src = std::string(p["source-path"].get_string().value());
                name_to_pcm[name] = (build_dir / (src.filename().string() + ".pcm")).string();
            }
        }

        std::ofstream out(build_dir / "deps.dd");
        out << "ninja_dyndep_version = 1\n";
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            std::filesystem::path src = std::string(provides.at(0)["source-path"].get_string().value());
            const auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
            std::string deps;
            simdjson::dom::array requires_;
            if (rule["requires"].get(requires_) == simdjson::SUCCESS)
            {
                for (auto r : requires_)
                {
                    std::string dep_name = std::string(r["logical-name"].get_string().value());
                    auto it = name_to_pcm.find(dep_name);
                    if (it != name_to_pcm.end())
                        deps += " " + it->second;
                }
            }
            out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
        }
    }

    auto scan_srcs(const std::filesystem::path &build_dir) -> const std::string
    {
        const auto module_commands = (build_dir / "compile_commands.json").string();
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
        return output;
    }

    auto generate_rsp(const std::filesystem::path &build_dir,
                      const types::toolchain &tc,
                      const std::vector<types::build_target> &targets,
                      const std::string_view scan_output) -> void
    {
        simdjson::dom::parser parser;
        auto doc = parser.parse(scan_output);
        // Map: logical-name -> pcm path
        std::unordered_map<std::string, std::string> name_to_pcm;
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            for (auto p : provides)
            {
                std::string name = std::string(p["logical-name"].get_string().value());
                std::filesystem::path src = std::string(p["source-path"].get_string().value());
                name_to_pcm[name] = (build_dir / (src.filename().string() + ".pcm")).string();
            }
        }

        // Map: logical-name -> direct requires, source-path -> logical-name
        std::unordered_map<std::string, std::vector<std::string>> name_to_requires;
        std::unordered_map<std::string, std::string> src_to_name;
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            std::string name = std::string(provides.at(0)["logical-name"].get_string().value());
            std::string src = std::string(provides.at(0)["source-path"].get_string().value());
            src_to_name[src] = name;
            simdjson::dom::array requires_;
            if (rule["requires"].get(requires_) != simdjson::SUCCESS)
                continue;
            for (auto r : requires_)
                name_to_requires[name].push_back(std::string(r["logical-name"].get_string().value()));
        }

        // Build graaf directed graph for transitive dep resolution
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_vid;
        graaf::directed_graph<std::string, int> dep_graph;
        for (const auto &[name, _] : name_to_pcm)
            name_to_vid[name] = dep_graph.add_vertex(name);
        for (const auto &[name, deps] : name_to_requires)
        {
            auto from_it = name_to_vid.find(name);
            if (from_it == name_to_vid.end())
                continue;
            for (const auto &dep : deps)
            {
                auto to_it = name_to_vid.find(dep);
                if (to_it == name_to_vid.end())
                    continue;
                dep_graph.add_edge(from_it->second, to_it->second, 1);
            }
        }

        // Get all transitive -fmodule-file= flags for a source file
        auto get_transitive_flags = [&](const std::string &src) -> std::vector<std::string> {
            std::vector<std::string> flags;
            auto name_it = src_to_name.find(src);
            if (name_it == src_to_name.end())
                return flags;
            auto vid_it = name_to_vid.find(name_it->second);
            if (vid_it == name_to_vid.end())
                return flags;
            graaf::algorithm::breadth_first_traverse(dep_graph, vid_it->second, [&](const auto &edge) {
                const auto &dep_name = dep_graph.get_vertex(edge.second);
                auto pcm_it = name_to_pcm.find(dep_name);
                if (pcm_it != name_to_pcm.end())
                    flags.push_back(fmt::format("-fmodule-file={}={}", dep_name, pcm_it->second));
            });
            return flags;
        };

        // Write .rsp files for all sources
        for (const auto &bt : targets)
        {
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    auto out = fmt::output_file((build_dir / (src.filename().string() + ".rsp")).string());
                    if (src.extension() == ".c")
                    {
                        out.print("{}\n", fmt::join(tc.cflags, "\n"));
                    }
                    else
                    {
                        out.print("{}\n", fmt::join(tc.cxxflags, "\n"));
                    }
                    for (const auto &flag : get_transitive_flags(src.string()))
                    {
                        out.print("{}\n", flag);
                    }
                }
            }
        }
    }
} // namespace cppbuild
