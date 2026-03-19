module;
#include <simdjson.h>

export module cppbuild:core;
import std;
import fmt;
import graaf;
import :types;
import :helpers;
import :ninja;
import :umka;
export namespace cppbuild
{
    using namespace cppbuild;
    auto build_graph(const umka_cxx_result &umka_result, const std::filesystem::path &src_dir) -> graph_result
    {
        graaf::directed_graph<target, int> graph;
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
        for (auto &ut : umka_result.build_targets)
        {
            target target;
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
