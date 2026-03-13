#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <graaflib/graaflib.hpp>
#include <simdjson.h>
#include <string_view>
#include <umka_api.h>

import std;
import umka_cxx;

struct target
{
    public:
        std::string name;
        std::vector<std::filesystem::path> srcs;
        std::map<std::string, std::string> src_to_mname;
        std::string type;
        std::vector<std::string> deps;
};

struct write_compilation_database_options
{
    public:
        graaf::directed_graph<target, int> &graph;
        std::string_view cxx;
        std::string_view flags;
};

auto write_compilation_database(const write_compilation_database_options &opts) -> void
{
    fmt::ostream file = fmt::output_file("compile_commands.json");
    file.print("[\n");
    bool first{true};
    for (const auto &[id, t] : opts.graph.get_vertices())
    {
        if (t.type != "named_module")
            continue;
        for (const auto &src : t.srcs)
        {
            if (!first)
                file.print(",\n");
            file.print("  {{\"directory\":\"{}\",\"command\":\"{} {} -x c++ -c "
                       "{}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                       "build",
                       opts.cxx,
                       opts.flags,
                       src.string(),
                       src.string(),
                       src.string());
            first = false;
        }
    }
    file.print("\n]\n");
}

auto fill_module_names(graaf::directed_graph<target, int> &graph, std::string_view scan_output) -> void
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

auto write_ninja_rules(fmt::ostream &file, const std::string_view cxx, const std::string_view flags) -> void
{
    file.print("rule scan_deps\n");
    file.print("  command = clang-scan-deps -format=p1689 -compilation-database compile_commands.json | ./cppbuild "
               "scan $out\n");
    file.print("  description = SCAN\n\n");
    file.print("rule precompile\n");
    file.print("  command = {} {} --precompile -x c++-module $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
    file.print("  description = PCM $out\n\n");
    file.print("rule compile_pcm\n");
    file.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
    file.print("  description = OBJ $out\n\n");
    file.print("rule precompile_header_unit\n");
    file.print("  command = {} {} -x c++-header -fmodule-header $in -o $out\n", cxx, flags);
    file.print("  description = PCM $out\n\n");
    file.print("rule compile_src\n");
    file.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=. $header_unit_deps\n", cxx, flags);
    file.print("  description = OBJ $out\n\n");
    file.print("rule link\n");
    file.print("  command = {} $in -o $out\n", cxx);
    file.print("  description = LINK $out\n\n");
    file.print("build modules.dd: scan_deps compile_commands.json\n\n");
}

auto write_phase_precompile(fmt::ostream &file,
                            const graaf::directed_graph<target, int> &graph,
                            const std::vector<graaf::vertex_id_t> &order) -> void
{
    for (auto id : order)
    {
        const auto &target = graph.get_vertex(id);
        const auto &deps = graph.get_neighbors(id);
        if (target.type == "named_module")
        {
            for (const auto &src : target.srcs)
            {
                const auto &mname = target.src_to_mname.at(src.string());
                file.print("build {}.pcm: precompile {} || modules.dd\n", mname, src.string());
                file.print("  dyndep = modules.dd\n");
            }
        }
        else if (target.type == "header_unit")
        {
            for (const auto &src : target.srcs)
                file.print("build {}.pcm: precompile_header_unit {}\n", src.string(), src.string());
        }
    }
    file.print("\n");
}

auto collect_deps(const graaf::directed_graph<target, int> &graph, graaf::vertex_id_t id)
    -> std::vector<graaf::vertex_id_t>
{
    std::vector<graaf::vertex_id_t> deps;
    std::set<graaf::vertex_id_t> visited;
    for (auto dep_id : graph.get_neighbors(id))
    {
        if (visited.insert(dep_id).second)
        {
            deps.push_back(dep_id);
        }
    }
    for (int i = 0; i < deps.size(); i++)
    {
        for (auto dep_id : graph.get_neighbors(deps[i]))
        {
            if (visited.insert(dep_id).second)
            {
                deps.push_back(dep_id);
            }
        }
    }
    return deps;
}

auto write_phase_codegen(fmt::ostream &file,
                         const graaf::directed_graph<target, int> &graph,
                         const std::vector<graaf::vertex_id_t> &order) -> void
{
    for (auto id : order)
    {
        const auto &target = graph.get_vertex(id);
        const auto deps = collect_deps(graph, id);

        if (target.type == "named_module")
        {
            for (const auto &src : target.srcs)
            {
                const auto &mname = target.src_to_mname.at(src.string());
                file.print("build {}.o: compile_pcm {}.pcm\n", mname, mname);
            }
        }
        else if (target.type == "translation_unit")
        {
            for (const auto &src : target.srcs)
            {
                std::string dep_pcms;
                std::string header_unit_flags;
                for (auto dep_id : deps)
                {
                    const auto &dep = graph.get_vertex(dep_id);
                    if (dep.type == "header_unit")
                    {
                        for (const auto &dep_src : dep.srcs)
                        {
                            dep_pcms += fmt::format(" {}.pcm", dep_src.string());
                            header_unit_flags += fmt::format(" -fmodule-file={}.pcm", dep_src.string());
                        }
                    }

                    else if (dep.type == "named_module")
                    {
                        for (const auto &[s, mname] : dep.src_to_mname)
                        {
                            dep_pcms += fmt::format(" {}.pcm", mname);
                        }
                    }
                }
                const auto order_only = dep_pcms.empty() ? "" : " |" + dep_pcms;
                file.print("build {}.o: compile_src {}{}\n", src.stem().string(), src.string(), order_only);
                if (!header_unit_flags.empty())
                {
                    file.print("  header_unit_deps ={}\n", header_unit_flags);
                }
            }
        }
    }
    file.print("\n");
}

auto write_phase_link(fmt::ostream &file,
                      const graaf::directed_graph<target, int> &graph,
                      const std::vector<graaf::vertex_id_t> &order) -> void
{
    for (auto id : order)
    {
        const auto &target = graph.get_vertex(id);
        const auto deps = collect_deps(graph, id);
        std::string objs;
        for (auto dep_id : deps)
        {
            const auto &dep = graph.get_vertex(dep_id);
            if (dep.type == "translation_unit")
                for (const auto &src : dep.srcs)
                    objs += fmt::format(" {}.o", src.stem().string());
            else if (dep.type == "named_module")
                for (const auto &[s, mname] : dep.src_to_mname)
                    objs += fmt::format(" {}.o", mname);
        }
        if (target.type == "exe")
        {
            file.print("build {}.elf: link{}\n", target.name, objs);
        }
    }
}

struct write_ninja_options
{
    public:
        const graaf::directed_graph<target, int> &graph;
        const std::vector<graaf::vertex_id_t> &order;
        std::string_view cxx;
        std::string_view flags;
};
auto write_ninja(write_ninja_options opts) -> void
{
    fmt::ostream file{fmt::output_file("build.ninja")};
    write_ninja_rules(file, opts.cxx, opts.flags);
    write_phase_precompile(file, opts.graph, opts.order);
    write_phase_codegen(file, opts.graph, opts.order);
    write_phase_link(file, opts.graph, opts.order);
}

void cmd_scan(int argc, char **argv)
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

    std::ofstream out(argv[2]);
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

struct graph_result
{
    public:
        graaf::directed_graph<target, int> g;
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
};

auto build_graph(const std::vector<umka_cxx::cxxtarget> &targets) -> graph_result
{
    graaf::directed_graph<target, int> graph;
    std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;

    for (auto &ut : targets)
    {
        target target;
        target.name = ut.name;
        target.type = ut.type;
        target.deps = ut.deps;
        for (auto &src : ut.srcs)
        {
            target.srcs.push_back(src);
        }
        name_to_id[ut.name] = graph.add_vertex(std::move(target));
    }

    for (auto &ut : targets)
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

auto main(int argc, char **argv) -> int
{
    const std::string_view cmd = argv[1];
    if (cmd == "configure")
    {
        umka_cxx::umka umka{};
        auto targets = umka.run(argv[2], "configure");
        graph_result res{build_graph(targets)};
        graaf::directed_graph<target, int> graph{std::move(res.g)};
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id{std::move(res.name_to_id)};
        const std::string dir = std::filesystem::current_path().string();
        const std::string cxx = "clang++";
        const std::string flags = "-std=c++26 -Wno-experimental-header-units";

        write_compilation_database({.graph = graph, .cxx = cxx, .flags = flags});

        std::string scan_output;
        {
            FILE *pipe = popen("clang-scan-deps -format=p1689 -compilation-database compile_commands.json", "r");
            if (!pipe)
                return 1;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe))
                scan_output += buf;
            pclose(pipe);
        }

        fill_module_names(graph, scan_output);

        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            return 1;
        }
        write_ninja({.graph = graph, .order = *order, .cxx = cxx, .flags = flags});
    }
    else if (cmd == "scan")
    {
        cmd_scan(argc, argv);
    }
    return 0;
}
