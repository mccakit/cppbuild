module;
#include <SQLiteCpp/SQLiteCpp.h>
export module cppbuild:toolchain_cache;
import std;
import fmt;
import glaze;
import zpp.bits;
import subprocess;
import :types;

export namespace cppbuild
{
    auto cache_root(const std::filesystem::path &self_path) -> std::filesystem::path
    {
        auto root = self_path.parent_path() / "cache";
        std::filesystem::create_directories(root);
        return root;
    }

    auto cache_db_path(const std::filesystem::path &self_path) -> std::filesystem::path
    {
        return cache_root(self_path) / "toolchains.db";
    }

    auto cache_pcm_dir(const std::filesystem::path &self_path, const std::string &name) -> std::filesystem::path
    {
        return cache_root(self_path) / "pcms" / name;
    }

    auto open_cache_db(const std::filesystem::path &self_path) -> SQLite::Database
    {
        SQLite::Database db {cache_db_path(self_path).string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
        db.exec("CREATE TABLE IF NOT EXISTS toolchains ("
                "name TEXT PRIMARY KEY, "
                "blob BLOB NOT NULL)");
        return db;
    }

    struct std_module_info
    {
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

    auto precompile_std(const types::toolchain &tc, const std::string &name, const std::filesystem::path &self_path)
        -> std::filesystem::path
    {
        if (tc.libcxx_modules_manifest.empty())
        {
            return {};
        }

        const auto info = read_std_module(tc.libcxx_modules_manifest);
        const auto out_dir = cache_pcm_dir(self_path, name);
        std::filesystem::remove_all(out_dir);
        std::filesystem::create_directories(out_dir);
        const auto out_pcm = out_dir / "std.pcm";

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
        return out_pcm;
    }

    auto register_toolchain(const std::string &name,
                            const std::filesystem::path &tc_json,
                            const std::filesystem::path &self_path) -> void
    {
        types::toolchain tc {};
        tc.parse(tc_json);

        if (auto pcm = precompile_std(tc, name, self_path); !pcm.empty())
        {
            tc.cxxflags.push_back(fmt::format("-fmodule-file=std={}", pcm.string()));
        }

        auto [blob, out] = zpp::bits::data_out();
        out(tc).or_throw();

        auto db = open_cache_db(self_path);
        SQLite::Statement stmt {db, "INSERT OR REPLACE INTO toolchains (name, blob) VALUES (?, ?)"};
        stmt.bind(1, name);
        stmt.bind(2, blob.data(), static_cast<int>(blob.size()));
        stmt.exec();

        fmt::println("registered toolchain '{}'", name);
    }

    auto get_toolchain(const std::string &name, const std::filesystem::path &self_path) -> types::toolchain
    {
        auto db = open_cache_db(self_path);
        SQLite::Statement stmt {db, "SELECT blob FROM toolchains WHERE name = ?"};
        stmt.bind(1, name);
        stmt.executeStep();
        auto col = stmt.getColumn(0);
        const auto *bytes = static_cast<const std::byte *>(col.getBlob());
        std::span<const std::byte> span {bytes, static_cast<std::size_t>(col.getBytes())};

        types::toolchain tc {};
        auto in = zpp::bits::in(span);
        in(tc).or_throw();
        return tc;
    }

    auto list_toolchains(const std::filesystem::path &self_path) -> void
    {
        auto db = open_cache_db(self_path);
        SQLite::Statement stmt {db, "SELECT name FROM toolchains ORDER BY name"};
        while (stmt.executeStep())
        {
            fmt::println("{}", stmt.getColumn(0).getString());
        }
    }

    auto remove_toolchain(const std::string &name, const std::filesystem::path &self_path) -> void
    {
        auto db = open_cache_db(self_path);
        SQLite::Statement stmt {db, "DELETE FROM toolchains WHERE name = ?"};
        stmt.bind(1, name);
        stmt.exec();

        std::error_code ec;
        std::filesystem::remove_all(cache_pcm_dir(self_path, name), ec);
        fmt::println("removed toolchain '{}'", name);
    }

    auto clear_toolchains(const std::filesystem::path &self_path) -> void
    {
        auto db = open_cache_db(self_path);
        db.exec("DELETE FROM toolchains");
        std::error_code ec;
        std::filesystem::remove_all(cache_root(self_path) / "pcms", ec);
        fmt::println("cleared all cached toolchains");
    }

    auto show_toolchain(const std::string &name, const std::filesystem::path &self_path) -> void
    {
        const auto tc = get_toolchain(name, self_path);
        fmt::println("name: {}", name);
        tc.print();
    }
} // namespace cppbuild
