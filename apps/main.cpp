#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <graaflib/graaflib.hpp>
#include <simdjson.h>
#include <string_view>
#include <umka_api.h>
#include <subprocess.h>

import std;
import umka_cxx;
struct toolchain
{
        std::string cxx_compiler = "clang++";
        std::string c_compiler = "clang";
        std::string archiver = "llvm-ar";
        std::vector<std::string> cxxflags = {"-std=c++26", "-Wno-experimental-header-units", "-flto=thin"};
        std::vector<std::string> cflags = {"-std=c23", "-flto=thin"};
        std::vector<std::string> exe_ldflags{"-flto=thin"};
        std::vector<std::string> shared_ldflags{"-flto=thin"};
};

auto parse_toolchain(const std::filesystem::path &path, toolchain &tc) -> void
{
    simdjson::dom::parser parser;
    auto doc = parser.load(path.string());
    if (auto val = doc["cxx_compiler"]; val.error() == simdjson::SUCCESS)
    {
        tc.cxx_compiler = std::string(val.get_string().value());
    }
    if (auto val = doc["c_compiler"]; val.error() == simdjson::SUCCESS)
    {
        tc.c_compiler = std::string(val.get_string().value());
    }
    if (auto val = doc["archiver"]; val.error() == simdjson::SUCCESS)
    {
        tc.archiver = std::string(val.get_string().value());
    }
    auto parse_flags = [&](std::string_view key, std::vector<std::string> &out) {
        simdjson::dom::array arr;
        if (doc[key].get(arr) == simdjson::SUCCESS)
        {
            out.clear();
            for (auto f : arr)
            {
                out.push_back(std::string(f.get_string().value()));
            }
        }
    };
    parse_flags("cxxflags", tc.cxxflags);
    parse_flags("cflags", tc.cflags);
    parse_flags("exe_ldflags", tc.exe_ldflags);
    parse_flags("shared_ldflags", tc.shared_ldflags);
}

auto join_flags(const std::vector<std::string_view> &flags) -> std::string
{
    return fmt::format("{}", fmt::join(flags, " "));
}

struct target
{
    public:
        std::string name;
        std::vector<std::filesystem::path> srcs;
        std::map<std::string, std::string> src_to_mname;
        std::string type;
        std::vector<std::string> deps;
        std::vector<std::string> cxxflags;
        std::vector<std::string> cflags;
        std::vector<std::string> ldflags;
};

struct write_module_commands_options
{
    public:
        graaf::directed_graph<target, int> &graph;
        std::string_view cxx_compiler;
        std::vector<std::string_view> cxxflags;
};

struct write_compile_commands_options
{
    public:
        graaf::directed_graph<target, int> &graph;
        std::string_view cxx_compiler;
        std::string_view c_compiler;
        std::vector<std::string_view> cxxflags;
        std::vector<std::string_view> cflags;
};

auto write_named_module_compile_commands(const write_module_commands_options &opts) -> void
{
    fmt::ostream file = fmt::output_file("module_commands.json");
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
            file.print("  {{\"directory\":\"{}\",\"command\":\"{} {} {} -x c++ -c "
                       "{}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                       "build",
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
    fmt::ostream file = fmt::output_file("compile_commands.json");
    file.print("[\n");
    bool first{true};
    for (const auto &[id, t] : opts.graph.get_vertices())
    {
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
            file.print("  {{\"directory\":\"{}\",\"command\":\"{} {} -c "
                       "{}\",\"file\":\"{}\",\"output\":\"{}.o\"}}",
                       "build",
                       compiler,
                       flags,
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

auto write_ninja_rules(fmt::ostream &file,
                       const std::string_view cxx_compiler,
                       const std::string_view c_compiler,
                       const std::string_view archiver) -> void
{
    file.print("rule scan_deps\n");
    file.print("  command = clang-scan-deps -format=p1689 -compilation-database module_commands.json | ./cppbuild "
               "--scan $out\n");
    file.print("  description = SCAN\n\n");
    file.print("rule precompile\n");
    file.print("  command = {} --precompile -x c++-module -Wno-experimental-header-units $in -o $out "
               "-fprebuilt-module-path=. $cxxflags\n",
               cxx_compiler);
    file.print("  description = PCM $out\n\n");
    file.print("rule compile_pcm\n");
    file.print("  command = {} -c $in -o $out -fprebuilt-module-path=. $cxxflags\n", cxx_compiler);
    file.print("  description = OBJ $out\n\n");
    file.print("rule precompile_header_unit\n");
    file.print("  command = {} -x c++-header -Wno-experimental-header-units -fmodule-header $in -o $out $cxxflags\n",
               cxx_compiler);
    file.print("  description = PCM $out\n\n");
    file.print("rule compile_cxx_translation_unit\n");
    file.print("  command = {} -c $in -o $out -Wno-experimental-header-units -fprebuilt-module-path=. $cxxflags "
               "$header_unit_deps\n",
               cxx_compiler);
    file.print("  description = OBJ $out\n\n");
    file.print("rule compile_c_translation_unit\n");
    file.print("  command = {} -c $in -o $out $cflags\n", c_compiler);
    file.print("  description = OBJ $out\n\n");
    file.print("rule link\n");
    file.print("  command = {} $in -o $out $ldflags\n", cxx_compiler);
    file.print("  description = LINK $out\n\n");
    file.print("build modules.dd: scan_deps module_commands.json\n\n");
    file.print("rule archive\n");
    file.print("  command = {} rcs $out $in\n", archiver);
    file.print("  description = AR $out\n\n");
}

auto write_phase_precompile(fmt::ostream &file,
                            const graaf::directed_graph<target, int> &graph,
                            const std::vector<graaf::vertex_id_t> &order,
                            const std::vector<std::string_view> &cxxflags) -> void
{
    for (auto id : order)
    {
        const auto &target = graph.get_vertex(id);
        if (target.type == "named_module")
        {
            for (const auto &src : target.srcs)
            {
                const auto &mname = target.src_to_mname.at(src.string());
                file.print("build {}.pcm: precompile {} || modules.dd\n", mname, src.string());
                file.print("  dyndep = modules.dd\n");
                if (!target.cxxflags.empty() or !cxxflags.empty())
                {
                    file.print("  cxxflags = {} {}\n", fmt::join(cxxflags, " "), fmt::join(target.cxxflags, " "));
                }
            }
        }
        else if (target.type == "header_unit")
        {
            for (const auto &src : target.srcs)
            {
                file.print("build {}.pcm: precompile_header_unit {}\n", src.string(), src.string());
                if (!cxxflags.empty() or !target.cxxflags.empty())
                {
                    file.print("  cxxflags = {} {}\n", fmt::join(cxxflags, " "), fmt::join(target.cxxflags, " "));
                }
            }
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
                         const std::vector<graaf::vertex_id_t> &order,
                         const std::vector<std::string_view> &cxxflags,
                         const std::vector<std::string_view> &cflags) -> void
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
                if (!target.cxxflags.empty() or !target.cflags.empty())
                {
                    file.print("  cxxflags = {} {}\n", fmt::join(cxxflags, " "), fmt::join(target.cxxflags, " "));
                }
            }
        }
        else if (target.type == "translation_unit")
        {
            for (const auto &src : target.srcs)
            {
                const bool is_c = src.extension() == ".c";
                std::string dep_pcms;
                std::string header_unit_deps;
                for (auto dep_id : deps)
                {
                    const auto &dep = graph.get_vertex(dep_id);
                    if (dep.type == "header_unit")
                    {
                        for (const auto &dep_src : dep.srcs)
                        {
                            dep_pcms += fmt::format(" {}.pcm", dep_src.string());
                            header_unit_deps += fmt::format(" -fmodule-file={}.pcm", dep_src.string());
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
                if (is_c)
                {
                    file.print(
                        "build {}.o: compile_c_translation_unit {}{}\n", src.stem().string(), src.string(), order_only);
                    if (!target.cflags.empty() or !cflags.empty())
                    {
                        file.print("  cflags = {} {}\n", fmt::join(cflags, " "), fmt::join(target.cflags, " "));
                    }
                }
                else
                {
                    file.print("build {}.o: compile_cxx_translation_unit {}{}\n",
                               src.stem().string(),
                               src.string(),
                               order_only);
                    if (!target.cxxflags.empty() or !cxxflags.empty())
                    {
                        file.print("  cxxflags = {} {}\n", fmt::join(cxxflags, " "), fmt::join(target.cxxflags, " "));
                    }
                    if (!header_unit_deps.empty())
                    {
                        file.print("  header_unit_deps ={}\n", header_unit_deps);
                    }
                }
            }
        }
    }
    file.print("\n");
}

auto write_phase_link(fmt::ostream &file,
                      const graaf::directed_graph<target, int> &graph,
                      const std::vector<graaf::vertex_id_t> &order,
                      const std::vector<std::string_view> &exe_ldflags,
                      const std::vector<std::string_view> &shared_ldflags) -> void
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
            if (!target.ldflags.empty() or !exe_ldflags.empty())
            {
                file.print("  ldflags = {} {}\n", fmt::join(exe_ldflags, " "), fmt::join(target.ldflags, " "));
            }
        }
        else if (target.type == "shared")
        {
            file.print("build lib{}.so: link{}\n", target.name, objs);
            if (!shared_ldflags.empty() or !target.ldflags.empty())
            {
                file.print(
                    "  ldflags = {} {} -shared \n", fmt::join(shared_ldflags, " "), fmt::join(target.ldflags, " "));
            }
        }
        else if (target.type == "static")
        {
            file.print("build lib{}.a: archive{}\n", target.name, objs);
        }
    }
}

struct write_ninja_options
{
    public:
        const graaf::directed_graph<target, int> &graph;
        const std::vector<graaf::vertex_id_t> &order;
        std::string_view cxx_compiler;
        std::string_view c_compiler;
        std::string_view archiver;
        std::vector<std::string_view> cxxflags;
        std::vector<std::string_view> cflags;
        std::vector<std::string_view> exe_ldflags;
        std::vector<std::string_view> shared_ldflags;
};

auto write_ninja(write_ninja_options opts) -> void
{
    fmt::ostream file{fmt::output_file("build.ninja")};
    write_ninja_rules(file, opts.cxx_compiler, opts.c_compiler, opts.archiver);
    write_phase_precompile(file, opts.graph, opts.order, opts.cxxflags);
    write_phase_codegen(file, opts.graph, opts.order, opts.cxxflags, opts.cflags);
    write_phase_link(file, opts.graph, opts.order, opts.exe_ldflags, opts.shared_ldflags);
}

void cmd_scan(const std::filesystem::path &path)
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
        target.cxxflags = ut.cxxflags;
        target.cflags = ut.cflags;
        target.ldflags = ut.ldflags;
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

auto run_scan_deps() -> std::string
{
    const char *command_line[] = {
        "clang-scan-deps", "-format=p1689", "-compilation-database", "module_commands.json", NULL};

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

auto to_views(const std::vector<std::string> &vec) -> std::vector<std::string_view>
{
    return {vec.begin(), vec.end()};
}

auto quote_flags(const std::vector<std::string> &flags) -> std::string
{
    std::string result;
    for (size_t i = 0; i < flags.size(); i++)
    {
        result += fmt::format("\"{}\"", flags[i]);
        if (i + 1 < flags.size())
        {
            result += ",";
        }
    }
    return result;
}

auto save_cache(const toolchain &tc, const graaf::directed_graph<target, int> &graph) -> void
{
    fmt::ostream file = fmt::output_file("cache.json");
    file.print("{{\n");
    file.print("  \"toolchain\": {{\n");
    file.print("    \"cxx_compiler\": \"{}\",\n", tc.cxx_compiler);
    file.print("    \"c_compiler\": \"{}\",\n", tc.c_compiler);
    file.print("    \"archiver\": \"{}\",\n", tc.archiver);
    file.print("    \"cxxflags\": [{}],\n", quote_flags(tc.cxxflags));
    file.print("    \"cflags\": [{}],\n", quote_flags(tc.cflags));
    file.print("    \"exe_ldflags\": [{}],\n", quote_flags(tc.exe_ldflags));
    file.print("    \"shared_ldflags\": [{}]\n", quote_flags(tc.shared_ldflags));
    file.print("  }},\n");
    file.print("  \"targets\": [\n");
    bool first{true};
    for (const auto &[id, t] : graph.get_vertices())
    {
        if (!first)
        {
            file.print(",\n");
        }
        file.print("    {{\"name\":\"{}\",\"type\":\"{}\",\"srcs\":[", t.name, t.type);
        bool first_src{true};
        for (const auto &src : t.srcs)
        {
            if (!first_src)
            {
                file.print(",");
            }
            file.print("\"{}\"", src.string());
            first_src = false;
        }
        file.print("],\"deps\":[");
        bool first_dep{true};
        for (const auto &dep : t.deps)
        {
            if (!first_dep)
            {
                file.print(",");
            }
            file.print("\"{}\"", dep);
            first_dep = false;
        }
        file.print("]}}\n");
        first = false;
    }
    file.print("  ]\n");
    file.print("}}\n");
}

auto load_graph(const std::filesystem::path &path) -> graph_result
{
    simdjson::dom::parser parser;
    auto doc = parser.load(path.string());
    std::vector<umka_cxx::cxxtarget> targets;
    for (auto t : doc["targets"])
    {
        umka_cxx::cxxtarget ct;
        ct.name = std::string(t["name"].get_string().value());
        ct.type = std::string(t["type"].get_string().value());
        for (auto src : t["srcs"].get_array().value())
        {
            ct.srcs.push_back(std::string(src.get_string().value()));
        }
        for (auto dep : t["deps"].get_array().value())
        {
            ct.deps.push_back(std::string(dep.get_string().value()));
        }
        targets.push_back(std::move(ct));
    }
    return build_graph(targets);
}

struct cache_result
{
        graph_result graph;
        toolchain toolchain;
};
auto load_cache(const std::filesystem::path &path) -> cache_result
{
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    cache_result result;
    if (parser.load(path.string()).get(doc) != simdjson::SUCCESS)
    {
        return result;
    }
    toolchain tc;
    simdjson::dom::element tc_doc;
    if (doc["toolchain"].get(tc_doc) != simdjson::SUCCESS)
    {
        return result;
    }
    if (auto val = tc_doc["cxx_compiler"]; val.error() == simdjson::SUCCESS)
    {
        tc.cxx_compiler = std::string(val.get_string().value());
    }
    if (auto val = tc_doc["c_compiler"]; val.error() == simdjson::SUCCESS)
    {
        tc.c_compiler = std::string(val.get_string().value());
    }
    if (auto val = tc_doc["archiver"]; val.error() == simdjson::SUCCESS)
    {
        tc.archiver = std::string(val.get_string().value());
    }

    simdjson::dom::array arr;
    if (tc_doc["cxxflags"].get(arr) == simdjson::SUCCESS)
    {
        for (auto f : arr)
        {
            tc.cxxflags.push_back(std::string(f.get_string().value()));
        }
    }
    if (tc_doc["cflags"].get(arr) == simdjson::SUCCESS)
    {
        for (auto f : arr)
        {
            tc.cflags.push_back(std::string(f.get_string().value()));
        }
    }
    if (tc_doc["exe_ldflags"].get(arr) == simdjson::SUCCESS)
    {
        for (auto f : arr)
        {
            tc.exe_ldflags.push_back(std::string(f.get_string().value()));
        }
    }
    if (tc_doc["shared_ldflags"].get(arr) == simdjson::SUCCESS)
    {
        for (auto f : arr)
        {
            tc.shared_ldflags.push_back(std::string(f.get_string().value()));
        }
    }

    std::vector<umka_cxx::cxxtarget> targets;
    for (auto t : doc["targets"])
    {
        umka_cxx::cxxtarget ct;
        ct.name = std::string(t["name"].get_string().value());
        ct.type = std::string(t["type"].get_string().value());
        for (auto src : t["srcs"].get_array().value())
        {
            ct.srcs.push_back(std::string(src.get_string().value()));
        }
        for (auto dep : t["deps"].get_array().value())
        {
            ct.deps.push_back(std::string(dep.get_string().value()));
        }
        targets.push_back(std::move(ct));
    }

    result.toolchain = tc;
    result.graph = build_graph(targets);
    return result;
}

auto reconfigure() -> int
{
    cache_result cache = load_cache("cache.json");
    graaf::directed_graph<target, int> graph{std::move(cache.graph.g)};
    fill_module_names(graph, run_scan_deps());
    const auto order = graaf::algorithm::dfs_topological_sort(graph);
    if (!order)
    {
        fmt::println("Reconfigure failed");
        return 1;
    }
    write_ninja({.graph = graph,
                 .order = *order,
                 .cxx_compiler = cache.toolchain.cxx_compiler,
                 .c_compiler = cache.toolchain.c_compiler,
                 .archiver = cache.toolchain.archiver,
                 .cxxflags = to_views(cache.toolchain.cxxflags),
                 .cflags = to_views(cache.toolchain.cflags),
                 .exe_ldflags = to_views(cache.toolchain.exe_ldflags),
                 .shared_ldflags = to_views(cache.toolchain.shared_ldflags)});
    std::filesystem::remove("./modules.dd");
    return 0;
}

auto main(int argc, char **argv) -> int
{
    const std::string_view cmd = argv[1];
    if (cmd == "--configure")
    {
        toolchain toolchain;
        umka_cxx::umka umka{};
        auto targets = umka.run(argv[2], "configure");
        graph_result res{build_graph(targets)};
        graaf::directed_graph<target, int> graph{std::move(res.g)};
        std::unordered_map<std::string, graaf::vertex_id_t> name_to_id{std::move(res.name_to_id)};
        parse_toolchain("./tc.json", toolchain);
        write_named_module_compile_commands(
            {.graph = graph, .cxx_compiler = toolchain.cxx_compiler, .cxxflags = to_views(toolchain.cxxflags)});
        fill_module_names(graph, run_scan_deps());

        const auto order = graaf::algorithm::dfs_topological_sort(graph);
        if (!order)
        {
            return 1;
        }
        write_ninja({.graph = graph,
                     .order = *order,
                     .cxx_compiler = toolchain.cxx_compiler,
                     .c_compiler = toolchain.c_compiler,
                     .archiver = toolchain.archiver,
                     .cxxflags = to_views(toolchain.cxxflags),
                     .cflags = to_views(toolchain.cflags),
                     .exe_ldflags = to_views(toolchain.exe_ldflags),
                     .shared_ldflags = to_views(toolchain.shared_ldflags)});
        save_cache(toolchain, graph);
        write_compile_commands({.graph = graph,
                                .cxx_compiler = toolchain.cxx_compiler,
                                .c_compiler = toolchain.c_compiler,
                                .cxxflags = to_views(toolchain.cxxflags),
                                .cflags = to_views(toolchain.cflags)});
    }
    else if (cmd == "--scan")
    {
        cmd_scan(argv[2]);
    }
    else if (cmd == "--reconfigure")
    {
        if (reconfigure() != 0)
        {
            return 1;
        }
    }
    else if (cmd == "--build")
    {
        if (reconfigure() != 0)
        {
            return 1;
        }
        return std::system("ninja -f build.ninja");
    }
    return 0;
}
