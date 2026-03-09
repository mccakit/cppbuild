#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <graaflib/graaflib.hpp>
#include <simdjson.h>
import std;
struct target
{
    public:
        std::string src;
        std::string mname;
        std::string type;
};
int main()
{
    // configure
    graaf::directed_graph<target, int> g;
    const auto target1 = g.add_vertex(target{.src = "m4.c++", .mname = "", .type = ""});
    const auto target2 = g.add_vertex(target{.src = "m3.cppm", .mname = "", .type = ""});
    g.add_edge(target1, target2, 1);
    const auto target3 = g.add_vertex(target{.src = "m2.cc", .mname = "", .type = ""});
    g.add_edge(target1, target3, 1);
    const auto target4 = g.add_vertex(target{.src = "m5.C", .mname = "", .type = ""});
    const auto target5 = g.add_vertex(target{.src = "m1.cpp", .mname = "", .type = ""});
    g.add_edge(target3, target5, 1);
    g.add_edge(target2, target5, 1);
    g.add_edge(target4, target5, 1);
    const auto target6 = g.add_vertex(target{.src = "m6.CPP", .mname = "", .type = ""});
    g.add_edge(target4, target6, 1);
    g.add_edge(target3, target6, 1);
    const auto target7 = g.add_vertex(target{.src = "app.cpp", .mname = "", .type = "exe"});
    g.add_edge(target5, target7, 1);
    g.add_edge(target6, target7, 1);
    // emit naive compile_commands.json
    const std::string dir = std::filesystem::current_path().string();
    const std::string cxx = "clang++";
    std::string flags = "-std=c++20 -x c++";
    {
        auto f = fmt::output_file("compile_commands.json");
        f.print("[\n");
        const auto &vertices = g.get_vertices();
        std::size_t i = 0;
        for (const auto &[id, t] : vertices)
        {
            f.print("  {{\"directory\":\"{}\",\"command\":\"{} {} -c {}\",\"file\":\"{}\"}}{}", dir, cxx, flags, t.src,
                    t.src, i + 1 < vertices.size() ? ",\n" : "\n");
            ++i;
        }
        f.print("]\n");
    }
    // scan module deps
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
    // parse p1689 and fill mname
    simdjson::dom::parser parser;
    auto doc = parser.parse(scan_output);
    for (auto rule : doc["rules"])
    {
        simdjson::dom::array provides;
        if (rule["provides"].get(provides) != simdjson::SUCCESS)
            continue;
        for (auto p : provides)
        {
            std::string_view src = p["source-path"].get_string().value();
            std::string_view name = p["logical-name"].get_string().value();
            for (const auto &[id, t] : g.get_vertices())
                if (t.src == src)
                    g.get_vertex(id).mname = std::string(name);
        }
    }
    // print results
    for (auto &[id, t] : g.get_vertices())
        fmt::println("{} -> module '{}'", t.src, t.mname);

    // ninja
    flags = "-std=c++26";
    const auto order = graaf::algorithm::dfs_topological_sort(g);
    if (!order)
        return 1;

    auto nf = fmt::output_file("build.ninja");
    nf.print("rule precompile\n");
    nf.print("  command = {} {} --precompile -x c++-module $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
    nf.print("  description = PCM $out\n");
    nf.print("rule compile_pcm\n");
    nf.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
    nf.print("  description = OBJ $out\n");
    nf.print("rule compile_src\n");
    nf.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
    nf.print("  description = OBJ $out\n\n");

    // phase 1 - precompile (module units only)
    for (auto id : *order)
    {
        const auto &t = g.get_vertex(id);
        if (t.mname.empty())
            continue;
        const auto &deps = g.get_predecessors(id);
        std::string dep_pcms;
        for (auto dep_id : deps)
            dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).mname);
        nf.print("build {}.pcm: precompile {}{}\n", t.mname, t.src, dep_pcms.empty() ? "" : " |" + dep_pcms);
    }

    nf.print("\n");

    // phase 2 - codegen
    for (auto id : *order)
    {
        const auto &t = g.get_vertex(id);
        const auto &deps = g.get_predecessors(id);
        if (t.mname.empty())
        {
            // non-module source
            std::string dep_pcms;
            for (auto dep_id : deps)
                dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).mname);
            nf.print("build {}.o: compile_src {}{}\n", t.src.substr(0, t.src.rfind('.')), t.src,
                     dep_pcms.empty() ? "" : " |" + dep_pcms);
        }
        else
        {
            std::string dep_pcms = fmt::format("{}.pcm", t.mname);
            for (auto dep_id : deps)
                dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).mname);
            nf.print("build {}.o: compile_pcm {}.pcm | {}\n", t.mname, t.mname, dep_pcms);
        }
    }
    // phase 3 - link
    nf.print("rule link\n");
    nf.print("  command = {} $in -o $out\n", cxx);
    nf.print("  description = LINK $out\n\n");

    for (auto id : *order)
    {
        const auto &t = g.get_vertex(id);
        if (t.type != "exe")
            continue;
        std::string objs;
        for (const auto &[vid, vt] : g.get_vertices())
            objs += fmt::format(" {}", vt.mname.empty()
                ? vt.src.substr(0, vt.src.rfind('.')) + ".o"
                : vt.mname + ".o");
        nf.print("build {}: link {}\n", t.src.substr(0, t.src.rfind('.')), objs);
    }
    return 0;
}
