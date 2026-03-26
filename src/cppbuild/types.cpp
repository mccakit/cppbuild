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
    struct source_group
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

    struct gen_group
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
                                                  .kind = std::move(out.kind)});
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
    };
} // namespace cppbuild::types
