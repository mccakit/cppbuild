module;
#include <cstdlib>
#include <vector>
#include <optional>
#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include "graaflib/graph.h"
#include "graaflib/algorithm/topological_sorting/dfs_topological_sorting.h"
#include <simdjson.h>
#include "subprocess.h"

export module cppbuild.app;
import std;
import cppbuild.types;
import cppbuild.core;
import cppbuild.helpers;
import cppbuild.ninja;
import cppbuild.compdb;
import cppbuild.cache;
import cppbuild.toolchain;
import cppbuild.umka;
export namespace cppbuild::app
{
    using namespace cppbuild;
    void generate_dyndep(const std::filesystem::path &path)
    {
        std::string input;
        for (std::string line; std::getline(std::cin, line);)
        {
            input += line + "\n";
        }
        simdjson::dom::parser parser;
        auto doc = parser.parse(input);
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
                name_to_pcm[name] = name + ".pcm";
            }
        }
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path);
        out << "ninja_dyndep_version = 1\n";
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }
            std::string pcm = std::string(provides.at(0)["logical-name"].get_string().value()) + ".pcm";
            std::string deps;
            simdjson::dom::array requires_;
            if (rule["requires"].get(requires_) == simdjson::SUCCESS)
            {
                for (auto r : requires_)
                {
                    auto it = name_to_pcm.find(std::string(r["logical-name"].get_string().value()));
                    if (it != name_to_pcm.end())
                    {
                        deps += " " + it->second;
                    }
                }
            }
            out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
        }
    }
    auto scan_p1689(const std::filesystem::path &build_dir, const types::toolchain &toolchain) -> std::string
    {
        const auto module_commands = (build_dir / "module_commands.json").string();
        const auto scanner = std::string{toolchain.cxx_scanner};
        const char *command_line[] = {
            scanner.c_str(), "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
        struct subprocess_s subprocess;
        int options =
            subprocess_option_search_user_path | subprocess_option_inherit_environment | subprocess_option_enable_async;
        if (subprocess_create(command_line, options, &subprocess) != 0)
        {
            return "";
        }
        std::string scan_output;
        char buf[4096];
        unsigned bytes_read;
        while ((bytes_read = subprocess_read_stdout(&subprocess, buf, sizeof(buf))) != 0)
        {
            scan_output.append(buf, bytes_read);
        }
        int process_return;
        subprocess_join(&subprocess, &process_return);
        subprocess_destroy(&subprocess);
        return scan_output;
    }
    struct configure_options
    {
        public:
            const std::filesystem::path &src_dir;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &toolchain_path;
            const std::filesystem::path &self_path;
            const std::filesystem::path &prefix;
    };
    auto configure(const configure_options &opts) -> int
    {
        types::toolchain tc{};
        umka::umka umka{};
        auto targets = umka.run((opts.src_dir / "build.um").string(), "configure");
        for (auto &t : targets.install_targets)
        {
            t.src = std::filesystem::weakly_canonical(opts.src_dir / t.src);
        }
        types::graph_result res{core::build_graph(targets, opts.src_dir)};
        graaf::directed_graph<types::target, int> graph{std::move(res.g)};
        toolchain::parse_toolchain(opts.toolchain_path, tc);
        compdb::write_named_module_compile_commands({.graph = graph,
                                                     .cxx_compiler = tc.cxx_compiler,
                                                     .cxxflags = helpers::to_views(tc.cxxflags),
                                                     .output_dir = opts.build_dir});
        compdb::fill_module_names(graph, scan_p1689(opts.build_dir, tc));
        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            return 1;
        }
        ninja::write_ninja_build({.graph = graph,
                                  .order = *order,
                                  .toolchain = tc,
                                  .build_dir = opts.build_dir,
                                  .self_path = opts.self_path});
        compdb::write_compile_commands({.graph = graph,
                                        .cxx_compiler = tc.cxx_compiler,
                                        .c_compiler = tc.c_compiler,
                                        .cxxflags = helpers::to_views(tc.cxxflags),
                                        .cflags = helpers::to_views(tc.cflags),
                                        .output_dir = opts.build_dir});
        ninja::write_ninja_install(
            {.install_targets = targets.install_targets, .build_dir = opts.build_dir, .prefix = opts.prefix});
        cache::save_cache(tc, graph, opts.build_dir);
        return 0;
    }
    struct reconfigure_options
    {
        public:
            const std::filesystem::path &build_dir;
            const std::filesystem::path &self_path;
    };
    auto reconfigure(const reconfigure_options &opts) -> int
    {
        types::cache_result cache = cache::load_cache(opts.build_dir / "cache.json");
        graaf::directed_graph<types::target, int> graph{std::move(cache.graph.g)};
        compdb::fill_module_names(graph, scan_p1689(opts.build_dir, cache.toolchain));
        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            fmt::println("Reconfigure failed");
            return 1;
        }
        ninja::write_ninja_build({.graph = graph,
                                  .order = *order,
                                  .toolchain = cache.toolchain,
                                  .build_dir = opts.build_dir,
                                  .self_path = opts.self_path});
        std::filesystem::remove(opts.build_dir / "modules.dd");
        return 0;
    }
} // namespace cppbuild::app
