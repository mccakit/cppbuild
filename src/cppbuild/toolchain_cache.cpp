export module cppbuild:toolchain_cache;
import std;
import fmt;
import glaze;
import zpp.bits;
import subprocess;
import :types;

export namespace cppbuild
{
    struct std_module_info
    {
        public:
            std::filesystem::path source_path;
            std::vector<std::string> system_include_dirs;
    };

    auto read_std_module(const std::filesystem::path &manifest_path) -> std_module_info
    {
        const auto manifest_dir = manifest_path.parent_path();

        std::string buffer;
        std::ifstream ifs(manifest_path, std::ios::binary | std::ios::ate);
        const auto size = ifs.tellg();
        ifs.seekg(0);
        buffer.resize(static_cast<std::size_t>(size));
        ifs.read(buffer.data(), size);

        glz::generic json {};
        glz::read_json(json, buffer);

        auto &root = json.get<glz::generic::object_t>();
        auto &modules = root["modules"].get<glz::generic::array_t>();

        for (auto &m : modules)
        {
            auto &obj = m.get<glz::generic::object_t>();
            if (obj["logical-name"].get<std::string>() != "std")
            {
                continue;
            }

            std_module_info info {};
            std::filesystem::path src_path {obj["source-path"].get<std::string>()};
            info.source_path = src_path.is_absolute() ? src_path : (manifest_dir / src_path).lexically_normal();

            if (auto la_it = obj.find("local-arguments"); la_it != obj.end())
            {
                auto &la = la_it->second.get<glz::generic::object_t>();
                if (auto sid_it = la.find("system-include-directories"); sid_it != la.end())
                {
                    for (auto &d : sid_it->second.get<glz::generic::array_t>())
                    {
                        std::filesystem::path dp {d.get<std::string>()};
                        info.system_include_dirs.push_back(
                            dp.is_absolute() ? dp.string() : (manifest_dir / dp).lexically_normal().string());
                    }
                }
            }
            return info;
        }
        return {};
    }

    auto precompile_std(const types::toolchain &tc, const std::filesystem::path &out_pcm) -> bool
    {
        if (tc.libcxx_modules_manifest.empty())
        {
            return false;
        }

        const auto info = read_std_module(tc.libcxx_modules_manifest);

        std::vector<std::string> cmd;
        cmd.push_back(tc.cxx_compiler);
        for (auto const &f : tc.cxxflags)
        {
            cmd.push_back(f);
        }
        for (auto const &d : info.system_include_dirs)
        {
            cmd.push_back(fmt::format("-isystem{}", d));
        }
        cmd.push_back("--precompile");
        cmd.push_back("-o");
        cmd.push_back(out_pcm.string());
        cmd.push_back(info.source_path.string());

        fmt::println("precompiling std: {}", out_pcm.string());
        subprocess::run(cmd);
        return true;
    }

    auto add_toolchain(const std::string &name,
                       const std::filesystem::path &tc_json,
                       const std::filesystem::path &self_path) -> void
    {
        types::toolchain tc {};
        tc.parse(tc_json);

        const auto dir = self_path.parent_path() / "cache" / "toolchains" / name;
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        const auto pcm_path = dir / "std.pcm";
        if (precompile_std(tc, pcm_path))
        {
            tc.cxxflags.push_back(fmt::format("-fmodule-file=std={}", pcm_path.string()));
        }

        auto [blob, out] = zpp::bits::data_out();
        out(tc).or_throw();

        std::ofstream ofs(dir / "toolchain.bin", std::ios::binary | std::ios::trunc);
        ofs.write(reinterpret_cast<const char *>(blob.data()), static_cast<std::streamsize>(blob.size()));

        fmt::println("added toolchain '{}'", name);
    }

    auto remove_toolchain(const std::string &name, const std::filesystem::path &self_path) -> void
    {
        std::error_code ec;
        std::filesystem::remove_all(self_path.parent_path() / "cache" / "toolchains" / name, ec);
        fmt::println("removed toolchain '{}'", name);
    }

    auto list_toolchains(const std::filesystem::path &self_path) -> void
    {
        const auto root = self_path.parent_path() / "cache" / "toolchains";
        if (!std::filesystem::exists(root))
        {
            return;
        }
        std::vector<std::string> names;
        for (const auto &entry : std::filesystem::directory_iterator(root))
        {
            if (entry.is_directory() && std::filesystem::exists(entry.path() / "toolchain.bin"))
            {
                names.push_back(entry.path().filename().string());
            }
        }
        std::ranges::sort(names);
        for (const auto &n : names)
        {
            fmt::println("{}", n);
        }
    }
} // namespace cppbuild
