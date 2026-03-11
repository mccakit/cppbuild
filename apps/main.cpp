#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <graaflib/graaflib.hpp>
#include <simdjson.h>
#include <umka_api.h>

import std;

struct target
{
    std::string name;
    std::vector<std::filesystem::path> srcs;
    std::map<std::string, std::string> src_to_mname;
    std::string type;
    std::vector<std::string> deps;
};

struct umka_strarr
{
    const UmkaType *type;
    int64_t itemSize;
    const char **data;
};

struct umka_target
{
    const char *name;
    umka_strarr srcs;
    const char *type;
    umka_strarr deps;
};

struct umka_result
{
    const UmkaType *type;
    int64_t itemSize;
    umka_target *data;
};

struct umka_target_cxx
{
    std::string name;
    std::vector<std::string> srcs;
    std::string type;
    std::vector<std::string> deps;
};

int main(int argc, char **argv)
{
    const std::string_view cmd = argv[1];
    if (cmd == "configure")
    {
        const std::string script = argv[2];
        Umka *umka = umkaAlloc();
        if (!umkaInit(umka, script.c_str(), nullptr, 65536, nullptr, 0, nullptr, true, true, nullptr))
        {
            fmt::println("umkaInit failed: {}", umkaGetError(umka)->msg);
            return 1;
        }
        if (!umkaCompile(umka))
        {
            fmt::println("umkaCompile failed: {}", umkaGetError(umka)->msg);
            return 1;
        }
        if (umkaRun(umka) != 0)
        {
            fmt::println("umkaRun failed: {}", umkaGetError(umka)->msg);
            return 1;
        }

        UmkaFuncContext umka_fn = {0};
        if (!umkaGetFunc(umka, nullptr, "configure", &umka_fn))
        {
            fmt::println("umkaGetFunc failed");
            return 1;
        }


        umka_result result_storage{};
        UmkaStackSlot result_slot = {};
        result_slot.ptrVal = &result_storage;
        umka_fn.result = &result_slot;
        if (umkaCall(umka, &umka_fn) != 0)
        {
            fmt::println("umkaCall failed: {}", umkaGetError(umka)->msg);
            return 1;
        }

        auto *header = (umka_result *)result_slot.ptrVal;
        int len = umkaGetDynArrayLen(header);

        std::vector<umka_target_cxx> umka_targets;
        for (int i = 0; i < len; i++)
        {
            auto &t = header->data[i];
            int src_len = umkaGetDynArrayLen(&t.srcs);
            int dep_len = umkaGetDynArrayLen(&t.deps);
            std::vector<std::string> srcs, deps;
            for (int j = 0; j < src_len; j++)
                srcs.push_back(t.srcs.data[j]);
            for (int j = 0; j < dep_len; j++)
                deps.push_back(t.deps.data[j]);
            umka_targets.push_back({t.name, std::move(srcs), t.type, std::move(deps)});
        }

        for (auto &t : umka_targets)
            fmt::println("name={} type={} srcs=[{}] deps=[{}]", t.name, t.type,
                         fmt::join(t.srcs, ", "), fmt::join(t.deps, ", "));

        for (int i = 0; i < len; i++)
        {
            umkaDecRef(umka, header->data[i].srcs.data);
            umkaDecRef(umka, header->data[i].deps.data);
        }
        umkaDecRef(umka, result_storage.data);
        umkaFree(umka);

        graaf::directed_graph<target, int> g;
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
        for (auto &ut : umka_targets)
        {
            target t;
            t.name = ut.name;
            t.type = ut.type;
            t.deps = ut.deps;
            for (auto &src : ut.srcs)
                t.srcs.push_back(src);
            name_to_id[ut.name] = g.add_vertex(std::move(t));
        }
        for (auto &ut : umka_targets)
            for (auto &dep : ut.deps)
                if (name_to_id.contains(dep))
                    g.add_edge(name_to_id[ut.name], name_to_id[dep], 1);

        const std::string dir = std::filesystem::current_path().string();
        const std::string cxx = "clang++";
        const std::string flags = "-std=c++26 -Wno-experimental-header-units";

        // compile_commands.json (named modules only, for clang-scan-deps)
        {
            auto f = fmt::output_file("compile_commands.json");
            f.print("[\n");
            bool first = true;
            for (const auto &[id, t] : g.get_vertices())
            {
                if (t.type != "named_module")
                    continue;
                for (const auto &src : t.srcs)
                {
                    if (!first)
                        f.print(",\n");
                    f.print("  {{\"directory\":\"{}\",\"command\":\"{} {} -x c++ -c {}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                            dir, cxx, flags, src.string(), src.string(), src.string());
                    first = false;
                }
            }
            f.print("\n]\n");
        }

        // configure-time scan to get logical module names
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

        // parse p1689 and fill src_to_mname
        simdjson::dom::parser parser;
        auto doc = parser.parse(scan_output);
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
                continue;
            for (auto p : provides)
            {
                std::string src = std::string(p["source-path"].get_string().value());
                std::string name = std::string(p["logical-name"].get_string().value());
                for (auto &[id, t] : g.get_vertices())
                    for (const auto &tsrc : t.srcs)
                        if (tsrc.string() == src)
                            g.get_vertex(id).src_to_mname[src] = name;
            }
        }

        // topological sort
        const auto order = graaf::algorithm::dfs_topological_sort(g);
        if (!order)
            return 1;

        auto nf = fmt::output_file("build.ninja");

        // rules
        nf.print("rule scan_deps\n");
        nf.print("  command = clang-scan-deps -format=p1689 -compilation-database compile_commands.json | ./cppbuild scan $out\n");
        nf.print("  description = SCAN\n\n");
        nf.print("rule precompile\n");
        nf.print("  command = {} {} --precompile -x c++-module $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
        nf.print("  description = PCM $out\n\n");
        nf.print("rule compile_pcm\n");
        nf.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=.\n", cxx, flags);
        nf.print("  description = OBJ $out\n\n");
        nf.print("rule precompile_header_unit\n");
        nf.print("  command = {} {} -x c++-header -fmodule-header $in -o $out\n", cxx, flags);
        nf.print("  description = PCM $out\n\n");
        nf.print("rule compile_src\n");
        nf.print("  command = {} {} -c $in -o $out -fprebuilt-module-path=. $header_unit_deps\n", cxx, flags);
        nf.print("  description = OBJ $out\n\n");
        nf.print("rule link\n");
        nf.print("  command = {} $in -o $out\n", cxx);
        nf.print("  description = LINK $out\n\n");

        // modules.dd
        nf.print("build modules.dd: scan_deps compile_commands.json\n\n");

        // phase 1 - precompile
        for (auto id : *order)
        {
            const auto &t = g.get_vertex(id);
            const auto &deps = g.get_neighbors(id);
            if (t.type == "named_module")
            {
                for (const auto &src : t.srcs)
                {
                    const auto &mname = t.src_to_mname.at(src.string());
                    nf.print("build {}.pcm: precompile {} || modules.dd\n", mname, src.string());
                    nf.print("  dyndep = modules.dd\n");
                }
            }
            else if (t.type == "header_unit")
            {
                for (const auto &src : t.srcs)
                {
                    std::string dep_pcms;
                    for (auto dep_id : deps)
                        for (const auto &dep_src : g.get_vertex(dep_id).srcs)
                            dep_pcms += fmt::format(" {}.pcm", dep_src.string());
                    nf.print("build {}.pcm: precompile_header_unit {}{}\n", src.string(), src.string(),
                             dep_pcms.empty() ? "" : " |" + dep_pcms);
                }
            }
        }
        nf.print("\n");

        // phase 2 - codegen
        for (auto id : *order)
        {
            const auto &t = g.get_vertex(id);
            const auto &deps = g.get_neighbors(id);
            if (t.type == "named_module")
            {
                for (const auto &src : t.srcs)
                {
                    const auto &mname = t.src_to_mname.at(src.string());
                    nf.print("build {}.o: compile_pcm {}.pcm\n", mname, mname);
                }
            }
            else if (t.type == "translation_unit")
            {
                for (const auto &src : t.srcs)
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
                            for (const auto &dep_src : dep.srcs)
                            {
                                dep_pcms += fmt::format(" {}.pcm", dep_src.string());
                                header_unit_flags += fmt::format(" -fmodule-file={}.pcm", dep_src.string());
                            }
                            for (auto dep_id : g.get_neighbors(cur))
                                stk.push(dep_id);
                        }
                        else if (dep.type == "named_module")
                            for (const auto &[s, mname] : dep.src_to_mname)
                                dep_pcms += fmt::format(" {}.pcm", mname);
                    }
                    nf.print("build {}.o: compile_src {}{}\n", src.stem().string(), src.string(),
                             dep_pcms.empty() ? "" : " |" + dep_pcms);
                    if (!header_unit_flags.empty())
                        nf.print("  header_unit_deps ={}\n", header_unit_flags);
                }
            }
        }
        nf.print("\n");

        // phase 3 - link
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
                    for (const auto &src : vt.srcs)
                        objs += fmt::format(" {}.o", src.stem().string());
                else if (vt.type == "named_module")
                    for (const auto &[s, mname] : vt.src_to_mname)
                        objs += fmt::format(" {}.o", mname);
                for (auto dep_id : g.get_neighbors(cur))
                    stack.push(dep_id);
            }
            nf.print("build {}.elf: link {}\n", t.name, objs);
        }
    }
    else if (cmd == "scan")
    {
        std::string input;
        for (std::string line; std::getline(std::cin, line);)
            input += line + "\n";

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

        std::ofstream out(argv[2]);
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
                for (auto r : requires_)
                {
                    auto it = name_to_pcm.find(std::string(r["logical-name"].get_string().value()));
                    if (it != name_to_pcm.end())
                        deps += " " + it->second;
                }
            out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
        }
    }
    return 0;
}
