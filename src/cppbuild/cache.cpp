module;
#include "zpp_bits.h"
export module cppbuild:cache;
import std;
import fmt;
import :types;

export namespace cppbuild
{
    struct cached_source_group
    {
            std::string kind {};
            std::vector<std::string> srcs {};

            cached_source_group() = default;
            cached_source_group(const types::source_group &sg) : kind(sg.kind)
            {
                srcs.reserve(sg.srcs.size());
                for (const auto &src : sg.srcs)
                {
                    srcs.push_back(src.string());
                }
            }
    };

    struct cached_gen_output
    {
            std::string path {};
            std::string kind {};

            cached_gen_output() = default;
            cached_gen_output(const types::gen_output &go) : path(go.path.string()), kind(go.kind)
            {
            }
    };

    struct cached_gen_group
    {
            std::vector<std::string> command {};
            std::vector<std::string> inputs {};
            std::vector<cached_gen_output> outputs {};

            cached_gen_group() = default;
            cached_gen_group(const types::gen_group &gg) : command(gg.command)
            {
                inputs.reserve(gg.inputs.size());
                for (const auto &inp : gg.inputs)
                {
                    inputs.push_back(inp.string());
                }
                outputs.reserve(gg.outputs.size());
                for (const auto &go : gg.outputs)
                {
                    outputs.emplace_back(go);
                }
            }
    };

    struct cached_build_target
    {
            std::string name {};
            std::vector<cached_source_group> srcs {};
            std::vector<cached_gen_group> gen_groups {};
            std::vector<std::string> deps {};
            std::vector<std::string> cxxflags {};
            std::vector<std::string> cflags {};

            cached_build_target() = default;
            cached_build_target(const types::build_target &bt)
                : name(bt.name), deps(bt.deps), cxxflags(bt.cxxflags), cflags(bt.cflags)
            {
                srcs.reserve(bt.srcs.size());
                for (const auto &sg : bt.srcs)
                {
                    srcs.emplace_back(sg);
                }
                gen_groups.reserve(bt.gen_groups.size());
                for (const auto &gg : bt.gen_groups)
                {
                    gen_groups.emplace_back(gg);
                }
            }
    };

    auto to_build_target(const cached_build_target &cbt) -> types::build_target
    {
        types::build_target bt;
        bt.name = cbt.name;
        bt.deps = cbt.deps;
        bt.cxxflags = cbt.cxxflags;
        bt.cflags = cbt.cflags;
        bt.srcs.reserve(cbt.srcs.size());
        for (const auto &csg : cbt.srcs)
        {
            types::source_group sg;
            sg.kind = csg.kind;
            sg.srcs.reserve(csg.srcs.size());
            for (const auto &src : csg.srcs)
            {
                sg.srcs.emplace_back(src);
            }
            bt.srcs.push_back(std::move(sg));
        }
        bt.gen_groups.reserve(cbt.gen_groups.size());
        for (const auto &cgg : cbt.gen_groups)
        {
            types::gen_group gg;
            gg.command = cgg.command;
            gg.inputs.reserve(cgg.inputs.size());
            for (const auto &inp : cgg.inputs)
            {
                gg.inputs.emplace_back(inp);
            }
            gg.outputs.reserve(cgg.outputs.size());
            for (const auto &go : cgg.outputs)
            {
                gg.outputs.push_back({.path = go.path, .kind = go.kind});
            }
            bt.gen_groups.push_back(std::move(gg));
        }
        return bt;
    }

    auto save_cache(const std::filesystem::path &cache_path, const types::toolchain &tc, const types::build_graph &bg)
        -> void
    {
        std::vector<cached_build_target> cached;
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            cached.emplace_back(bt);
        }
        auto [data, out] = zpp::bits::data_out();
        out(tc, cached).or_throw();
        std::ofstream f(cache_path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), data.size());
    }

    auto load_cache(const std::filesystem::path &cache_path,
                    types::toolchain &tc,
                    std::vector<types::build_target> &targets) -> bool
    {
        std::ifstream f(cache_path, std::ios::binary);
        if (!f)
        {
            return false;
        }
        f.seekg(0, std::ios::end);
        std::vector<std::byte> data(f.tellg());
        f.seekg(0);
        f.read(reinterpret_cast<char *>(data.data()), data.size());
        std::vector<cached_build_target> cached;
        auto in = zpp::bits::in(data);
        in(tc, cached).or_throw();
        targets.reserve(cached.size());
        for (const auto &ct : cached)
        {
            targets.push_back(to_build_target(ct));
        }
        return true;
    }
} // namespace cppbuild
