module;
#include <cstdlib>
#include <vector>
#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include "graaflib/graph.h"
#include <simdjson.h>

export module cppbuild.core;
import std;
import cppbuild.types;
import cppbuild.helpers;
import cppbuild.ninja;
import cppbuild.umka;
export namespace cppbuild::core
{
    using namespace cppbuild;
    auto build_graph(const umka::umka_cxx_result &umka_result, const std::filesystem::path &src_dir) -> types::graph_result
    {
        graaf::directed_graph<types::target, int> graph;
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
        for (auto &ut : umka_result.build_targets)
        {
            types::target target;
            target.name = ut.name;
            target.type = ut.kind;
            target.deps = ut.deps;
            target.cxxflags = ut.cxxflags;
            target.cflags = ut.cflags;
            target.ldflags = ut.ldflags;
            target.srcs = ut.srcs;
            name_to_id[ut.name] = graph.add_vertex(std::move(target));
        }
        for (auto &ut : umka_result.build_targets)
        {
            for (auto &dep : ut.deps)
            {
                if (name_to_id.contains(dep))
                {
                    graph.add_edge(name_to_id[ut.name], name_to_id[dep], 1);
                }
            }
        }
        return {std::move(graph), std::move(name_to_id)};
    }
}
