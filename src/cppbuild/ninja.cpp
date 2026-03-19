module;
#include <cstdlib>
#include <vector>

export module cppbuild:ninja;
import std;
import fmt;
import graaf;
import :types;
import :helpers;
import :umka;
export namespace cppbuild
{
    using namespace cppbuild;
    auto write_ninja_build_rules(fmt::ostream &file,
                                 const toolchain &toolchain,
                                 const std::filesystem::path &self_path) -> void
    {
        file.print("rule scan_deps\n");
        file.print("  command = clang-scan-deps -format=p1689 -compilation-database module_commands.json | {} "
                   "--mode=scan --compdb_path=$out\n",
                   self_path.string());
        file.print("  description = SCAN\n\n");
        file.print("rule precompile\n");
        file.print("  command = {} --precompile -x c++-module -Wno-experimental-header-units $in -o $out "
                   "-fprebuilt-module-path=. $cxxflags\n",
                   toolchain.cxx_compiler);
        file.print("  description = PCM $out\n\n");
        file.print("rule compile_pcm\n");
        file.print("  command = {} -c $in -o $out -fprebuilt-module-path=. $cxxflags\n", toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");
        file.print("rule precompile_header_unit\n");
        file.print(
            "  command = {} -x c++-header -Wno-experimental-header-units -fmodule-header $in -o $out $cxxflags\n",
            toolchain.cxx_compiler);
        file.print("  description = PCM $out\n\n");
        file.print("rule compile_cxx_translation_unit\n");
        file.print("  command = {} -c $in -o $out -Wno-experimental-header-units -fprebuilt-module-path=. $cxxflags "
                   "$header_unit_deps\n",
                   toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");
        file.print("rule compile_c_translation_unit\n");
        file.print("  command = {} -c $in -o $out $cflags\n", toolchain.c_compiler);
        file.print("  description = OBJ $out\n\n");
        file.print("rule link\n");
        file.print("  command = {} $in -o $out $ldflags\n", toolchain.cxx_compiler);
        file.print("  description = LINK $out\n\n");
        file.print("build modules.dd: scan_deps module_commands.json\n\n");
        file.print("rule archive\n");
        file.print("  command = {} rcs $out $in\n", toolchain.archiver);
        file.print("  description = AR $out\n\n");
    }
    auto write_ninja_build_precompile_edges(fmt::ostream &file,
                                            const graaf::directed_graph<target, int> &graph,
                                            const std::vector<graaf::vertex_id_t> &order,
                                            const toolchain &toolchain) -> void
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
                    if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                    {
                        file.print("  cxxflags = {} {}\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
                    }
                }
            }
            else if (target.type == "header_unit")
            {
                for (const auto &src : target.srcs)
                {
                    file.print("build {}.pcm: precompile_header_unit {}\n", src.filename().string(), src.string());
                    if (!toolchain.cxxflags.empty() or !target.cxxflags.empty())
                    {
                        file.print("  cxxflags = {} {}\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
                    }
                }
            }
        }
        file.print("\n");
    }
    auto collect_deps(const graaf::directed_graph<cppbuild::target, int> &graph, graaf::vertex_id_t id)
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
    auto write_ninja_build_codegen_edges(fmt::ostream &file,
                                         const graaf::directed_graph<cppbuild::target, int> &graph,
                                         const std::vector<graaf::vertex_id_t> &order,
                                         const toolchain &toolchain) -> void
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
                        file.print("  cxxflags = {} {}\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
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
                                dep_pcms += fmt::format(" {}.pcm", dep_src.filename().string());
                                header_unit_deps += fmt::format(" -fmodule-file={}.pcm", dep_src.filename().string());
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
                        file.print("build {}.o: compile_c_translation_unit {}{}\n",
                                   src.stem().string(),
                                   src.string(),
                                   order_only);
                        if (!target.cflags.empty() or !toolchain.cflags.empty())
                        {
                            file.print(
                                "  cflags = {} {}\n", fmt::join(toolchain.cflags, " "), fmt::join(target.cflags, " "));
                        }
                    }
                    else
                    {
                        file.print("build {}.o: compile_cxx_translation_unit {}{}\n",
                                   src.stem().string(),
                                   src.string(),
                                   order_only);
                        if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
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
    auto write_ninja_build_link_edges(fmt::ostream &file,
                                      const graaf::directed_graph<cppbuild::target, int> &graph,
                                      const std::vector<graaf::vertex_id_t> &order,
                                      const toolchain &toolchain) -> void
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
                if (!target.ldflags.empty() or !toolchain.exe_ldflags.empty())
                {
                    file.print(
                        "  ldflags = {} {}\n", fmt::join(toolchain.exe_ldflags, " "), fmt::join(target.ldflags, " "));
                }
            }
            else if (target.type == "shared")
            {
                file.print("build lib{}.so: link{}\n", target.name, objs);
                if (!toolchain.shared_ldflags.empty() or !target.ldflags.empty())
                {
                    file.print("  ldflags = {} {} -shared \n",
                               fmt::join(toolchain.shared_ldflags, " "),
                               fmt::join(target.ldflags, " "));
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
            const graaf::directed_graph<cppbuild::target, int> &graph;
            const std::vector<graaf::vertex_id_t> &order;
            const toolchain &toolchain;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &self_path;
    };
    auto write_ninja_build(const write_ninja_options &opts) -> void
    {
        fmt::ostream file{fmt::output_file((opts.build_dir / "build.ninja").string())};
        write_ninja_build_rules(file, opts.toolchain, opts.self_path);
        write_ninja_build_precompile_edges(file, opts.graph, opts.order, opts.toolchain);
        write_ninja_build_codegen_edges(file, opts.graph, opts.order, opts.toolchain);
        write_ninja_build_link_edges(file, opts.graph, opts.order, opts.toolchain);
    }
    struct write_ninja_install_options
    {
        public:
            const std::vector<umka_cxx_install_target> &install_targets;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &prefix;
    };

    auto write_ninja_install(const write_ninja_install_options &opts) -> void
    {
        fmt::ostream file{fmt::output_file((opts.build_dir / "install.ninja").string())};
        file.print("rule cp\n");
        file.print("  command = cp $in $out\n");
        file.print("  description = INSTALL $out\n\n");
        for (const auto &t : opts.install_targets)
        {
            for (const auto &f : t.files)
            {
                const auto dst = std::filesystem::weakly_canonical(opts.prefix / t.install_dir / f.filename());
                file.print("build {}: cp {}\n", dst.string(), f.string());
            }
        }
    }
} // namespace cppbuild::ninja
