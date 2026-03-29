module;
#include "zpp_bits.h"
export module cppbuild:cache;
import std;
import fmt;
import :types;

export namespace cppbuild::cache
{
    class source_group
    {
        public:
            std::string kind {};
            std::vector<std::string> srcs {};

            source_group() = default;
            source_group(const types::source_group &sg) : kind(sg.kind)
            {
                srcs.reserve(sg.srcs.size());
                for (const auto &src : sg.srcs)
                {
                    srcs.push_back(src.string());
                }
            }

            auto process() const -> types::source_group
            {
                types::source_group sg;
                sg.kind = kind;
                sg.srcs.reserve(srcs.size());
                for (const auto &src : srcs)
                {
                    sg.srcs.emplace_back(src);
                }
                return sg;
            }
    };

    class gen_output
    {
        public:
            std::string path {};
            std::string kind {};

            gen_output() = default;
            gen_output(const types::gen_output &go) : path(go.path.string()), kind(go.kind)
            {
            }

            auto process() const -> types::gen_output
            {
                return {.path = path, .kind = kind};
            }
    };

    class gen_group
    {
        public:
            std::vector<std::string> command {};
            std::vector<std::string> inputs {};
            std::vector<gen_output> outputs {};

            gen_group() = default;
            gen_group(const types::gen_group &gg) : command(gg.command)
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

            auto process() const -> types::gen_group
            {
                types::gen_group gg;
                gg.command = command;
                gg.inputs.reserve(inputs.size());
                for (const auto &inp : inputs)
                {
                    gg.inputs.emplace_back(inp);
                }
                gg.outputs.reserve(outputs.size());
                for (const auto &go : outputs)
                {
                    gg.outputs.push_back(go.process());
                }
                return gg;
            }
    };

    class cxx_flags
    {
        public:
            std::vector<std::string> public_ {};
            std::vector<std::string> private_ {};

            cxx_flags() = default;
            cxx_flags(const types::cxx_flags &f) : public_(f.public_), private_(f.private_)
            {
            }

            auto process() const -> types::cxx_flags
            {
                return {.public_ = public_, .private_ = private_};
            }
    };

    class c_flags
    {
        public:
            std::vector<std::string> public_ {};
            std::vector<std::string> private_ {};

            c_flags() = default;
            c_flags(const types::c_flags &f) : public_(f.public_), private_(f.private_)
            {
            }

            auto process() const -> types::c_flags
            {
                return {.public_ = public_, .private_ = private_};
            }
    };

    class build_target
    {
        public:
            std::string name {};
            std::vector<source_group> srcs {};
            std::vector<gen_group> gen_groups {};
            std::vector<std::string> deps {};
            cxx_flags cxxflags {};
            c_flags cflags {};

            build_target() = default;
            build_target(const types::build_target &bt)
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

            auto process() const -> types::build_target
            {
                types::build_target bt;
                bt.name = name;
                bt.deps = deps;
                bt.cxxflags = cxxflags.process();
                bt.cflags = cflags.process();
                bt.srcs.reserve(srcs.size());
                for (const auto &csg : srcs)
                {
                    bt.srcs.push_back(csg.process());
                }
                bt.gen_groups.reserve(gen_groups.size());
                for (const auto &cgg : gen_groups)
                {
                    bt.gen_groups.push_back(cgg.process());
                }
                return bt;
            }
    };

    class link_target
    {
        public:
            std::string name {};
            std::string kind {};
            std::vector<std::string> deps {};
            std::vector<std::string> ldflags {};

            link_target() = default;
            link_target(const types::link_target &lt) : name(lt.name), kind(lt.kind), deps(lt.deps), ldflags(lt.ldflags)
            {
            }

            auto process() const -> types::link_target
            {
                return {.name = name, .kind = kind, .deps = deps, .ldflags = ldflags};
            }
    };

    class cache_payload
    {
        public:
            types::toolchain tc {};
            std::vector<types::build_target> build_targets {};
            std::vector<types::link_target> link_targets {};
    };

    auto save(const std::filesystem::path &cache_path, const types::toolchain &tc, const types::build_graph &bg)
        -> void
    {
        std::vector<build_target> cached_builds;
        std::vector<link_target> cached_links;

        cached_builds.reserve(bg.graph.vertex_count());
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            cached_builds.emplace_back(bt);
        }

        cached_links.reserve(bg.link_targets.size());
        for (const auto &lt : bg.link_targets)
        {
            cached_links.emplace_back(lt);
        }

        auto [data, out] = zpp::bits::data_out();
        out(tc, cached_builds, cached_links).or_throw();

        std::ofstream f(cache_path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), data.size());
    }

    auto load(const std::filesystem::path &cache_path) -> cache_payload
    {
        std::ifstream f(cache_path, std::ios::binary);
        if (!f)
        {
            fmt::print(stderr, "Failed to open cache file: {}\n", cache_path.string());
            std::terminate();
        }
        f.seekg(0, std::ios::end);
        std::streamsize size = f.tellg();
        f.seekg(0);
        std::vector<std::byte> data(size);
        if (!f.read(reinterpret_cast<char *>(data.data()), size))
        {
            fmt::print(stderr, "Failed to read cache file: {}\n", cache_path.string());
            std::terminate();
        }

        types::toolchain tc;
        std::vector<build_target> cached_builds;
        std::vector<link_target> cached_links;

        auto in = zpp::bits::in(data);
        if (zpp::bits::failure(in(tc, cached_builds, cached_links)))
        {
            fmt::print(stderr, "Failed to deserialize cache: {}\n", cache_path.string());
            std::terminate();
        }

        cache_payload result;
        result.tc = std::move(tc);
        result.build_targets.reserve(cached_builds.size());
        for (const auto &ct : cached_builds)
        {
            result.build_targets.push_back(ct.process());
        }
        result.link_targets.reserve(cached_links.size());
        for (const auto &clt : cached_links)
        {
            result.link_targets.push_back(clt.process());
        }
        return result;
    }
} // namespace cppbuild::cache
