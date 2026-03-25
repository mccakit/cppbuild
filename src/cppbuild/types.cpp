module;
#include <simdjson.h>
#include <subprocess.h>
export module cppbuild:types;
import std;
import fmt;
import graaf;
import :modules_cppbuild;
export namespace cppbuild::types
{
    class source_group
    {
        public:
            std::string kind {};
            std::vector<std::filesystem::path> srcs;
    };
    struct gen_output
    {
        public:
            std::filesystem::path path {};
            std::string kind {};
            std::string module_name {};
    };

    class gen_group
    {
        public:
            std::vector<std::string> command {};
            std::vector<std::filesystem::path> inputs {};
            std::vector<gen_output> outputs {};
    };
    struct build_target
    {
        public:
            std::string name {};
            std::vector<source_group> srcs {};
            std::vector<gen_group> gen_groups {};
            std::vector<std::string> deps {};
            std::vector<std::string> cxxflags {};
            std::vector<std::string> cflags {};
            std::map<std::string, std::string> src_to_mname {};
    };
    struct link_target
    {
        public:
            std::string name {};
            std::string kind {};
            std::vector<std::string> deps {};
            std::vector<std::string> ldflags {};
    };
    struct install_target
    {
        public:
            std::string name {};
            std::filesystem::path install_dir {};
            std::vector<std::string> files {};
    };
    auto parse_flags(simdjson::dom::element doc, std::string_view key) -> std::vector<std::string>
    {
        simdjson::dom::array arr;
        std::vector<std::string> out;
        if (doc[key].get(arr) == simdjson::SUCCESS)
        {
            for (auto f : arr)
            {
                out.push_back(std::string(f.get_string().value()));
            }
        }
        return out;
    }
    class toolchain
    {
        public:
            std::string cxx_compiler {};
            std::string c_compiler {};
            std::string archiver {};
            std::string cxx_scanner {};
            std::vector<std::string> cxxflags {};
            std::vector<std::string> cflags {};
            std::vector<std::string> exe_ldflags {};
            std::vector<std::string> shared_ldflags {};
            auto parse(const std::filesystem::path &path) -> void
            {
                simdjson::dom::parser parser;
                simdjson::dom::element doc = parser.load(path.string());
                if (auto val = doc["cxx_compiler"]; val.error() == simdjson::SUCCESS)
                {
                    cxx_compiler = std::string(val.get_string().value());
                }
                if (auto val = doc["c_compiler"]; val.error() == simdjson::SUCCESS)
                {
                    c_compiler = std::string(val.get_string().value());
                }
                if (auto val = doc["archiver"]; val.error() == simdjson::SUCCESS)
                {
                    archiver = std::string(val.get_string().value());
                }
                if (auto val = doc["cxx_scanner"]; val.error() == simdjson::SUCCESS)
                {
                    cxx_scanner = std::string(val.get_string().value());
                }
                cxxflags = parse_flags(doc, "cxxflags");
                cflags = parse_flags(doc, "cflags");
                exe_ldflags = parse_flags(doc, "exe_ldflags");
                shared_ldflags = parse_flags(doc, "shared_ldflags");
            }
            auto print() const -> void
            {
                fmt::println("cxx_compiler:   {}", cxx_compiler);
                fmt::println("c_compiler:     {}", c_compiler);
                fmt::println("archiver:       {}", archiver);
                fmt::println("cxx_scanner:    {}", cxx_scanner);
                fmt::println("cxxflags:       {}", fmt::join(cxxflags, " "));
                fmt::println("cflags:         {}", fmt::join(cflags, " "));
                fmt::println("exe_ldflags:    {}", fmt::join(exe_ldflags, " "));
                fmt::println("shared_ldflags: {}", fmt::join(shared_ldflags, " "));
            }
    };
    class build_graph
    {
        public:
            graaf::directed_graph<build_target, int> graph {};
            std::unordered_map<std::string, graaf::vertex_id_t> name_to_id {};
            std::vector<graaf::vertex_id_t> topo_order {};
            std::vector<link_target> link_targets {};
            std::vector<install_target> install_targets {};
            auto parse(modules_cppbuild::results_cxx results,
                       const std::filesystem::path &script_path,
                       const std::filesystem::path &build_dir) -> void
            {
                std::string target_name {};
                target_name.reserve(256);
                const std::filesystem::path src_dir = script_path.parent_path();
                name_to_id.reserve(results.build_targets.size());
                for (auto &bt_raw : results.build_targets)
                {
                    types::build_target target {.name = std::move(bt_raw.name),
                                                .deps = std::move(bt_raw.deps),
                                                .cxxflags = std::move(bt_raw.cxxflags),
                                                .cflags = std::move(bt_raw.cflags)};
                    target.srcs.reserve(bt_raw.srcs.size());
                    for (auto &sg_raw : bt_raw.srcs)
                    {
                        for (auto &src : sg_raw.srcs)
                        {
                            src = std::filesystem::weakly_canonical(src_dir / src);
                        }
                        target.srcs.push_back(
                            types::source_group {.kind = std::move(sg_raw.kind), .srcs = std::move(sg_raw.srcs)});
                    }

                    target.gen_groups.reserve(bt_raw.gen_groups.size());
                    for (auto &gg_raw : bt_raw.gen_groups)
                    {
                        types::gen_group gg;
                        gg.command = std::move(gg_raw.command);
                        gg.inputs.reserve(gg_raw.inputs.size());
                        for (auto &inp : gg_raw.inputs)
                        {
                            gg.inputs.push_back(std::filesystem::weakly_canonical(src_dir / inp));
                        }
                        gg.outputs.reserve(gg_raw.outputs.size());
                        for (auto &out : gg_raw.outputs)
                        {
                            gg.outputs.push_back({.path = build_dir / out.path,
                                                  .kind = std::move(out.kind),
                                                  .module_name = std::move(out.module_name)});
                        }
                        target.gen_groups.push_back(std::move(gg));
                    }

                    target_name = target.name;
                    name_to_id[target_name] = graph.add_vertex(std::move(target));
                    target_name.clear();
                }
                for (const auto &[name, id] : name_to_id)
                {
                    const auto &vertex_data = graph.get_vertex(id);
                    for (const auto &dep_name : vertex_data.deps)
                    {
                        if (auto it = name_to_id.find(dep_name); it != name_to_id.end())
                        {
                            graph.add_edge(id, it->second, 1);
                        }
                    }
                }

                link_targets.reserve(results.link_targets.size());
                for (auto &lt_raw : results.link_targets)
                {
                    link_targets.push_back({.name = std::move(lt_raw.name),
                                            .kind = std::move(lt_raw.kind),
                                            .deps = std::move(lt_raw.deps),
                                            .ldflags = std::move(lt_raw.ldflags)});
                }

                install_targets.reserve(results.install_targets.size());
                for (auto &it_raw : results.install_targets)
                {
                    std::vector<std::string> files;
                    files.reserve(it_raw.files.size());
                    for (auto &f : it_raw.files)
                    {
                        files.push_back(std::filesystem::weakly_canonical(src_dir / f).string());
                    }
                    install_targets.push_back({.name = std::move(it_raw.name),
                                               .install_dir = std::move(it_raw.install_dir),
                                               .files = std::move(files)});
                }
            }
            auto scan(const std::filesystem::path &build_dir, const toolchain &toolchain) -> void
            {
                const auto module_commands = (build_dir / "module_commands.json").string();
                const auto scanner = std::string {toolchain.cxx_scanner};
                const char *command_line[] = {
                    scanner.c_str(), "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
                struct subprocess_s subprocess;
                int options = subprocess_option_search_user_path | subprocess_option_inherit_environment |
                              subprocess_option_enable_async;
                if (subprocess_create(command_line, options, &subprocess) != 0)
                {
                    return;
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
                simdjson::dom::parser parser {};
                auto doc = parser.parse(scan_output);
                for (auto rule : doc["rules"])
                {
                    simdjson::dom::array provides {};
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
                            for (const auto &sg : t.srcs)
                            {
                                for (const auto &tsrc : sg.srcs)
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
            }
            auto order() -> bool
            {
                auto result = graaf::algorithm::dfs_topological_sort(graph);
                if (!result)
                {
                    fmt::print(stderr, "Cycle detected in build graph\n");
                    std::terminate();
                }
                topo_order = std::move(result.value());
                return true;
            }
            auto print_input() const -> void
            {
                fmt::println("=== Build Targets ({}) ===", graph.vertex_count());
                for (const auto &[id, bt] : graph.get_vertices())
                {
                    fmt::println("  name: {}", bt.name);
                    fmt::println("  deps: [{}]", fmt::join(bt.deps, ", "));
                    fmt::println("  cxxflags: [{}]", fmt::join(bt.cxxflags, ", "));
                    fmt::println("  cflags: [{}]", fmt::join(bt.cflags, ", "));
                    fmt::println("  srcs ({}):", bt.srcs.size());
                    for (const auto &sg : bt.srcs)
                    {
                        fmt::println("    kind: {}", sg.kind);
                        for (const auto &src : sg.srcs)
                        {
                            fmt::println("      - {}", src.string());
                        }
                    }
                    fmt::println("  gen_groups ({}):", bt.gen_groups.size());
                    for (const auto &gg : bt.gen_groups)
                    {
                        fmt::println("    command: [{}]", fmt::join(gg.command, " "));
                        fmt::println("    inputs ({}):", gg.inputs.size());
                        for (const auto &inp : gg.inputs)
                        {
                            fmt::println("      - {}", inp.string());
                        }
                        fmt::println("    outputs ({}):", gg.outputs.size());
                        for (const auto &out : gg.outputs)
                        {
                            fmt::println(
                                "      - path={} kind={} module_name={}", out.path.string(), out.kind, out.module_name);
                        }
                    }
                }
                fmt::println("=== Link Targets ({}) ===", link_targets.size());
                for (const auto &lt : link_targets)
                {
                    fmt::println("  name={} kind={} deps=[{}] ldflags=[{}]",
                                 lt.name,
                                 lt.kind,
                                 fmt::join(lt.deps, ", "),
                                 fmt::join(lt.ldflags, ", "));
                }
                fmt::println("=== Install Targets ({}) ===", install_targets.size());
                for (const auto &it : install_targets)
                {
                    fmt::println("  name={} install_dir={} files=[{}]",
                                 it.name,
                                 it.install_dir.string(),
                                 fmt::join(it.files, ", "));
                }
            }
            auto print_modules() const -> void
            {
                for (const auto &[id, bt] : graph.get_vertices())
                {
                    fmt::println("  {} src_to_mname ({}):", bt.name, bt.src_to_mname.size());
                    for (const auto &[src, mname] : bt.src_to_mname)
                    {
                        fmt::println("    {} -> {}", src, mname);
                    }
                }
            }
    };
} // namespace cppbuild::types
