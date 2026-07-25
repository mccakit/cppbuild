module;
export module cppbuild:cache;
import std;
import lmdb;
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
            auto env = lmdb::env::create();
            env.set_mapsize(default_db_mapsize);
            // MDB_NOSYNC and MDB_NOMETASYNC completely remove the fsync() bottleneck
            env.open(db_path,
                     lmdb::env_flags::no_subdir | lmdb::env_flags::no_sync | lmdb::env_flags::no_meta_sync,
                     0644);
            auto txn = lmdb::txn::begin(env);
            auto dbi = lmdb::dbi::open(txn, nullptr, lmdb::db_flags::create);
            txn.commit();
        }
        catch (const lmdb::error &e)
        {
            std::println(
                std::cerr, "Failed to initialize database {}: {} (code: {})", db_path.string(), e.what(), e.code());
            std::exit(1);
        }
    }
    auto load_db(const std::filesystem::path &db_path, lmdb::env_flags additional_flags = lmdb::env_flags::none)
        -> lmdb::env
    {
        auto env = lmdb::env::create();
        env.set_mapsize(default_db_mapsize);
        // MDB_NOSYNC and MDB_NOMETASYNC completely remove the fsync() bottleneck
        env.open(db_path,
                 lmdb::env_flags::no_subdir | lmdb::env_flags::no_sync | lmdb::env_flags::no_meta_sync |
                     additional_flags,
                 0644);
        return env;
    }

    template <typename T>
    auto db_put_struct(lmdb::dbi &db, lmdb::txn &txn, std::string_view key, const T &value) -> void
    {
        auto [data, out] = zpp::bits::data_out();
        out(value).or_throw();
        std::string_view sv {reinterpret_cast<const char *>(data.data()), data.size()};
        db.put(txn, key, sv);
    }

    template <typename T>
    auto db_get_struct(lmdb::dbi &db, lmdb::txn &txn, std::string_view key) -> std::optional<T>
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

    auto db_put_raw(lmdb::dbi &db, lmdb::txn &txn, std::string_view key, std::string_view value) -> void
    {
        db.put(txn, key, value);
    }

    auto db_get_raw(lmdb::dbi &db, lmdb::txn &txn, std::string_view key) -> std::optional<std::string>
    {
        std::string_view sv;
        if (!db.get(txn, key, sv))
            return std::nullopt;
        return std::string {sv};
    }
} // namespace cppbuild
