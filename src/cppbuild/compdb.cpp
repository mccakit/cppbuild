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
    /**
     * Helper to join a vector of flags into a single space-separated string.
     */
    auto join_flags(const std::vector<std::string> &flags) -> std::string
    {
        return fmt::format("{}", fmt::join(flags, " "));
    }

    struct write_module_commands_options
    {
        public:
            types::build_graph &build_graph; // Updated to use types::build_graph
            types::toolchain tc {};          // Scoped to types
            std::filesystem::path output_dir;
    };

    /**
     * Generates a compilation database specifically for C++ named modules.
     */
    auto write_named_module_compile_commands(const write_module_commands_options &opts) -> void
    {
        if (!opts.output_dir.empty())
        {
            std::filesystem::create_directories(opts.output_dir);
        }

        const auto output_path = std::filesystem::absolute(opts.output_dir) / "module_commands.json";
        fmt::ostream file = fmt::output_file(output_path.string());

        file.print("[\n");
        bool first {true};

        // Access the graph member within the build_graph class
        for (const auto &[id, target] : opts.build_graph.graph.get_vertices())
        {
            for (const auto &source_group : target.srcs)
            {
                if (source_group.kind != "named_module")
                {
                    continue;
                }

                for (const auto &src : source_group.srcs)
                {
                    if (!first)
                    {
                        file.print(",\n");
                    }

                    file.print("  {{\"directory\":\".\",\"command\":\"{} {} {} -x c++ -c "
                               "{}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                               opts.tc.cxx_compiler,
                               join_flags(opts.tc.cxxflags),
                               join_flags(target.cxxflags),
                               src.string(),
                               src.string(),
                               src.string());
                    first = false;
                }
            }
        }
        file.print("\n]\n");
    }

    struct write_compile_commands_options
    {
        public:
            types::build_graph &build_graph; // Consistent with module options
            types::toolchain tc {};
            std::filesystem::path output_dir;
    };

    /**
     * Generates a standard compile_commands.json for the entire project.
     */
    auto write_compile_commands(const write_compile_commands_options &opts) -> void
    {
        const auto output_path = opts.output_dir / "compile_commands.json";
        fmt::ostream file = fmt::output_file(output_path.string());

        file.print("[\n");
        bool first {true};
        const std::string abs_out_dir = std::filesystem::absolute(opts.output_dir).string();

        for (const auto &[id, target] : opts.build_graph.graph.get_vertices())
        {
            for (const auto &source_group : target.srcs)
            {
                // Header units typically don't have standard compile commands in this format
                if (source_group.kind == "header_unit")
                {
                    continue;
                }

                for (const auto &src : source_group.srcs)
                {
                    if (!first)
                    {
                        file.print(",\n");
                    }

                    const bool is_c = src.extension() == ".c";
                    const auto &compiler = is_c ? opts.tc.c_compiler : opts.tc.cxx_compiler;
                    const auto &tc_flags = is_c ? join_flags(opts.tc.cflags) : join_flags(opts.tc.cxxflags);
                    const auto &tgt_flags = join_flags(is_c ? target.cflags : target.cxxflags);

                    file.print("  {{\"directory\":\"{}\",\"command\":\"{} {} {} -c "
                               "{}\",\"file\":\"{}\",\"output\":\"{}/{}.o\"}}",
                               abs_out_dir,
                               compiler,
                               tc_flags,
                               tgt_flags,
                               src.string(),
                               src.string(),
                               abs_out_dir,
                               src.stem().string());
                    first = false;
                }
            }
        }
        file.print("\n]\n");
    }

    /**
     * Updates build targets with logical module names parsed from a scanner output.
     */
    auto fill_module_names(types::build_graph &graph, std::string_view scan_output) -> void
    {
        simdjson::dom::parser parser {};
        auto doc = parser.parse(scan_output);

        // Optimization: Create a temporary map of src_path -> vertex_id
        // This turns an O(N^3) search into an O(N) lookup.
        std::unordered_map<std::string, graaf::vertex_id_t> src_to_id;
        for (const auto &[id, target] : graph.graph.get_vertices())
        {
            for (const auto &source_group : target.srcs)
            {
                for (const auto &src : source_group.srcs)
                {
                    src_to_id[src.string()] = id;
                }
            }
        }

        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides {};
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }

            for (auto p : provides)
            {
                std::string src_path = std::string(p["source-path"].get_string().value());
                std::string module_name = std::string(p["logical-name"].get_string().value());

                // Use our index to find the vertex immediately
                if (auto it = src_to_id.find(src_path); it != src_to_id.end())
                {
                    // FIX: Use graph.get_vertex(id) to get a MUTABLE reference
                    auto &target = graph.graph.get_vertex(it->second);
                    target.src_to_mname[src_path] = std::move(module_name);
                }
            }
        }
    }

    void generate_dyndep(const std::filesystem::path &build_dir)
    {
        const auto module_commands = (build_dir / "module_commands.json").string();
        const char *command_line[] = {
            "clang-scan-deps", "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
        struct subprocess_s subprocess;
        int options = subprocess_option_search_user_path | subprocess_option_inherit_environment | subprocess_option_enable_async;
        if (subprocess_create(command_line, options, &subprocess) != 0)
        {
            return;
        }
        std::string input;
        char buf[4096];
        unsigned bytes_read;
        while ((bytes_read = subprocess_read_stdout(&subprocess, buf, sizeof(buf))) != 0)
        {
            input.append(buf, bytes_read);
        }
        int process_return;
        subprocess_join(&subprocess, &process_return);
        subprocess_destroy(&subprocess);

        simdjson::dom::parser parser;
        auto doc = parser.parse(input);
        std::unordered_map<std::string, std::string> name_to_pcm;
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            for (auto p : provides)
            {
                std::string name = std::string(p["logical-name"].get_string().value());
                name_to_pcm[name] = name + ".pcm";
            }
        }
        std::ofstream out(build_dir / "modules.dd");
        out << "ninja_dyndep_version = 1\n";
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            std::string pcm = std::string(provides.at(0)["logical-name"].get_string().value()) + ".pcm";
            std::string deps;
            simdjson::dom::array requires_;
            if (rule["requires"].get(requires_) == simdjson::SUCCESS)
            {
                for (auto r : requires_)
                {
                    auto it = name_to_pcm.find(std::string(r["logical-name"].get_string().value()));
                    if (it != name_to_pcm.end())
                        deps += " " + it->second;
                }
            }
            out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
        }
    }
} // namespace cppbuild
