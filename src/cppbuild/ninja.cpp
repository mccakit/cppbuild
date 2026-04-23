module;
export module cppbuild:ninja;
import std;
import fmt;
import graaf;
import :types;
import :helpers;
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

    auto get_all_cxx_sources(const types::build_graph &bg) -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> srcs;
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
                    srcs.push_back(src);
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
                    srcs.push_back(go.path);
                }
            }
        }
        return srcs;
    }

    auto rules(fmt::ostream &file,
               const types::toolchain &toolchain,
               const std::filesystem::path &self_path,
               const std::filesystem::path &build_dir,
               const types::build_graph &bg) -> void
    {
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
        file.print("  command = {} scan_single --build-dir={} $in\n", self_path.string(), build_dir.string());
        file.print("  description = SCAN $in\n\n");

        file.print("rule gen_single_dd\n");
        file.print("  command = {} gen_single_dd --build-dir={} $in\n", self_path.string(), build_dir.string());
        file.print("  description = DD $in\n\n");

        file.print("rule gen_single_rsp\n");
        file.print("  command = {} gen_single_rsp --build-dir={} $in\n", self_path.string(), build_dir.string());
        file.print("  description = RSP $in\n\n");
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

    auto scan_edges(fmt::ostream &file,
                    const std::filesystem::path &src_root,
                    const std::filesystem::path &build_dir,
                    const std::vector<std::filesystem::path> &srcs) -> void
    {
        for (auto const &src : srcs)
        {
            auto base = calc_output_path(src, src_root, build_dir);
            const auto db = base.string() + ".p1689.json";
            const auto out = base.string() + ".scan.stamp";
            file.print("build {}: scan_single {} | {}\n\n", out, db, src.string());
        }
    }

    auto scan_phase(fmt::ostream &file,
                    const std::filesystem::path &src_root,
                    const std::filesystem::path &build_dir,
                    const std::vector<std::filesystem::path> &srcs) -> void
    {
        if (srcs.empty())
        {
            return;
        }
        std::vector<std::string> stamps;
        stamps.reserve(srcs.size());
        for (auto const &src : srcs)
        {
            auto base = calc_output_path(src, src_root, build_dir);
            stamps.push_back(base.string() + ".scan.stamp");
        }
        file.print("build scan_phase: phony {}\n\n", fmt::join(stamps, " "));
    }

    auto dd_edges(fmt::ostream &file,
                  const std::filesystem::path &src_root,
                  const std::filesystem::path &build_dir,
                  const std::vector<std::filesystem::path> &srcs) -> void
    {
        for (auto const &src : srcs)
        {
            auto base = calc_output_path(src, src_root, build_dir);
            const auto dd = base.string() + ".dd";
            const auto stamp = base.string() + ".scan.stamp";
            file.print("build {}: gen_single_dd {} | {} || scan_phase\n\n", dd, src.string(), stamp);
        }
    }

    auto rsp_edges(fmt::ostream &file,
                   const std::filesystem::path &src_root,
                   const std::filesystem::path &build_dir,
                   const std::vector<std::filesystem::path> &srcs) -> void
    {
        for (auto const &src : srcs)
        {
            auto base = calc_output_path(src, src_root, build_dir);
            const auto rsp = base.string() + ".rsp";
            const auto stamp = base.string() + ".scan.stamp";
            file.print("build {}: gen_single_rsp {} | {} || scan_phase\n\n", rsp, src.string(), stamp);
        }
    }

    auto dyndep_edge(fmt::ostream &file,
                     const std::filesystem::path &src_root,
                     const std::filesystem::path &build_dir,
                     const types::build_graph &bg,
                     const std::vector<std::string> &scan_outputs) -> void
    {
        auto collect_gen_outputs = [&]() {
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
        };

        auto collect_rsp_outputs = [&]() {
            std::vector<std::string> rsps;
            for (auto const &[id, bt] : bg.graph.get_vertices())
            {
                for (auto const &sg : bt.srcs)
                {
                    for (auto const &src : sg.srcs)
                    {
                        auto base = calc_output_path(src, src_root, build_dir);
                        rsps.push_back(base.string() + ".rsp");
                    }
                }
                for (auto const &gg : bt.gen_groups)
                {
                    for (auto const &go : gg.outputs)
                    {
                        if (go.kind != "header_unit")
                        {
                            auto base = calc_output_path(go.path, src_root, build_dir);
                            rsps.push_back(base.string() + ".rsp");
                        }
                    }
                }
            }
            return rsps;
        };

        const auto gen_outputs = collect_gen_outputs();
        const auto rsp_outputs = collect_rsp_outputs();

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
                                       const std::filesystem::path &src_root,
                                       const std::filesystem::path &build_dir,
                                       const types::toolchain &toolchain) -> void
    {
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            auto write_edge = [&](const std::filesystem::path &src) {
                auto base = calc_output_path(src, src_root, build_dir);
                const auto pcm = base.string() + ".pcm";
                const auto rsp = base.string() + ".rsp";
                const auto dd = base.string() + ".dd";
                file.print("build {}: precompile_named_module {} | {} || {}\n", pcm, src.string(), rsp, dd);
                file.print("  dyndep = {}\n", dd);
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
                       const std::filesystem::path &src_root,
                       const std::filesystem::path &build_dir) -> void
    {
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            auto write_named_module = [&](const std::filesystem::path &src) {
                auto base = calc_output_path(src, src_root, build_dir);
                auto pcm = base.string() + ".pcm";
                auto obj = base.string() + ".pcm.o";
                auto rsp = base.string() + ".rsp";
                file.print("build {}: compile_named_module {} | {}\n", obj, pcm, rsp);
                file.print("  rsp = {}\n", rsp);
                file.print("  cxxflags = {} {} {}\n\n",
                           fmt::join(toolchain.cxxflags, " "),
                           fmt::join(target.cxxflags.public_, " "),
                           fmt::join(target.cxxflags.private_, " "));
            };
            auto write_translation_unit = [&](const std::filesystem::path &src) {
                const bool is_c = src.extension() == ".c";
                auto base = calc_output_path(src, src_root, build_dir);
                auto obj = base.string() + ".o";
                auto rsp = base.string() + ".rsp";
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
                    auto dd = base.string() + ".dd";
                    file.print("build {}: compile_cxx_translation_unit {} | {} || {}\n", obj, src.string(), rsp, dd);
                    file.print("  dyndep = {}\n", dd);
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
                    const std::filesystem::path &src_root,
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
                            {
                                auto base = calc_output_path(src, src_root, build_dir);
                                objs.push_back(base.string() + ".o");
                            }
                        else if (sg.kind == "named_module")
                            for (const auto &src : sg.srcs)
                            {
                                auto base = calc_output_path(src, src_root, build_dir);
                                objs.push_back(base.string() + ".pcm.o");
                            }
                    }
                    for (const auto &gg : dep.gen_groups)
                        for (const auto &go : gg.outputs)
                        {
                            if (go.kind == "translation_unit")
                            {
                                auto base = calc_output_path(go.path, src_root, build_dir);
                                objs.push_back(base.string() + ".o");
                            }
                            else if (go.kind == "named_module")
                            {
                                auto base = calc_output_path(go.path, src_root, build_dir);
                                objs.push_back(base.string() + ".pcm.o");
                            }
                        }
                }
            }

            auto write_rsp = [&](const std::string &rsp_path) {
                std::filesystem::create_directories(std::filesystem::path(rsp_path).parent_path());
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
                       const std::filesystem::path &src_root,
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
                            auto base = calc_output_path(src, src_root, build_dir);
                            auto pcm = std::filesystem::path {base.string() + ".pcm"};
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
                            auto base = calc_output_path(go.path, src_root, build_dir);
                            auto pcm = std::filesystem::path {base.string() + ".pcm"};
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
            const std::filesystem::path &src_root;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &self_path;
    };

    auto write_ninja_build(const write_ninja_build_options &opts) -> void
    {
        auto file = fmt::output_file((opts.build_dir / "build.ninja").string());
        rules(file, opts.toolchain, opts.self_path, opts.build_dir, opts.graph);
        generate_edges(file, opts.graph);
        auto srcs = get_all_cxx_sources(opts.graph);
        scan_edges(file, opts.src_root, opts.build_dir, srcs);
        scan_phase(file, opts.src_root, opts.build_dir, srcs);
        rsp_edges(file, opts.src_root, opts.build_dir, srcs);
        dd_edges(file, opts.src_root, opts.build_dir, srcs);
        precompile_named_module_edges(file, opts.graph, opts.src_root, opts.build_dir, opts.toolchain);
        codegen_edges(file, opts.graph, opts.toolchain, opts.src_root, opts.build_dir);
        link_edges(file, opts.graph, opts.toolchain, opts.src_root, opts.build_dir);
    }

    struct write_ninja_install_options
    {
        public:
            const types::build_graph &graph;
            const std::filesystem::path &src_root;
            const std::filesystem::path &build_dir;
            const std::filesystem::path &prefix;
    };

    auto write_ninja_install(const write_ninja_install_options &opts) -> void
    {
        auto file = fmt::output_file((opts.build_dir / "install.ninja").string());
        file.print("rule install_file\n");
        file.print("  command = install -D $in $out\n");
        file.print("  description = INSTALL $out\n\n");
        install_edges(file, opts.graph, opts.src_root, opts.build_dir, opts.prefix);
    }
} // namespace cppbuild
