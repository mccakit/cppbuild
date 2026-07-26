module;
export module cppbuild:modules_cppbuild;
import std;
import umka;

constexpr char src[] = {
#embed "mod.um"
    , 0};

struct source_files_umka
{
    public:
        umka::str_t kind;
        umka::arr_t<umka::str_t> srcs;
};

class source_files_cxx
{
    public:
        std::string kind {};
        std::vector<std::filesystem::path> srcs;

        explicit source_files_cxx(const source_files_umka &raw)
            : kind {raw.kind ? raw.kind : ""}, srcs {raw.srcs.data, raw.srcs.data + raw.srcs.len()}
        {
        }
};

struct gen_output_umka
{
    public:
        umka::str_t path;
        umka::str_t kind;
};

struct gen_output_cxx
{
    public:
        std::filesystem::path path {};
        std::string kind {};

        explicit gen_output_cxx(const gen_output_umka &raw)
            : path {raw.path ? raw.path : ""}, kind {raw.kind ? raw.kind : ""}
        {
        }
};

struct gen_src_umka
{
    public:
        umka::arr_t<umka::str_t> command;
        umka::arr_t<umka::str_t> inputs;
        umka::arr_t<gen_output_umka> outputs;
};

struct gen_src_cxx
{
    public:
        std::vector<std::string> command {};
        std::vector<std::filesystem::path> inputs {};
        std::vector<gen_output_cxx> outputs {};

        explicit gen_src_cxx(const gen_src_umka &raw)
            : command {raw.command.data, raw.command.data + raw.command.len()},
              inputs {raw.inputs.data, raw.inputs.data + raw.inputs.len()}
        {
            const auto len = raw.outputs.len();
            outputs.reserve(static_cast<std::size_t>(len));
            for (umka::int_t i = 0; i < len; ++i)
            {
                outputs.emplace_back(raw.outputs.data[i]);
            }
        }
};

struct cxx_flags_umka
{
    public:
        umka::arr_t<umka::str_t> public_;
        umka::arr_t<umka::str_t> private_;
};

struct cxx_flags_cxx
{
    public:
        std::vector<std::string> public_ {};
        std::vector<std::string> private_ {};

        cxx_flags_cxx() = default;
        explicit cxx_flags_cxx(const cxx_flags_umka &raw)
            : public_ {raw.public_.data, raw.public_.data + raw.public_.len()},
              private_ {raw.private_.data, raw.private_.data + raw.private_.len()}
        {
        }
};

struct c_flags_umka
{
    public:
        umka::arr_t<umka::str_t> public_;
        umka::arr_t<umka::str_t> private_;
};

struct c_flags_cxx
{
    public:
        std::vector<std::string> public_ {};
        std::vector<std::string> private_ {};

        c_flags_cxx() = default;
        explicit c_flags_cxx(const c_flags_umka &raw)
            : public_ {raw.public_.data, raw.public_.data + raw.public_.len()},
              private_ {raw.private_.data, raw.private_.data + raw.private_.len()}
        {
        }
};

struct build_target_umka
{
    public:
        umka::str_t name;
        umka::arr_t<source_files_umka> srcs;
        umka::arr_t<gen_src_umka> gen_groups;
        umka::arr_t<umka::str_t> deps;
        cxx_flags_umka cxxflags;
        c_flags_umka cflags;
};

struct build_target_cxx
{
    public:
        std::string name {};
        std::vector<source_files_cxx> srcs {};
        std::vector<gen_src_cxx> gen_groups {};
        std::vector<std::string> deps {};
        cxx_flags_cxx cxxflags {};
        c_flags_cxx cflags {};

        explicit build_target_cxx(const build_target_umka &raw)
            : name {raw.name ? raw.name : ""}, deps {raw.deps.data, raw.deps.data + raw.deps.len()},
              cxxflags {raw.cxxflags}, cflags {raw.cflags}
        {
            const auto slen = raw.srcs.len();
            srcs.reserve(static_cast<std::size_t>(slen));
            for (umka::int_t i = 0; i < slen; ++i)
            {
                srcs.emplace_back(raw.srcs.data[i]);
            }

            const auto glen = raw.gen_groups.len();
            gen_groups.reserve(static_cast<std::size_t>(glen));
            for (umka::int_t i = 0; i < glen; ++i)
            {
                gen_groups.emplace_back(raw.gen_groups.data[i]);
            }
        }
};

struct archive_target_umka
{
    public:
        umka::str_t name;
        umka::arr_t<umka::str_t> deps;
        umka::arr_t<umka::str_t> arflags;
};

struct archive_target_cxx
{
    public:
        std::string name {};
        std::vector<std::string> deps {};
        std::vector<std::string> arflags {};

        explicit archive_target_cxx(const archive_target_umka &raw)
            : name {raw.name ? raw.name : ""}, deps {raw.deps.data, raw.deps.data + raw.deps.len()},
              arflags {raw.arflags.data, raw.arflags.data + raw.arflags.len()}
        {
        }
};

struct link_target_umka
{
    public:
        umka::str_t name;
        umka::str_t kind;
        umka::arr_t<umka::str_t> deps;
        umka::arr_t<umka::str_t> ldflags;
};

struct link_target_cxx
{
    public:
        std::string name {};
        std::string kind {};
        std::vector<std::string> deps {};
        std::vector<std::string> ldflags {};

        explicit link_target_cxx(const link_target_umka &raw)
            : name {raw.name ? raw.name : ""}, kind {raw.kind ? raw.kind : ""},
              deps {raw.deps.data, raw.deps.data + raw.deps.len()},
              ldflags {raw.ldflags.data, raw.ldflags.data + raw.ldflags.len()}
        {
        }
};

struct install_target_umka
{
    public:
        umka::str_t name;
        umka::str_t install_dir;
        umka::arr_t<umka::str_t> files;
        umka::arr_t<umka::str_t> build_targets;
        umka::arr_t<umka::str_t> archive_targets;
        umka::arr_t<umka::str_t> link_targets;
};

struct install_target_cxx
{
    public:
        std::string name {};
        std::filesystem::path install_dir {};
        std::vector<std::string> files {};
        std::vector<std::string> build_targets {};
        std::vector<std::string> archive_targets {};
        std::vector<std::string> link_targets {};

        explicit install_target_cxx(const install_target_umka &raw)
            : name {raw.name ? raw.name : ""}, install_dir {raw.install_dir ? raw.install_dir : ""},
              files {raw.files.data, raw.files.data + raw.files.len()},
              build_targets {raw.build_targets.data, raw.build_targets.data + raw.build_targets.len()},
              archive_targets {raw.archive_targets.data, raw.archive_targets.data + raw.archive_targets.len()},
              link_targets {raw.link_targets.data, raw.link_targets.data + raw.link_targets.len()}
        {
        }
};

export namespace cppbuild::modules_cppbuild
{
    umka::module_t mod {"cppbuild.um", src, {}};

    struct results_umka
    {
        public:
            umka::arr_t<build_target_umka> build_targets;
            umka::arr_t<archive_target_umka> archive_targets;
            umka::arr_t<link_target_umka> link_targets;
            umka::arr_t<install_target_umka> install_targets;
    };

    struct results_cxx
    {
        public:
            std::vector<build_target_cxx> build_targets {};
            std::vector<archive_target_cxx> archive_targets {};
            std::vector<link_target_cxx> link_targets {};
            std::vector<install_target_cxx> install_targets {};

            // The wrapper never releases anything implicitly, so the four
            // top-level arrays are released here once their contents have been
            // copied out. Releasing an outer array releases the inner ones too,
            // which is why only these four are named.
            results_cxx(const umka::vm_t &vm, results_umka raw)
            {
                const auto blen = raw.build_targets.len();
                build_targets.reserve(static_cast<std::size_t>(blen));
                for (umka::int_t i = 0; i < blen; ++i)
                {
                    build_targets.emplace_back(raw.build_targets.data[i]);
                }
                vm.decref(raw.build_targets);

                const auto alen = raw.archive_targets.len();
                archive_targets.reserve(static_cast<std::size_t>(alen));
                for (umka::int_t i = 0; i < alen; ++i)
                {
                    archive_targets.emplace_back(raw.archive_targets.data[i]);
                }
                vm.decref(raw.archive_targets);

                const auto llen = raw.link_targets.len();
                link_targets.reserve(static_cast<std::size_t>(llen));
                for (umka::int_t i = 0; i < llen; ++i)
                {
                    link_targets.emplace_back(raw.link_targets.data[i]);
                }
                vm.decref(raw.link_targets);

                const auto ilen = raw.install_targets.len();
                install_targets.reserve(static_cast<std::size_t>(ilen));
                for (umka::int_t i = 0; i < ilen; ++i)
                {
                    install_targets.emplace_back(raw.install_targets.data[i]);
                }
                vm.decref(raw.install_targets);
            }

            auto print() const -> void
            {
                std::println("=== Build Targets ({}) ===", build_targets.size());
                for (const auto &bt : build_targets)
                {
                    std::println("  name: {}", bt.name);
                    std::println("  deps: {}", bt.deps.size());
                    for (const auto &d : bt.deps)
                    {
                        std::println("    - {}", d);
                    }
                    std::println("  srcs: {}", bt.srcs.size());
                    for (const auto &sf : bt.srcs)
                    {
                        std::println("    kind: {}", sf.kind);
                        for (const auto &s : sf.srcs)
                        {
                            std::println("      - {}", s.string());
                        }
                    }
                }

                std::println("=== Archive Targets ({}) ===", archive_targets.size());
                for (const auto &at : archive_targets)
                {
                    std::println("  name: {}", at.name);
                    for (const auto &d : at.deps)
                    {
                        std::println("    - {}", d);
                    }
                }

                std::println("=== Link Targets ({}) ===", link_targets.size());
                for (const auto &lt : link_targets)
                {
                    std::println("  name: {}", lt.name);
                    for (const auto &d : lt.deps)
                    {
                        std::println("    - {}", d);
                    }
                }

                std::println("=== Install Targets ({}) ===", install_targets.size());
                for (const auto &it : install_targets)
                {
                    std::println("  install_dir: {}", it.install_dir.string());
                    for (const auto &bt : it.build_targets)
                    {
                        std::println("    build: {}", bt);
                    }
                    for (const auto &at : it.archive_targets)
                    {
                        std::println("    archive: {}", at);
                    }
                    for (const auto &lt : it.link_targets)
                    {
                        std::println("    link: {}", lt);
                    }
                }
            }
    };
} // namespace cppbuild::modules_cppbuild
