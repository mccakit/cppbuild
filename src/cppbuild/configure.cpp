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

export module cppbuild.configure;
import std;
import cppbuild.types;
import cppbuild.core;
import cppbuild.helpers;
import cppbuild.ninja;
import cppbuild.scanner;
import cppbuild.compdb;
import cppbuild.cache;
import cppbuild.toolchain;
import cppbuild.umka;
export namespace cppbuild::configure
{
    using namespace cppbuild;

    auto conf(const std::filesystem::path &src_dir,
              const std::filesystem::path &build_dir,
              const std::filesystem::path &toolchain_path,
              const std::filesystem::path &self_path) -> int
    {
        types::toolchain tc{};
        umka::umka umka{};
        auto targets = umka.run((src_dir / "build.um").string(), "configure");
        types::graph_result res{core::build_graph(targets, src_dir)};
        graaf::directed_graph<types::target, int> graph{std::move(res.g)};
        toolchain::parse_toolchain(toolchain_path, tc);
        compdb::write_named_module_compile_commands({.graph = graph,
                                                     .cxx_compiler = tc.cxx_compiler,
                                                     .cxxflags = helpers::to_views(tc.cxxflags),
                                                     .output_dir = build_dir});
        compdb::fill_module_names(graph, scanner::run_scan_deps(build_dir, tc));
        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            return 1;
        }
        ninja::write_ninja({.graph = graph,
                            .order = *order,
                            .cxx_compiler = tc.cxx_compiler,
                            .c_compiler = tc.c_compiler,
                            .archiver = tc.archiver,
                            .cxxflags = helpers::to_views(tc.cxxflags),
                            .cflags = helpers::to_views(tc.cflags),
                            .exe_ldflags = helpers::to_views(tc.exe_ldflags),
                            .shared_ldflags = helpers::to_views(tc.shared_ldflags),
                            .build_dir = build_dir,
                            .self_path = self_path});
        cache::save_cache(tc, graph, build_dir);
        compdb::write_compile_commands({.graph = graph,
                                        .cxx_compiler = tc.cxx_compiler,
                                        .c_compiler = tc.c_compiler,
                                        .cxxflags = helpers::to_views(tc.cxxflags),
                                        .cflags = helpers::to_views(tc.cflags),
                                        .output_dir = build_dir});
        return 0;
    }

    auto reconf(const std::filesystem::path &build_dir, const std::filesystem::path &self_path) -> int
    {
        types::cache_result cache = cache::load_cache(build_dir / "cache.json");
        graaf::directed_graph<types::target, int> graph{std::move(cache.graph.g)};
        compdb::fill_module_names(graph, scanner::run_scan_deps(build_dir, cache.toolchain));
        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            fmt::println("Reconfigure failed");
            return 1;
        }
        cppbuild::ninja::write_ninja({.graph = graph,
                     .order = *order,
                     .cxx_compiler = cache.toolchain.cxx_compiler,
                     .c_compiler = cache.toolchain.c_compiler,
                     .archiver = cache.toolchain.archiver,
                     .cxxflags = helpers::to_views(cache.toolchain.cxxflags),
                     .cflags = helpers::to_views(cache.toolchain.cflags),
                     .exe_ldflags = helpers::to_views(cache.toolchain.exe_ldflags),
                     .shared_ldflags = helpers::to_views(cache.toolchain.shared_ldflags),
                     .build_dir = build_dir,
                     .self_path = self_path});
        std::filesystem::remove(build_dir / "modules.dd");
        return 0;
    }
}
