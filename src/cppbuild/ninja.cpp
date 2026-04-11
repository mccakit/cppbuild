module;
export module cppbuild:ninja;
import std;
import fmt;
import graaf;
import :types;
export namespace cppbuild
{
    auto get_target_with_deps(const types::build_graph &bg, graaf::vertex_id_t id)
        -> std::vector<std::reference_wrapper<const types::build_target>>
    {
        std::vector<std::reference_wrapper<const types::build_target>> result {};
        result.push_back(bg.graph.get_vertex(id));
        graaf::algorithm::breadth_first_traverse(bg.graph, id, [&](graaf::edge_id_t e) {
            auto [src, dst] = e;
            result.push_back(bg.graph.get_vertex(dst));
        });
        return result;
    }

    auto rules(fmt::ostream &file,
               const types::toolchain &toolchain,
               const std::filesystem::path &self_path,
               const std::filesystem::path &build_dir,
               const types::build_graph &bg) -> void
    {
        file.print("rule generate_p1689\n");
        file.print("  command = {} gen_p1689 --build-dir=$build_dir\n", self_path.string());
        file.print("  description = GEN P1689 DB $out\n");
        file.print("  restat = 1\n\n");

        file.print("rule scan_srcs\n");
        file.print("  command = {} scan_srcs --build-dir=$build_dir\n", self_path.string());
        file.print("  description = SCAN $out\n");
        file.print("  restat = 1\n\n");

        file.print("rule precompile_named_module\n");
        file.print("  command = {} --precompile -x c++-module $in -o $out @$rsp $cxxflags\n", toolchain.cxx_compiler);
        file.print("  dyndep = $dyndep\n");
        file.print("  description = PCM $out\n\n");

        file.print("rule compile_named_module\n");
        file.print("  command = {} -c $in -o $out @$rsp $cxxflags\n", toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");

        file.print("rule compile_cxx_translation_unit\n");
        file.print("  command = {} -c $in -o $out @$rsp $cxxflags\n", toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");

        file.print("rule compile_c_translation_unit\n");
        file.print("  command = {} -c $in -o $out $cflags\n", toolchain.c_compiler);
        file.print("  description = OBJ $out\n\n");

        file.print("rule generate\n");
        file.print("  command = $cmd\n");
        file.print("  description = GEN $out\n");
        file.print("  restat = 1\n\n");

        file.print("rule link\n");
        file.print("  command = {} @$rsp -o $out $ldflags\n", toolchain.cxx_compiler);
        file.print("  description = LINK $out\n\n");

        file.print("rule archive\n");
        file.print("  command = {} rcs $out @$rsp\n", toolchain.archiver);
        file.print("  description = AR $out\n\n");

        file.print("rule scan_single\n");
        file.print("  command = {} scan_single --scanner={} $in\n", self_path.string(), toolchain.cxx_scanner);
        file.print("  description = SCAN $in\n\n");
    }

    auto generate_edges(fmt::ostream &file, const types::build_graph &bg) -> void
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
                file.print("build {}: generate {}\n", fmt::join(all_outputs, " "), fmt::join(all_inputs, " "));
                file.print("  cmd = {}\n\n", fmt::join(gg.command, " "));
            }
        }
    }

    auto scan_edges(fmt::ostream &file, const std::filesystem::path &build_dir, const types::build_graph &bg) -> void
    {
        for (auto const &[id, bt] : bg.graph.get_vertices())
        {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    if (src.extension() == ".c")
                    {
                        continue;
                    }
                    const auto db = (build_dir / (src.filename().string() + ".p1689.json")).string();
                    const auto out = (build_dir / (src.filename().string() + ".dyndep.bin")).string();
                    file.print("build {}: scan_single {}\n\n", out, db);
                }
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.path.extension() == ".c")
                    {
                        continue;
                    }
                    const auto db = (build_dir / (go.path.filename().string() + ".p1689.json")).string();
                    const auto out = (build_dir / (go.path.filename().string() + ".dyndep.bin")).string();
                    file.print("build {}: scan_single {}\n\n", out, db);
                }
            }
        }
    }

    auto collect_gen_outputs(const types::build_graph &bg) -> std::vector<std::string>
    {
        std::vector<std::string> outputs;
        for (auto const &[id, bt] : bg.graph.get_vertices())
        {
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &out : gg.outputs)
                {
                    outputs.push_back(out.path.string());
                }
            }
        }
        return outputs;
    }

    auto collect_rsp_outputs(const types::build_graph &bg, const std::filesystem::path &build_dir)
        -> std::vector<std::string>
    {
        std::vector<std::string> rsps;
        for (auto const &[id, bt] : bg.graph.get_vertices())
        {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    rsps.push_back((build_dir / (src.filename().string() + ".rsp")).string());
                }
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.kind != "header_unit")
                    {
                        rsps.push_back((build_dir / (go.path.filename().string() + ".rsp")).string());
                    }
                }
            }
        }
        return rsps;
    }

    auto init(fmt::ostream &file, const std::filesystem::path &build_dir, const types::build_graph &bg) -> void
    {
        // generate per-source p1689 dbs
        auto p1689_outputs = std::vector<std::string> {};
        for (auto const &[id, bt] : bg.graph.get_vertices())
        {
            for (auto const &sg : bt.srcs)
            {
                for (auto const &src : sg.srcs)
                {
                    if (src.extension() == ".c")
                    {
                        continue;
                    }
                    p1689_outputs.push_back((build_dir / (src.filename().string() + ".p1689.json")).string());
                }
            }
            for (auto const &gg : bt.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.path.extension() == ".c")
                    {
                        continue;
                    }
                    p1689_outputs.push_back((build_dir / (go.path.filename().string() + ".p1689.json")).string());
                }
            }
        }

        file.print("build {}: generate_p1689\n", fmt::join(p1689_outputs, " "));
        file.print("  build_dir = {}\n\n", build_dir.string());

        // scan each source individually
        auto scan_outputs = std::vector<std::string> {};
        for (const auto &db : p1689_outputs)
        {
            auto db_path = std::filesystem::path {db};
            const auto out = (db_path.parent_path() / (db_path.stem().stem().string() + ".dyndep.bin")).string();
            file.print("build {}: scan_single {}\n\n", out, db);
            scan_outputs.push_back(out);
        }

        // scan_srcs reads all .dyndep.bin files and produces deps.dd + rsps
        const auto gen_outputs = collect_gen_outputs(bg);
        const auto rsp_outputs = collect_rsp_outputs(bg, build_dir);

        std::string implicit_outs;
        if (!rsp_outputs.empty())
        {
            implicit_outs = fmt::format(" | {}", fmt::join(rsp_outputs, " "));
        }

        auto all_deps = std::vector<std::string> {};
        all_deps.insert(all_deps.end(), scan_outputs.begin(), scan_outputs.end());
        all_deps.insert(all_deps.end(), gen_outputs.begin(), gen_outputs.end());

        std::string implicit_deps;
        if (!all_deps.empty())
        {
            implicit_deps = fmt::format(" | {}", fmt::join(all_deps, " "));
        }

        file.print("build {}/deps.dd{}: scan_srcs{}\n", build_dir.string(), implicit_outs, implicit_deps);
        file.print("  build_dir = {}\n\n", build_dir.string());
    }

    auto precompile_named_module_edges(fmt::ostream &file,
                                       const types::build_graph &bg,
                                       const std::filesystem::path &build_dir,
                                       const types::toolchain &toolchain) -> void
    {
        const auto dyndep = (build_dir / "deps.dd").string();
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            const auto order_only = " || " + dyndep;

            auto write_edge = [&](const std::filesystem::path &src) {
                const auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                const auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                file.print("build {}: precompile_named_module {}{}\n", pcm, src.string(), order_only);
                file.print("  dyndep = {}\n", dyndep);
                file.print("  rsp = {}\n", rsp);
                file.print("  cxxflags = {} {} {}\n",
                           fmt::join(toolchain.cxxflags, " "),
                           fmt::join(target.cxxflags.public_, " "),
                           fmt::join(target.cxxflags.private_, " "));
                file.print("\n");
            };

            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (auto const &src : sg.srcs)
                    {
                        write_edge(src);
                    }
                }
            }

            for (auto const &gg : target.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.kind == "named_module")
                    {
                        write_edge(go.path);
                    }
                }
            }
        }
    }

    auto codegen_edges(fmt::ostream &file,
                       const types::build_graph &bg,
                       const types::toolchain &toolchain,
                       const std::filesystem::path &build_dir) -> void
    {
        const auto dyndep = (build_dir / "deps.dd").string();
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            const auto order_only = " || " + dyndep;

            auto write_named_module = [&](const std::filesystem::path &src) {
                auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                auto obj = (build_dir / (src.filename().string() + ".pcm.o")).string();
                auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                file.print("build {}: compile_named_module {}\n", obj, pcm);
                file.print("  rsp = {}\n", rsp);
                file.print("  cxxflags = {} {} {}\n\n",
                           fmt::join(toolchain.cxxflags, " "),
                           fmt::join(target.cxxflags.public_, " "),
                           fmt::join(target.cxxflags.private_, " "));
            };

            auto write_translation_unit = [&](const std::filesystem::path &src) {
                const bool is_c = src.extension() == ".c";
                auto obj = (build_dir / (src.filename().string() + ".o")).string();
                auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                if (is_c)
                {
                    file.print("build {}: compile_c_translation_unit {}\n", obj, src.string());
                    file.print("  cflags = {} {} {}\n\n",
                               fmt::join(toolchain.cflags, " "),
                               fmt::join(target.cflags.public_, " "),
                               fmt::join(target.cflags.private_, " "));
                }
                else
                {
                    file.print("build {}: compile_cxx_translation_unit {}{}\n", obj, src.string(), order_only);
                    file.print("  dyndep = {}\n", dyndep);
                    file.print("  rsp = {}\n", rsp);
                    file.print("  cxxflags = {} {} {}\n",
                               fmt::join(toolchain.cxxflags, " "),
                               fmt::join(target.cxxflags.public_, " "),
                               fmt::join(target.cxxflags.private_, " "));
                    file.print("\n");
                }
            };

            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (auto const &src : sg.srcs)
                    {
                        write_named_module(src);
                    }
                }
                else if (sg.kind == "translation_unit")
                {
                    for (auto const &src : sg.srcs)
                    {
                        write_translation_unit(src);
                    }
                }
            }

            for (auto const &gg : target.gen_groups)
            {
                for (auto const &go : gg.outputs)
                {
                    if (go.kind == "named_module")
                    {
                        write_named_module(go.path);
                    }
                    else if (go.kind == "translation_unit")
                    {
                        write_translation_unit(go.path);
                    }
                }
            }
        }
        file.print("\n");
    }

    auto link_edges(fmt::ostream &file,
                    const types::build_graph &bg,
                    const types::toolchain &toolchain,
                    const std::filesystem::path &build_dir) -> void
    {
        for (const auto &lt : bg.link_targets)
        {
            std::vector<std::string> objs;
            for (const auto &dep_name : lt.deps)
            {
                auto it = bg.name_to_id.find(dep_name);
                if (it == bg.name_to_id.end())
                    continue;
                for (const auto &dep_target : get_target_with_deps(bg, it->second))
                {
                    const auto &dep = dep_target.get();
                    for (const auto &sg : dep.srcs)
                    {
                        if (sg.kind == "translation_unit")
                            for (const auto &src : sg.srcs)
                                objs.push_back((build_dir / (src.filename().string() + ".o")).string());
                        else if (sg.kind == "named_module")
                            for (const auto &src : sg.srcs)
                                objs.push_back((build_dir / (src.filename().string() + ".pcm.o")).string());
                    }
                    for (const auto &gg : dep.gen_groups)
                        for (const auto &go : gg.outputs)
                        {
                            if (go.kind == "translation_unit")
                                objs.push_back((build_dir / (go.path.filename().string() + ".o")).string());
                            else if (go.kind == "named_module")
                                objs.push_back((build_dir / (go.path.filename().string() + ".pcm.o")).string());
                        }
                }
            }

            auto write_rsp = [&](const std::string &rsp_path) {
                auto rsp = fmt::output_file(rsp_path);
                for (const auto &obj : objs)
                {
                    rsp.print("{}\n", obj);
                }
            };

            auto objs_implicit = fmt::format(" | {}", fmt::join(objs, " "));

            if (lt.kind == "executable")
            {
                auto out_path = (build_dir / lt.name).string();
                auto rsp_path = (build_dir / (lt.name + ".link.rsp")).string();
                write_rsp(rsp_path);
                file.print("build {}: link{}\n", out_path, objs_implicit);
                file.print("  rsp = {}\n", rsp_path);
                file.print("  ldflags = {} {}\n", fmt::join(toolchain.exe_ldflags, " "), fmt::join(lt.ldflags, " "));
                file.print("\n");
            }
            else if (lt.kind == "shared_library")
            {
                auto out_path = (build_dir / ("lib" + lt.name + ".so")).string();
                auto rsp_path = (build_dir / ("lib" + lt.name + ".so.link.rsp")).string();
                write_rsp(rsp_path);
                file.print("build {}: link{}\n", out_path, objs_implicit);
                file.print("  rsp = {}\n", rsp_path);
                file.print("  ldflags = {} {} -shared\n",
                           fmt::join(toolchain.shared_ldflags, " "),
                           fmt::join(lt.ldflags, " "));
                file.print("\n");
            }
            else if (lt.kind == "static_library")
            {
                auto out_path = (build_dir / ("lib" + lt.name + ".a")).string();
                auto rsp_path = (build_dir / ("lib" + lt.name + ".a.link.rsp")).string();
                write_rsp(rsp_path);
                file.print("build {}: archive{}\n", out_path, objs_implicit);
                file.print("  rsp = {}\n", rsp_path);
                file.print("\n");
            }
        }
    }

    auto install_edges(fmt::ostream &file,
                       const types::build_graph &bg,
                       const std::filesystem::path &build_dir,
                       const std::filesystem::path &prefix) -> void
    {
        std::vector<std::string> all_installed;
        for (const auto &it : bg.install_targets)
        {
            const auto dst_base = prefix / it.install_dir;
            for (const auto &f : it.files)
            {
                auto dst = dst_base / std::filesystem::path(f).filename();
                file.print("build {}: install_file {}\n\n", dst.string(), f);
                all_installed.push_back(dst.string());
            }
            for (const auto &lt_name : it.link_targets)
            {
                auto lt_it = std::find_if(
                    bg.link_targets.begin(), bg.link_targets.end(), [&](const auto &lt) { return lt.name == lt_name; });
                if (lt_it == bg.link_targets.end())
                {
                    continue;
                }
                std::filesystem::path src;
                if (lt_it->kind == "executable")
                {
                    src = build_dir / lt_it->name;
                }
                else if (lt_it->kind == "shared_library")
                {
                    src = build_dir / ("lib" + lt_it->name + ".so");
                }
                else if (lt_it->kind == "static_library")
                {
                    src = build_dir / ("lib" + lt_it->name + ".a");
                }
                auto dst = dst_base / src.filename();
                file.print("build {}: install_file {}\n\n", dst.string(), src.string());
                all_installed.push_back(dst.string());
            }
            for (const auto &bt_name : it.build_targets)
            {
                auto bt_it = bg.name_to_id.find(bt_name);
                if (bt_it == bg.name_to_id.end())
                {
                    continue;
                }
                const auto &bt = bg.graph.get_vertex(bt_it->second);
                for (const auto &sg : bt.srcs)
                {
                    if (sg.kind == "named_module")
                    {
                        for (const auto &src : sg.srcs)
                        {
                            auto pcm = build_dir / (src.filename().string() + ".pcm");
                            auto dst = dst_base / pcm.filename();
                            file.print("build {}: install_file {}\n\n", dst.string(), pcm.string());
                            all_installed.push_back(dst.string());
                        }
                    }
                }
                for (const auto &gg : bt.gen_groups)
                {
                    for (const auto &go : gg.outputs)
                    {
                        if (go.kind == "named_module")
                        {
                            auto pcm = build_dir / (go.path.filename().string() + ".pcm");
                            auto dst = dst_base / pcm.filename();
                            file.print("build {}: install_file {}\n\n", dst.string(), pcm.string());
                            all_installed.push_back(dst.string());
                        }
                    }
                }
            }
        }
        if (!all_installed.empty())
        {
            file.print("build install: phony {}\n\n", fmt::join(all_installed, " "));
        }
    }

    struct write_ninja_build_options
    {
        public:
            const types::build_graph &graph;
            const types::toolchain &toolchain;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &self_path;
    };

    auto write_ninja_build(const write_ninja_build_options &opts) -> void
    {
        auto file = fmt::output_file((opts.build_dir / "build.ninja").string());
        rules(file, opts.toolchain, opts.self_path, opts.build_dir, opts.graph);
        generate_edges(file, opts.graph);
        init(file, opts.build_dir, opts.graph);
        precompile_named_module_edges(file, opts.graph, opts.build_dir, opts.toolchain);
        codegen_edges(file, opts.graph, opts.toolchain, opts.build_dir);
        link_edges(file, opts.graph, opts.toolchain, opts.build_dir);
    }

    struct write_ninja_install_options
    {
        public:
            const types::build_graph &graph;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &prefix;
    };

    auto write_ninja_install(const write_ninja_install_options &opts) -> void
    {
        auto file = fmt::output_file((opts.build_dir / "install.ninja").string());
        file.print("rule install_file\n");
        file.print("  command = install -D $in $out\n");
        file.print("  description = INSTALL $out\n\n");
        install_edges(file, opts.graph, opts.build_dir, opts.prefix);
    }
} // namespace cppbuild
