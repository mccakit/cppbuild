module;
#include <simdjson.h>

export module cppbuild:compdb;
import std;
import graaf;
import fmt;

import :types;
export namespace cppbuild
{
    using namespace cppbuild;
    auto join_flags(const std::vector<std::string_view> &flags) -> std::string
    {
        return fmt::format("{}", fmt::join(flags, " "));
    }

    struct write_module_commands_options
    {
        public:
            graaf::directed_graph<target, int> &graph;
            std::string_view cxx_compiler;
            std::vector<std::string_view> cxxflags;
            std::filesystem::path output_dir;
    };

    struct write_compile_commands_options
    {
        public:
            graaf::directed_graph<cppbuild::target, int> &graph;
            std::string_view cxx_compiler;
            std::string_view c_compiler;
            std::vector<std::string_view> cxxflags;
            std::vector<std::string_view> cflags;
            std::filesystem::path output_dir;
    };

    auto write_named_module_compile_commands(const write_module_commands_options &opts) -> void
    {
        if (!opts.output_dir.empty())
        {
            std::filesystem::create_directories(opts.output_dir);
        }
        const auto output_path = std::filesystem::absolute(opts.output_dir) / "module_commands.json";
        fmt::ostream file = fmt::output_file(output_path.string());
        file.print("[\n");
        bool first{true};
        for (const auto &[id, t] : opts.graph.get_vertices())
        {
            if (t.type != "named_module")
            {
                continue;
            }
            for (const auto &src : t.srcs)
            {
                if (!first)
                {
                    file.print(",\n");
                }
                const std::string target_flags = fmt::format("{}", fmt::join(t.cxxflags, " "));
                file.print("  {{\"directory\":\".\",\"command\":\"{} {} {} -x c++ -c "
                           "{}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                           opts.cxx_compiler,
                           join_flags(opts.cxxflags),
                           target_flags,
                           src.string(),
                           src.string(),
                           src.string());
                first = false;
            }
        }
        file.print("\n]\n");
    }

    auto write_compile_commands(const write_compile_commands_options &opts) -> void
    {
        fmt::ostream file = fmt::output_file((opts.output_dir / "compile_commands.json").string());
        file.print("[\n");
        bool first{true};
        for (const auto &[id, t] : opts.graph.get_vertices())
        {
            if (t.type == "header_unit")
            {
                continue;
            }
            for (const auto &src : t.srcs)
            {
                if (!first)
                {
                    file.print(",\n");
                }
                const bool is_c = src.extension() == ".c";
                const auto &compiler = is_c ? opts.c_compiler : opts.cxx_compiler;
                const auto &flags = is_c ? join_flags(opts.cflags) : join_flags(opts.cxxflags);
                const std::string target_flags = fmt::format("{}", fmt::join(is_c ? t.cflags : t.cxxflags, " "));
                file.print("  {{\"directory\":\"{}\",\"command\":\"{} {} {} -c "
                           "{}\",\"file\":\"{}\",\"output\":\"{}/{}.o\"}}",
                           std::filesystem::absolute(opts.output_dir).string(),
                           compiler,
                           flags,
                           target_flags,
                           src.string(),
                           src.string(),
                           std::filesystem::absolute(opts.output_dir).string(),
                           src.stem().string());
                first = false;
            }
        }
        file.print("\n]\n");
    }

    auto fill_module_names(graaf::directed_graph<cppbuild::target, int> &graph, std::string_view scan_output)
        -> void
    {
        simdjson::dom::parser parser{};
        auto doc = parser.parse(scan_output);

        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides{};
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }
            for (auto p : provides)
            {
                std::string_view src = p["source-path"].get_string().value();
                std::string_view name = p["logical-name"].get_string().value();
                for (auto &[id, t] : graph.get_vertices())
                {
                    for (const auto &tsrc : t.srcs)
                    {
                        if (tsrc.string() == src)
                        {
                            graph.get_vertex(id).src_to_mname[std::string(src)] = std::string(name);
                        }
                    }
                }
            }
        }
    }
} // namespace cppbuild::compdb
