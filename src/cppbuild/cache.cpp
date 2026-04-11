module;
export module cppbuild:cache;
import std;
import fmt;
import zpp.bits;
import :types;

export namespace cppbuild::cache
{
    struct cache_payload
    {
        public:
            types::toolchain tc {};
            std::vector<types::build_target> build_targets {};
            std::vector<types::link_target> link_targets {};
    };

    auto save(const std::filesystem::path &cache_path, const types::toolchain &tc, const types::build_graph &bg) -> void
    {
        std::vector<types::build_target> build_targets;
        build_targets.reserve(bg.graph.vertex_count());
        for (const auto &[id, bt] : bg.graph.get_vertices())
        {
            build_targets.push_back(bt);
        }

        std::vector<types::link_target> link_targets = bg.link_targets;

        auto [data, out] = zpp::bits::data_out();
        out(tc, build_targets, link_targets).or_throw();

        std::ofstream f(cache_path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), data.size());
    }

    auto load(const std::filesystem::path &cache_path) -> cache_payload
    {
        std::ifstream f(cache_path, std::ios::binary);
        if (!f)
        {
            fmt::print("Failed to open cache file: {}\n", cache_path.string());
            std::terminate();
        }
        f.seekg(0, std::ios::end);
        std::streamsize size = f.tellg();
        f.seekg(0);
        std::vector<std::byte> data(size);
        if (!f.read(reinterpret_cast<char *>(data.data()), size))
        {
            fmt::print("Failed to read cache file: {}\n", cache_path.string());
            std::terminate();
        }

        cache_payload result;
        auto in = zpp::bits::in(data);
        if (zpp::bits::failure(in(result.tc, result.build_targets, result.link_targets)))
        {
            fmt::print("Failed to deserialize cache: {}\n", cache_path.string());
            std::terminate();
        }
        return result;
    }
} // namespace cppbuild::cache
