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
        file.print("  command = {} gen_p1689 --build-dir=$build_dir --cxx-compiler=$cxx_compiler --cxxflags=$cxxflags "
                   "--cflags=$cflags --c-compiler=$c_compiler --cxx-sources=$cxx_sources --c-sources=$c_sources\n",
                   self_path.string());
        file.print("  description = GEN P1689 DB $out\n");
        file.print("  restat = 1\n\n");

        // Scan P1689 database and produce dyndep file
        file.print("rule scan_deps\n");
        file.print("  command = {} scan --build-dir=$build_dir\n", self_path.string());
        file.print("  description = SCAN $out\n");
        file.print("  restat = 1\n\n");

        // Generate per-source response files
        file.print("rule rsp_gen\n");
        file.print("  command = {} rsp_gen --build-dir=$build_dir --cxxflags=$cxxflags "
                   "--cflags=$cflags --cxx-sources=$cxx_sources --c-sources=$c_sources\n",
                   self_path.string());
        file.print("  description = RSP GEN $out\n");
        file.print("  restat = 1\n\n");

        // Precompile named module to .pcm
        file.print("rule precompile\n");
        file.print("  command = {} --precompile -x c++-module -Wno-experimental-header-units $in -o $out @$rsp\n",
                   toolchain.cxx_compiler);
        file.print("  dyndep = $dyndep\n");
        file.print("  description = PCM $out\n\n");

        // Build compile_commands.json then scan it for dependencies
        std::vector<std::string> c_sources;
        c_sources.reserve(1024);
        std::vector<std::string> cxx_sources;
        cxx_sources.reserve(1024);
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            for (const auto &sg : bt.srcs)
            {
                for (const auto &src : sg.srcs)
                {
                    if (src.extension() == ".c")
                    {
                        c_sources.push_back(src.string());
                    }
                    else
                    {
                        cxx_sources.push_back(src.string());
                    }
                }
            }
        }
        file.print("build {}/compile_commands.json: generate_p1689\n", build_dir.string());
        file.print("  build_dir = {}\n", build_dir.string());
        file.print("  cxx_compiler = {}\n", toolchain.cxx_compiler);
        file.print("  c_compiler = {}\n", toolchain.c_compiler);
        file.print("  cxxflags = \"{}\"\n", fmt::join(toolchain.cxxflags, " "));
        file.print("  cflags = \"{}\"\n", fmt::join(toolchain.cflags, " "));
        file.print("  cxx_sources = \"{}\"\n", fmt::join(cxx_sources, " "));
        file.print("  c_sources = \"{}\"\n\n", fmt::join(c_sources, " "));
        file.print("build {}/deps.dd: scan_deps || {}/compile_commands.json\n", build_dir.string(), build_dir.string());
        file.print("  build_dir = {}\n\n", build_dir.string());

        // Generate per-source response files
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
        file.print("build {}: rsp_gen || {}/compile_commands.json\n", fmt::join(rsp_outputs, " "), build_dir.string());
        file.print("  build_dir = {}\n", build_dir.string());
        file.print("  cxxflags = \"{}\"\n", fmt::join(toolchain.cxxflags, " "));
        file.print("  cflags = \"{}\"\n", fmt::join(toolchain.cflags, " "));
        file.print("  cxx_sources = \"{}\"\n", fmt::join(cxx_sources, " "));
        file.print("  c_sources = \"{}\"\n\n", fmt::join(c_sources, " "));
    }

    auto precompile_edges(fmt::ostream &file,
                          const types::build_graph &bg,
                          const std::filesystem::path &build_dir) -> void
    {
        const auto dyndep = (build_dir / "deps.dd").string();
        for (auto id : bg.topo_order)
        {
            const auto &target = bg.graph.get_vertex(id);
            for (const auto &sg : target.srcs)
            {
                if (sg.kind == "named_module")
                {
                    for (const auto &src : sg.srcs)
                    {
                        const auto pcm = (build_dir / (src.filename().string() + ".pcm")).string();
                        const auto rsp = (build_dir / (src.filename().string() + ".rsp")).string();
                        file.print("build {}: precompile {} || {}\n", pcm, src.string(), dyndep);
                        file.print("  dyndep = {}\n", dyndep);
                        file.print("  rsp = {}\n\n", rsp);
                    }
                }
            }
            for (const auto &gg : target.gen_groups)
            {
                for (const auto &out : gg.outputs)
                {
                    if (out.kind == "named_module")
                    {
                        const auto pcm = (build_dir / (out.path.filename().string() + ".pcm")).string();
                        const auto rsp = (build_dir / (out.path.filename().string() + ".rsp")).string();
                        file.print("build {}: precompile {} || {}\n", pcm, out.path.string(), dyndep);
                        file.print("  dyndep = {}\n", dyndep);
                        file.print("  rsp = {}\n\n", rsp);
                    }
                }
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
        auto file = fmt::output_file((opts.build_dir / "build.ninja").string());
        rules(file, opts.toolchain, opts.self_path, opts.build_dir, opts.graph);
        precompile_edges(file, opts.graph, opts.build_dir);
    }
} // namespace cppbuild
