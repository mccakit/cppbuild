module;
export module cppbuild:cache;
import std;
import lmdbxx;
import zpp.bits;
import fmt;
import :types;
export namespace cppbuild
{
    constexpr std::size_t default_db_mapsize = static_cast<std::size_t>(1) << 30; // 1 GiB
    auto create_db(const std::filesystem::path &db_path) -> void
    {
        try
        {
            auto env = lmdbxx::env::create();
            env.set_mapsize(default_db_mapsize);
            env.open(db_path, lmdbxx::env_flags::no_subdir, 0644);
            auto txn = lmdbxx::txn::begin(env);
            auto dbi = lmdbxx::dbi::open(txn, nullptr, lmdbxx::db_flags::create);
            txn.commit();
        }
        catch (const lmdbxx::error &e)
        {
            std::println(
                std::cerr, "Failed to initialize database {}: {} (code: {})", db_path.string(), e.what(), e.code());
            std::exit(1);
        }
    }
    auto load_db(const std::filesystem::path &db_path, lmdbxx::env_flags additional_flags = lmdbxx::env_flags::none)
        -> lmdbxx::env
    {
        auto env = lmdbxx::env::create();
        env.set_mapsize(default_db_mapsize);
        env.open(db_path, lmdbxx::env_flags::no_subdir | additional_flags, 0644);
        return env;
    }
} // namespace cppbuild
