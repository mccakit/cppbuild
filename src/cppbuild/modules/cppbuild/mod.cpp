module;
export module cppbuild:modules_cppbuild;
import std;
import umkacxx;

constexpr char src[] = {
#embed "mod.um"
    , 0};

struct source_files_umka
{
    public:
        umkacxx::types::bool_t generated;
        umkacxx::types::str_t command;
        umkacxx::types::arr_t<umkacxx::types::str_t> inputs;
        umkacxx::types::str_t kind;
        umkacxx::types::arr_t<umkacxx::types::str_t> srcs;
};
class source_files_cxx
{
    public:
        bool generated {};
        std::string command {};
        std::vector<std::filesystem::path> inputs;
        std::string kind {};
        std::vector<std::filesystem::path> srcs;
        source_files_cxx(const source_files_umka &raw)
            : generated {raw.generated}, command {raw.command ? raw.command : ""}, kind {raw.kind ? raw.kind : ""},
              inputs {raw.inputs.data, raw.inputs.data + raw.inputs.len()},
              srcs {raw.srcs.data, raw.srcs.data + raw.srcs.len()}
        {
        }
};

struct build_target_umka
{
    public:
        umkacxx::types::str_t name;
        umkacxx::types::arr_t<source_files_umka> srcs;
        umkacxx::types::arr_t<umkacxx::types::str_t> deps;
        umkacxx::types::arr_t<umkacxx::types::str_t> cxxflags;
        umkacxx::types::arr_t<umkacxx::types::str_t> cflags;
};

struct build_target_cxx
{
    public:
        std::string name {};
        std::vector<source_files_cxx> srcs {};
        std::vector<std::string> deps {};
        std::vector<std::string> cxxflags {};
        std::vector<std::string> cflags {};
        build_target_cxx(const build_target_umka &raw)
            : name {raw.name ? raw.name : ""}, deps {raw.deps.data, raw.deps.data + raw.deps.len()},
              cxxflags {raw.cxxflags.data, raw.cxxflags.data + raw.cxxflags.len()},
              cflags {raw.cflags.data, raw.cflags.data + raw.cflags.len()}
        {
            auto len = raw.srcs.len();
            srcs.reserve(len);
            for (int i = 0; i < len; ++i)
            {
                srcs.emplace_back(raw.srcs.data[i]);
            }
        }
};

struct link_target_umka
{
    public:
        umkacxx::types::str_t name;
        umkacxx::types::str_t kind;
        umkacxx::types::arr_t<umkacxx::types::str_t> deps;
        umkacxx::types::arr_t<umkacxx::types::str_t> ldflags;
};

struct link_target_cxx
{
    public:
        std::string name {};
        std::string kind {};
        std::vector<std::string> deps {};
        std::vector<std::string> ldflags {};
        link_target_cxx(const link_target_umka &raw)
            : name {raw.name ? raw.name : ""}, kind {raw.kind ? raw.kind : ""},
              deps {raw.deps.data, raw.deps.data + raw.deps.len()},
              ldflags {raw.ldflags.data, raw.ldflags.data + raw.ldflags.len()}
        {
        }
};

struct install_target_umka
{
    public:
        umkacxx::types::str_t name;
        umkacxx::types::str_t install_dir;
        umkacxx::types::arr_t<umkacxx::types::str_t> files;
        umkacxx::types::arr_t<umkacxx::types::str_t> build_targets;
        umkacxx::types::arr_t<umkacxx::types::str_t> link_targets;
};

struct install_target_cxx
{
    public:
        std::string name {};
        std::filesystem::path install_dir {};
        std::vector<std::string> files {};
        std::vector<std::string> build_targets {};
        std::vector<std::string> link_targets {};
        install_target_cxx(const install_target_umka &raw)
            : name {raw.name ? raw.name : ""}, install_dir {raw.install_dir ? raw.install_dir : ""},
              files {raw.files.data, raw.files.data + raw.files.len()},
              build_targets {raw.build_targets.data, raw.build_targets.data + raw.build_targets.len()},
              link_targets {raw.link_targets.data, raw.link_targets.data + raw.link_targets.len()}
        {
        }
};

export namespace cppbuild::modules_cppbuild
{
    umkacxx::types::module_t mod {"cppbuild.um", src, {}};
    struct results_umka
    {
        public:
            umkacxx::types::arr_t<build_target_umka> build_targets;
            umkacxx::types::arr_t<link_target_umka> link_targets;
            umkacxx::types::arr_t<install_target_umka> install_targets;
    };

    struct results_cxx
    {
        public:
            std::vector<build_target_cxx> build_targets {};
            std::vector<link_target_cxx> link_targets {};
            std::vector<install_target_cxx> install_targets {};

            results_cxx(umkacxx::types::vm_handle vm, results_umka raw)
            {
                auto blen = raw.build_targets.len();
                build_targets.reserve(blen);
                for (int i = 0; i < blen; ++i)
                {
                    build_targets.emplace_back(raw.build_targets.data[i]);
                }
                raw.build_targets.decref(vm);

                auto llen = raw.link_targets.len();
                link_targets.reserve(llen);
                for (int i = 0; i < llen; ++i)
                {
                    link_targets.emplace_back(raw.link_targets.data[i]);
                }
                raw.link_targets.decref(vm);

                auto ilen = raw.install_targets.len();
                install_targets.reserve(ilen);
                for (int i = 0; i < ilen; ++i)
                {
                    install_targets.emplace_back(raw.install_targets.data[i]);
                }
                raw.install_targets.decref(vm);
            }
            auto print() const -> void
            {
                std::println("=== Build Targets ({}) ===", build_targets.size());
                for (auto &bt : build_targets)
                {
                    std::println("  name: {}", bt.name);
                    std::println("  deps: {}", bt.deps.size());
                    for (auto &d : bt.deps)
                    {
                        std::println("    - {}", d);
                    }
                    std::println("  srcs: {}", bt.srcs.size());
                    for (auto &sf : bt.srcs)
                    {
                        std::println("    kind: {}", sf.kind);
                        for (auto &s : sf.srcs)
                        {
                            std::println("      - {}", s.string());
                        }
                    }
                }

                std::println("=== Link Targets ({}) ===", link_targets.size());
                for (auto &lt : link_targets)
                {
                    std::println("  name: {}", lt.name);
                    for (auto &d : lt.deps)
                    {
                        std::println("    - {}", d);
                    }
                }

                std::println("=== Install Targets ({}) ===", install_targets.size());
                for (auto &it : install_targets)
                {
                    std::println("  install_dir: {}", it.install_dir.string());
                    for (auto &bt : it.build_targets)
                    {
                        std::println("    build: {}", bt);
                    }
                    for (auto &lt : it.link_targets)
                    {
                        std::println("    link: {}", lt);
                    }
                }
            }
    };
} // namespace cppbuild::modules_cppbuild
