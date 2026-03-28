module;
#include <new>
export module cppbuild:ninja;
import std;
import fmt;
import graaf;
import :types;
export namespace cppbuild
{
    auto rules(fmt::ostream &file,
               const types::toolchain &toolchain,
               const std::filesystem::path &self_path,
               const std::filesystem::path &build_dir,
               const types::build_graph &bg) -> void
    {
        // Generate P1689 compilation database
        file.print("rule generate_p1689\n");
        file.print("  command = {} gen_p1689 --build-dir=$build_dir\n", self_path.string());
        file.print("  description = GEN P1689 DB $out\n");
        file.print("  restat = 1\n\n");

        // Scan P1689 database and produce dyndep file
        file.print("rule scan_srcs\n");
        file.print("  command = {} scan_srcs --build-dir=$build_dir\n", self_path.string());
        file.print("  description = SCAN $out\n");
        file.print("  restat = 1\n\n");

        // Precompile named module to .pcm
        file.print("rule precompile_named_module\n");
        file.print(
            "  command = {} --precompile -x c++-module -Wno-experimental-header-units $in -o $out @$rsp $cxxflags\n",
            toolchain.cxx_compiler);
        file.print("  dyndep = $dyndep\n");
        file.print("  description = PCM $out\n\n");

        // Precompile header unit to .pcm
        file.print("rule precompile_header_unit\n");
        file.print("  command = {} --precompile -fmodule-header=user -xc++-user-header $in -o $out $cxxflags\n",
                   toolchain.cxx_compiler);
        file.print("  description = PCM $out\n\n");

        // Compile named module .pcm to .o
        file.print("rule compile_named_module\n");
        file.print("  command = {} -c $in -o $out @$rsp $cxxflags\n", toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");

        // Compile C++ translation unit to .o
        file.print("rule compile_cxx_translation_unit\n");
        file.print("  command = {} -c $in -o $out -Wno-experimental-header-units @$rsp $cxxflags $header_unit_flags\n",
                   toolchain.cxx_compiler);
        file.print("  description = OBJ $out\n\n");

        // Compile C translation unit to .o
        file.print("rule compile_c_translation_unit\n");
        file.print("  command = {} -c $in -o $out $cflags\n", toolchain.c_compiler);
        file.print("  description = OBJ $out\n\n");
    }

    auto init(fmt::ostream &file, const std::filesystem::path &build_dir, const types::build_graph &bg) -> void
    {
        // Collect rsp outputs
        std::vector<std::string> rsp_outputs;
        rsp_outputs.reserve(1024);
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    rsp_outputs.push_back((build_dir / (src.filename().string() + ".rsp")).string());
                }
            }
        }

        file.print("build {}/compile_commands.json: generate_p1689\n", build_dir.string());
        file.print("  build_dir = {}\n\n", build_dir.string());

        file.print("build {}/deps.dd: scan_srcs || {}/compile_commands.json\n", build_dir.string(), build_dir.string());
        file.print("  build_dir = {}\n\n", build_dir.string());
    }

    auto precompile_header_unit_edges(fmt::ostream &file,
                                      const types::build_graph &bg,
                                      const std::filesystem::path &build_dir,
                                      const types::toolchain &toolchain) -> void
    {
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "header_unit")
                {
                    for (auto const &src : sg.srcs)
                    {
                        const auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                        file.print("build {}: precompile_header_unit {}\n", pcm, src.string());
                        file.print("  cxxflags = {} {}\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
                        file.print("\n");
                    }
                }
            }
        }
    }

    struct header_unit_info
    {
            std::string flags {};
            std::string order_only {};
    };
    struct header_unit_info_map
    {
            std::unordered_map<graaf::vertex_id_t, header_unit_info> data {};
    };
    auto collect_header_unit_info(const types::build_graph &bg, const std::filesystem::path &build_dir)
        -> header_unit_info_map
    {
        header_unit_info_map map {};
        for (auto id : bg.topo_order)
        {
            header_unit_info info {};
            const auto &target = bg.graph.get_vertex(id);
            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "header_unit")
                {
                    for (auto const &src : sg.srcs)
                    {
                        auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                        info.flags += fmt::format(" -fmodule-file={}", pcm);
                        info.order_only += fmt::format(" {}", pcm);
                    }
                }
            }
            map.data[id] = std::move(info);
        }
        return map;
    }

    auto precompile_named_module_edges(fmt::ostream &file,
                                       const types::build_graph &bg,
                                       const std::filesystem::path &build_dir,
                                       const header_unit_info_map &hu_map,
                                       const types::toolchain &toolchain) -> void
    {
        const auto dyndep = (build_dir / "deps.dd").string();
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            const auto &info = hu_map.data.at(id);
            const auto order_only = " || " + dyndep + info.order_only;
            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (auto const &src : sg.srcs)
                    {
                        const auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                        const auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                        file.print("build {}: precompile_named_module {}{}\n", pcm, src.string(), order_only);
                        file.print("  dyndep = {}\n", dyndep);
                        file.print("  rsp = {}\n", rsp);
                        file.print("  cxxflags = {} {}\n\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
                    }
                }
            }
        }
    }

    auto codegen_edges(fmt::ostream &file,
                       const types::build_graph &bg,
                       const types::toolchain &toolchain,
                       const std::filesystem::path &build_dir,
                       const header_unit_info_map &hu_map) -> void
    {
        const auto dyndep = (build_dir / "deps.dd").string();
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            const auto &info = hu_map.data.at(id);
            const auto order_only = " || " + dyndep + info.order_only;
            for (auto const &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (auto const &src : sg.srcs)
                    {
                        auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                        auto obj = (build_dir / (src.filename().string() + ".pcm.o")).string();
                        auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                        file.print("build {}: compile_named_module {}\n", obj, pcm);
                        file.print("  rsp = {}\n", rsp);
                        file.print("  cxxflags = {} {}\n\n",
                                   fmt::join(toolchain.cxxflags, " "),
                                   fmt::join(target.cxxflags, " "));
                    }
                }
                else if (sg.kind == "translation_unit")
                {
                    for (auto const &src : sg.srcs)
                    {
                        const bool is_c = src.extension() == ".c";
                        auto obj = (build_dir / (src.filename().string() + ".o")).string();
                        auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                        if (is_c)
                        {
                            file.print("build {}: compile_c_translation_unit {}\n", obj, src.string());
                            file.print("  cflags = {} {}\n\n", fmt::join(toolchain.cflags, " "), fmt::join(target.cflags, " "));
                        }
                        else
                        {
                            file.print("build {}: compile_cxx_translation_unit {}{}\n", obj, src.string(), order_only);
                            file.print("  dyndep = {}\n", dyndep);
                            file.print("  rsp = {}\n", rsp);
                            file.print("  cxxflags = {} {}\n", fmt::join(toolchain.cxxflags, " "), fmt::join(target.cxxflags, " "));
                            if (!info.flags.empty())
                                file.print("  header_unit_flags ={}\n", info.flags);
                            file.print("\n");
                        }
                    }
                }
            }
        }
        file.print("\n");
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
        const auto hu_map = collect_header_unit_info(opts.graph, opts.build_dir);
        auto file = fmt::output_file((opts.build_dir / "build.ninja").string());
        rules(file, opts.toolchain, opts.self_path, opts.build_dir, opts.graph);
        init(file, opts.build_dir, opts.graph);
        precompile_header_unit_edges(file, opts.graph, opts.build_dir, opts.toolchain);
        precompile_named_module_edges(file, opts.graph, opts.build_dir, hu_map, opts.toolchain);
        codegen_edges(file, opts.graph, opts.toolchain, opts.build_dir, hu_map);
    }
} // namespace cppbuild
