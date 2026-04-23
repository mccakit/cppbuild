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
            // MDB_NOSYNC and MDB_NOMETASYNC completely remove the fsync() bottleneck
            env.open(db_path,
                     lmdbxx::env_flags::no_subdir | lmdbxx::env_flags::no_sync | lmdbxx::env_flags::no_meta_sync,
                     0644);
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
        // MDB_NOSYNC and MDB_NOMETASYNC completely remove the fsync() bottleneck
        env.open(db_path,
                 lmdbxx::env_flags::no_subdir | lmdbxx::env_flags::no_sync | lmdbxx::env_flags::no_meta_sync |
                     additional_flags,
                 0644);
        return env;
    }

    template <typename T>
    auto db_put_struct(lmdbxx::dbi &db, lmdbxx::txn &txn, std::string_view key, const T &value) -> void
    {
        auto [data, out] = zpp::bits::data_out();
        out(value).or_throw();
        std::string_view sv {reinterpret_cast<const char *>(data.data()), data.size()};
        db.put(txn, key, sv);
    }

    template <typename T>
    auto db_get_struct(lmdbxx::dbi &db, lmdbxx::txn &txn, std::string_view key) -> std::optional<T>
    {
        std::string_view sv;
        if (!db.get(txn, key, sv))
            return std::nullopt;
        std::span<const std::byte> bytes {reinterpret_cast<const std::byte *>(sv.data()), sv.size()};
        T value;
        auto in = zpp::bits::in(bytes);
        in(value).or_throw();
        return value;
    }

    auto db_put_raw(lmdbxx::dbi &db, lmdbxx::txn &txn, std::string_view key, std::string_view value) -> void
    {
        db.put(txn, key, value);
    }

    auto db_get_raw(lmdbxx::dbi &db, lmdbxx::txn &txn, std::string_view key) -> std::optional<std::string>
    {
        std::string_view sv;
        if (!db.get(txn, key, sv))
            return std::nullopt;
        return std::string {sv};
    }
} // namespace cppbuild
