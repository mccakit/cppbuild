#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <graaflib/graaflib.hpp>
#include <simdjson.h>
#include <umka_api.h>

import std;
struct target
{
    public:
        std::string src;
        std::string mname;
        std::string type;
};

int main(int argc, char **argv)
{
    const std::string script = argv[1];
    Umka *umka = umkaAlloc();
    if (!umkaInit(umka, script.c_str(), nullptr, 65536, nullptr, 0, nullptr, true, true, nullptr))
    {
        return 1;
    }
    if (!umkaCompile(umka))
    {
        return 1;
    }
    if (umkaRun(umka) != 0)
    {
        return 1;
    }
    UmkaFuncContext umka_fn;
    if (!umkaGetFunc(umka, nullptr, "greet", &umka_fn))
    {
        return 1;
    }
    UmkaStackSlot resultSlot = {};
    umka_fn.params = nullptr;
    umka_fn.result = &resultSlot;
    if (!umkaCall(umka, &umka_fn))
    {
        return 1;
    }
    fmt::println("got: {}", static_cast<const char *>(resultSlot.ptrVal));
    umkaFree(umka);
    // configure
    graaf::directed_graph<target, int> g;
    // named module diamond
    const auto target1 = g.add_vertex(target{.src = "m4.c++", .mname = "", .type = "named_module"});
    const auto target2 = g.add_vertex(target{.src = "m3.cppm", .mname = "", .type = "named_module"});
    g.add_edge(target1, target2, 1);
    const auto target3 = g.add_vertex(target{.src = "m2.cc", .mname = "", .type = "named_module"});
    g.add_edge(target1, target3, 1);
    const auto target5 = g.add_vertex(target{.src = "m1.cpp", .mname = "", .type = "named_module"});
    g.add_edge(target3, target5, 1);
    g.add_edge(target2, target5, 1);
    const auto target6 = g.add_vertex(target{.src = "app0.cpp", .mname = "", .type = "translation_unit"});
    g.add_edge(target5, target6, 1);
    const auto target7 = g.add_vertex(target{.src = "app0", .mname = "", .type = "exe"});
    g.add_edge(target6, target7, 1);

    // header unit diamond
    const auto target8 = g.add_vertex(target{.src = "h1.hpp", .mname = "", .type = "header_unit"});
    const auto target9 = g.add_vertex(target{.src = "h1.cpp", .mname = "", .type = "translation_unit"});
    const auto target10 = g.add_vertex(target{.src = "h2.h", .mname = "", .type = "header_unit"});
    const auto target11 = g.add_vertex(target{.src = "h2.cpp", .mname = "", .type = "translation_unit"});
    const auto target12 = g.add_vertex(target{.src = "h3.hh", .mname = "", .type = "header_unit"});
    const auto target13 = g.add_vertex(target{.src = "h3.cpp", .mname = "", .type = "translation_unit"});
    g.add_edge(target8, target10, 1);
    g.add_edge(target8, target12, 1);
    const auto target14 = g.add_vertex(target{.src = "h4.h++", .mname = "", .type = "header_unit"});
    const auto target15 = g.add_vertex(target{.src = "h4.cpp", .mname = "", .type = "translation_unit"});
    g.add_edge(target10, target14, 1);
    g.add_edge(target12, target14, 1);
    const auto target16 = g.add_vertex(target{.src = "app1.cpp", .mname = "", .type = "translation_unit"});
    g.add_edge(target14, target16, 1);
    const auto target17 = g.add_vertex(target{.src = "app1", .mname = "", .type = "exe"});
    // impl cpps and app all just link together
    g.add_edge(target9, target17, 1);
    g.add_edge(target11, target17, 1);
    g.add_edge(target13, target17, 1);
    g.add_edge(target15, target17, 1);
    g.add_edge(target16, target17, 1);
    // emit P1689.json for clang-scan-deps (named modules only)
    const std::string dir = std::filesystem::current_path().string();
    const std::string cxx = "clang++";
    std::string flags = "-std=c++20 -x c++";
    {
        auto f = fmt::output_file("P1689.json");
        f.print("[\n");
        const auto &vertices = g.get_vertices();
        std::vector<std::pair<graaf::vertex_id_t, target>> filtered;
        for (const auto &[id, t] : vertices)
            if (t.type == "named_module")
                filtered.emplace_back(id, t);
        std::size_t i = 0;
        for (const auto &[id, t] : filtered)
        {
            f.print("  {{\"directory\":\"{}\",\"command\":\"{} {} -c {}\",\"file\":\"{}\",\"output\":\"{}.o\"}}{}", dir,
                    cxx, flags, t.src, t.src, t.src, i + 1 < filtered.size() ? ",\n" : "\n");
            ++i;
        }
        f.print("]\n");
    }
    // scan module deps
    std::string scan_output;
    {
        FILE *pipe = popen("clang-scan-deps -format=p1689 -compilation-database P1689.json", "r");
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

    // emit compile_commands.json for clangd (all compilable sources)
    {
        auto f = fmt::output_file("compile_commands.json");
        f.print("[\n");
        std::vector<std::pair<graaf::vertex_id_t, target>> sources;
        for (const auto &[id, t] : g.get_vertices())
            if (t.type == "named_module" || t.type == "translation_unit")
                sources.emplace_back(id, t);
        std::size_t i = 0;
        for (const auto &[id, t] : sources)
        {
            std::string cmd = fmt::format("{} -std=c++26 -fprebuilt-module-path=. -c {}", cxx, t.src);
            // add -fmodule-file= for header unit deps
            std::set<graaf::vertex_id_t> visited;
            std::stack<graaf::vertex_id_t> stk;
            for (auto dep_id : g.get_predecessors(id))
                stk.push(dep_id);
            while (!stk.empty())
            {
                auto cur = stk.top();
                stk.pop();
                if (visited.contains(cur))
                    continue;
                visited.insert(cur);
                const auto &dep = g.get_vertex(cur);
                if (dep.type == "header_unit")
                {
                    cmd += fmt::format(" -fmodule-file={}.pcm", dep.src);
                    for (auto dep_id : g.get_predecessors(cur))
                        stk.push(dep_id);
                }
            }
            f.print("  {{\"directory\":\"{}\",\"command\":\"{}\",\"file\":\"{}\"}}{}", dir, cmd, t.src,
                    i + 1 < sources.size() ? ",\n" : "\n");
            ++i;
        }
        f.print("]\n");
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

    nf.print("rule precompile_header_unit\n");
    nf.print("  command = {} {} -x c++-header -fmodule-header $in -o $out\n", cxx, flags);
    nf.print("  description = PCM $out\n");

    nf.print("rule compile_src\n");
    nf.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=. $header_unit_deps\n", cxx, flags);
    nf.print("  description = OBJ $out\n");
    // phase 1 - precompile
    for (auto id : *order)
    {
        const auto &t = g.get_vertex(id);
        const auto &deps = g.get_predecessors(id);

        if (t.type == "named_module")
        {
            std::string dep_pcms;
            for (auto dep_id : deps)
                dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).mname);
            nf.print("build {}.pcm: precompile {}{}\n", t.mname, t.src, dep_pcms.empty() ? "" : " |" + dep_pcms);
        }
        else if (t.type == "header_unit")
        {
            std::string dep_pcms;
            for (auto dep_id : deps)
                dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).src);
            const bool needs_x = t.src.ends_with(".h++") || t.src.ends_with(".hxx");
            nf.print("build {}.pcm: precompile_header_unit {}{}\n", t.src, t.src,
                     dep_pcms.empty() ? "" : " |" + dep_pcms);
        }
    }
    nf.print("\n");

    // phase 2 - codegen
    for (auto id : *order)
    {
        const auto &t = g.get_vertex(id);
        const auto &deps = g.get_predecessors(id);

        if (t.type == "named_module")
        {
            std::string dep_pcms = fmt::format("{}.pcm", t.mname);
            for (auto dep_id : deps)
                dep_pcms += fmt::format(" {}.pcm", g.get_vertex(dep_id).mname);
            nf.print("build {}.o: compile_pcm {}.pcm | {}\n", t.mname, t.mname, dep_pcms);
        }
        else if (t.type == "translation_unit")
        {
            std::string dep_pcms;
            std::string header_unit_flags;
            std::set<graaf::vertex_id_t> visited;
            std::stack<graaf::vertex_id_t> stk;
            for (auto dep_id : deps)
                stk.push(dep_id);
            while (!stk.empty())
            {
                auto cur = stk.top();
                stk.pop();
                if (visited.contains(cur))
                    continue;
                visited.insert(cur);
                const auto &dep = g.get_vertex(cur);
                if (dep.type == "header_unit")
                {
                    dep_pcms += fmt::format(" {}.pcm", dep.src);
                    header_unit_flags += fmt::format(" -fmodule-file={}.pcm", dep.src);
                    for (auto dep_id : g.get_predecessors(cur))
                        stk.push(dep_id);
                }
                else if (dep.type == "named_module")
                    dep_pcms += fmt::format(" {}.pcm", dep.mname);
            }
            nf.print("build {}.o: compile_src {}{}\n", t.src.substr(0, t.src.rfind('.')), t.src,
                     dep_pcms.empty() ? "" : " |" + dep_pcms);
            if (!header_unit_flags.empty())
                nf.print("  header_unit_deps ={}\n", header_unit_flags);
        }
    }

    nf.print("\n");

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
        std::set<graaf::vertex_id_t> visited;
        std::stack<graaf::vertex_id_t> stack;
        stack.push(id);
        while (!stack.empty())
        {
            auto cur = stack.top();
            stack.pop();
            if (visited.contains(cur))
                continue;
            visited.insert(cur);
            const auto &vt = g.get_vertex(cur);
            if (vt.type == "translation_unit")
                objs += fmt::format(" {}", vt.src.substr(0, vt.src.rfind('.')) + ".o");
            else if (vt.type == "named_module")
                objs += fmt::format(" {}.o", vt.mname);
            for (auto dep_id : g.get_predecessors(cur))
                stack.push(dep_id);
        }
        nf.print("build {}: link {}\n", t.src, objs);
    }
    return 0;
}
