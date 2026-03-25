module;
#include <new>
export module cppbuild:ninja;
import std;
import fmt;
import graaf;
import :types;
export namespace cppbuild
{
    auto write_ninja_build_rules(fmt::ostream &file,
                                 const types::toolchain &toolchain,
                                 const std::filesystem::path &self_path,
                                 const std::filesystem::path &build_dir,
                                 const types::build_graph &bg) -> void
    {
        file.print("rule scan_deps\n");
        file.print("  command = {} scan --build-dir=$build_dir\n", self_path.string());
        file.print("  description = SCAN $out\n");
        file.print("  restat = 1\n\n");
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
        file.print("rule archive\n");
        file.print("  command = {} rcs $out $in\n", toolchain.archiver);
        file.print("  description = AR $out\n\n");
        file.print("rule generate\n");
        file.print("  command = $cmd\n");
        file.print("  description = GEN $out\n\n");

        std::vector<std::string> gen_outputs;
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            for (const auto &gg : bt.gen_groups)
            {
                for (const auto &out : gg.outputs)
                {
                    gen_outputs.push_back(out.path.string());
                }
            }
        }

        if (gen_outputs.empty())
        {
            file.print("build modules.dd: scan_deps module_commands.json\n");
            file.print("  build_dir = {}\n\n", build_dir.string());
        }
        else
        {
            file.print("build modules.dd: scan_deps module_commands.json || {}\n", fmt::join(gen_outputs, " "));
            file.print("  build_dir = {}\n\n", build_dir.string());
        }
    }

    auto expand_token(const std::string &token,
                      const std::vector<std::string> &inputs,
                      const std::vector<std::string> &outputs) -> std::vector<std::string>
    {
        if (token == "{inputs}")
            return inputs;
        if (token == "{outputs}")
            return outputs;

        auto inner = std::string_view(token).substr(1, token.size() - 2);

        if (inner.starts_with("input_"))
        {
            auto idx = std::stoul(std::string(inner.substr(6)));
            return {inputs[idx]};
        }
        if (inner.starts_with("output_"))
        {
            auto idx = std::stoul(std::string(inner.substr(7)));
            return {outputs[idx]};
        }

        return {token};
    }

    auto write_ninja_build_generate_edges(fmt::ostream &file, const types::build_graph &bg) -> void
    {
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            for (const auto &gg : target.gen_groups)
            {
                std::vector<std::string> all_inputs;
                for (const auto &inp : gg.inputs)
                {
                    all_inputs.push_back(inp.string());
                }

                std::vector<std::string> all_outputs;
                for (const auto &out : gg.outputs)
                {
                    all_outputs.push_back(out.path.string());
                }

                std::vector<std::string> expanded;
                for (const auto &token : gg.command)
                {
                    for (const auto &s : expand_token(token, all_inputs, all_outputs))
                    {
                        expanded.push_back(s);
                    }
                }

                file.print("build {}: generate {}\n", fmt::join(all_outputs, " "), fmt::join(all_inputs, " "));
                file.print("  cmd = {}\n\n", fmt::join(expanded, " "));
            }
        }
    }

    auto write_ninja_build_precompile_edges(fmt::ostream &file,
                                            const types::build_graph &bg,
                                            const types::toolchain &toolchain) -> void
    {
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            for (const auto &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (const auto &src : sg.srcs)
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
                else if (sg.kind == "header_unit")
                {
                    for (const auto &src : sg.srcs)
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

            for (const auto &gg : target.gen_groups)
            {
                for (const auto &out : gg.outputs)
                {
                    if (out.kind == "named_module")
                    {
                        file.print("build {}.pcm: precompile {} || modules.dd\n", out.module_name, out.path.string());
                        file.print("  dyndep = modules.dd\n");
                        if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
                        }
                    }
                    else if (out.kind == "header_unit")
                    {
                        file.print("build {}.pcm: precompile_header_unit {}\n",
                                   out.path.filename().string(),
                                   out.path.string());
                        if (!toolchain.cxxflags.empty() or !target.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
                        }
                    }
                }
            }
        }
        file.print("\n");
    }

    auto collect_deps(const types::build_graph &bg, graaf::vertex_id_t id) -> std::vector<graaf::vertex_id_t>
    {
        std::vector<graaf::vertex_id_t> deps;
        graaf::algorithm::breadth_first_traverse(bg.graph, id, [&](const auto &edge) { deps.push_back(edge.second); });
        return deps;
    }
    auto write_ninja_build_codegen_edges(fmt::ostream &file,
                                         const types::build_graph &bg,
                                         const types::toolchain &toolchain) -> void
    {
        std::string header_unit_dependency_flags {};
        header_unit_dependency_flags.reserve(1024);
        std::string header_unit_output_files {};
        header_unit_output_files.reserve(1024);
        std::string named_module_output_files {};
        named_module_output_files.reserve(1024);
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            const auto deps = collect_deps(bg, id);

            header_unit_dependency_flags.clear();
            header_unit_output_files.clear();
            named_module_output_files.clear();

            for (const auto &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (const auto &src : sg.srcs)
                    {
                        const auto it = target.src_to_mname.find(src.string());
                        if (it != target.src_to_mname.end())
                        {
                            named_module_output_files += fmt::format(" {}.pcm", it->second);
                        }
                    }
                }
                if (sg.kind == "header_unit")
                {
                    for (const auto &src : sg.srcs)
                    {
                        header_unit_dependency_flags += fmt::format(" -fmodule-file={}.pcm", src.filename().string());
                        header_unit_output_files += fmt::format(" {}.pcm", src.filename().string());
                    }
                }
            }

            for (const auto &gg : target.gen_groups)
            {
                for (const auto &out : gg.outputs)
                {
                    if (out.kind == "named_module")
                    {
                        named_module_output_files += fmt::format(" {}.pcm", out.module_name);
                    }
                    else if (out.kind == "header_unit")
                    {
                        header_unit_dependency_flags +=
                            fmt::format(" -fmodule-file={}.pcm", out.path.filename().string());
                        header_unit_output_files += fmt::format(" {}.pcm", out.path.filename().string());
                    }
                }
            }

            for (const auto &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (const auto &src : sg.srcs)
                    {
                        const auto &mname = target.src_to_mname.at(src.string());
                        const auto order_only = header_unit_output_files.empty() ? "" : " |" + header_unit_output_files;
                        file.print("build {}.o: compile_pcm {}.pcm{}\n", mname, mname, order_only);
                        if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
                        }
                        if (!header_unit_dependency_flags.empty())
                        {
                            file.print("  header_unit_deps ={}\n", header_unit_dependency_flags);
                        }
                    }
                }
                else if (sg.kind == "translation_unit")
                {
                    for (const auto &src : sg.srcs)
                    {
                        const bool is_c = src.extension() == ".c";
                        if (is_c)
                        {
                            file.print(
                                "build {}.o: compile_c_translation_unit {}\n", src.stem().string(), src.string());
                            if (!target.cflags.empty() or !toolchain.cflags.empty())
                            {
                                file.print("  cflags = {} {}\n",
                                           fmt::join(toolchain.cflags, " "),
                                           fmt::join(target.cflags, " "));
                            }
                        }
                        else
                        {
                            std::string order_only = header_unit_output_files + named_module_output_files;
                            const auto order_only_str = order_only.empty() ? "" : " |" + order_only;
                            file.print("build {}.o: compile_cxx_translation_unit {}{}\n",
                                       src.stem().string(),
                                       src.string(),
                                       order_only_str);
                            if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                            {
                                file.print("  cxxflags = {} {}\n",
                                           fmt::join(toolchain.cxxflags, " "),
                                           fmt::join(target.cxxflags, " "));
                            }
                            if (!header_unit_dependency_flags.empty())
                            {
                                file.print("  header_unit_deps ={}\n", header_unit_dependency_flags);
                            }
                        }
                    }
                }
            }

            for (const auto &gg : target.gen_groups)
            {
                for (const auto &out : gg.outputs)
                {
                    if (out.kind == "named_module")
                    {
                        const auto order_only = header_unit_output_files.empty() ? "" : " |" + header_unit_output_files;
                        file.print("build {}.o: compile_pcm {}.pcm{}\n", out.module_name, out.module_name, order_only);
                        if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
                        }
                        if (!header_unit_dependency_flags.empty())
                        {
                            file.print("  header_unit_deps ={}\n", header_unit_dependency_flags);
                        }
                    }
                    else if (out.kind == "translation_unit")
                    {
                        std::string order_only = header_unit_output_files + named_module_output_files;
                        const auto order_only_str = order_only.empty() ? "" : " |" + order_only;
                        file.print("build {}.o: compile_cxx_translation_unit {}{}\n",
                                   out.path.stem().string(),
                                   out.path.string(),
                                   order_only_str);
                        if (!target.cxxflags.empty() or !toolchain.cxxflags.empty())
                        {
                            file.print("  cxxflags = {} {}\n",
                                       fmt::join(toolchain.cxxflags, " "),
                                       fmt::join(target.cxxflags, " "));
                        }
                        if (!header_unit_dependency_flags.empty())
                        {
                            file.print("  header_unit_deps ={}\n", header_unit_dependency_flags);
                        }
                    }
                }
            }
        }
        file.print("\n");
    }

    auto write_ninja_build_link_edges(fmt::ostream &file,
                                      const types::build_graph &bg,
                                      const types::toolchain &toolchain) -> void
    {
        for (const auto &lt : bg.link_targets)
        {
            std::string objs;
            for (const auto &dep_name : lt.deps)
            {
                auto it = bg.name_to_id.find(dep_name);
                if (it == bg.name_to_id.end())
                {
                    continue;
                }
                const auto &dep = bg.graph.get_vertex(it->second);
                for (const auto &sg : dep.srcs)
                {
                    if (sg.kind == "translation_unit")
                    {
                        for (const auto &src : sg.srcs)
                        {
                            objs += fmt::format(" {}.o", src.stem().string());
                        }
                    }
                    else if (sg.kind == "named_module")
                    {
                        for (const auto &[s, mname] : dep.src_to_mname)
                        {
                            objs += fmt::format(" {}.o", mname);
                        }
                    }
                }
                for (const auto &gg : dep.gen_groups)
                {
                    for (const auto &out : gg.outputs)
                    {
                        if (out.kind == "translation_unit")
                        {
                            objs += fmt::format(" {}.o", out.path.stem().string());
                        }
                        else if (out.kind == "named_module")
                        {
                            objs += fmt::format(" {}.o", out.module_name);
                        }
                    }
                }
            }
            if (lt.kind == "exe")
            {
                file.print("build {}.elf: link{}\n", lt.name, objs);
                if (!lt.ldflags.empty() or !toolchain.exe_ldflags.empty())
                {
                    file.print(
                        "  ldflags = {} {}\n", fmt::join(toolchain.exe_ldflags, " "), fmt::join(lt.ldflags, " "));
                }
            }
            else if (lt.kind == "shared")
            {
                file.print("build lib{}.so: link{}\n", lt.name, objs);
                if (!lt.ldflags.empty() or !toolchain.shared_ldflags.empty())
                {
                    file.print("  ldflags = {} {} -shared\n",
                               fmt::join(toolchain.shared_ldflags, " "),
                               fmt::join(lt.ldflags, " "));
                }
            }
            else if (lt.kind == "static")
            {
                file.print("build lib{}.a: archive{}\n", lt.name, objs);
            }
        }
    }

    struct write_ninja_options
    {
        public:
            const types::build_graph &graph;
            const types::toolchain &toolchain;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &self_path;
    };
    auto write_ninja_build(const write_ninja_options &opts) -> void
    {
        fmt::ostream file {fmt::output_file((opts.build_dir / "build.ninja").string())};
        write_ninja_build_rules(file, opts.toolchain, opts.self_path, opts.build_dir, opts.graph);
        write_ninja_build_generate_edges(file, opts.graph);
        write_ninja_build_precompile_edges(file, opts.graph, opts.toolchain);
        write_ninja_build_codegen_edges(file, opts.graph, opts.toolchain);
        write_ninja_build_link_edges(file, opts.graph, opts.toolchain);
    }
} // namespace cppbuild
