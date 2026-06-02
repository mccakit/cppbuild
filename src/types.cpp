module;
export module cppbuild:types;
import std;
import fmt;
import graaf;
import glaze;
import zpp.bits;
import :modules_cppbuild;

export namespace cppbuild::types
{
    class source_group
    {
        public:
            std::string kind;
            std::vector<std::filesystem::path> srcs;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                std::vector<std::string> srcs_str;
                if constexpr (archive.kind() == zpp::bits::kind::out)
                {
                    srcs_str.reserve(self.srcs.size());
                    for (const auto &s : self.srcs)
                    {
                        srcs_str.push_back(s.string());
                    }
                }
                auto result = archive(self.kind, srcs_str);
                if constexpr (archive.kind() == zpp::bits::kind::in)
                {
                    self.srcs.clear();
                    self.srcs.reserve(srcs_str.size());
                    for (const auto &s : srcs_str)
                    {
                        self.srcs.emplace_back(s);
                    }
                }
                return result;
            }
    };

    class gen_output
    {
        public:
            std::filesystem::path path;
            std::string kind;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                std::string path_str;
                if constexpr (archive.kind() == zpp::bits::kind::out)
                {
                    path_str = self.path.string();
                }
                auto result = archive(path_str, self.kind);
                if constexpr (archive.kind() == zpp::bits::kind::in)
                {
                    self.path = path_str;
                }
                return result;
            }
    };

    class gen_group
    {
        public:
            std::vector<std::string> command;
            std::vector<std::filesystem::path> inputs;
            std::vector<gen_output> outputs;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                std::vector<std::string> inputs_str;
                if constexpr (archive.kind() == zpp::bits::kind::out)
                {
                    inputs_str.reserve(self.inputs.size());
                    for (const auto &inp : self.inputs)
                    {
                        inputs_str.push_back(inp.string());
                    }
                }
                auto result = archive(self.command, inputs_str, self.outputs);
                if constexpr (archive.kind() == zpp::bits::kind::in)
                {
                    self.inputs.clear();
                    self.inputs.reserve(inputs_str.size());
                    for (const auto &inp : inputs_str)
                    {
                        self.inputs.emplace_back(inp);
                    }
                }
                return result;
            }
    };

    class cxx_flags
    {
        public:
            std::vector<std::string> public_;
            std::vector<std::string> private_;
    };

    class c_flags
    {
        public:
            std::vector<std::string> public_ {};
            std::vector<std::string> private_ {};
    };

    class build_target
    {
        public:
            std::string name;
            std::vector<source_group> srcs;
            std::vector<gen_group> gen_groups;
            std::vector<std::string> deps;
            cxx_flags cxxflags;
            c_flags cflags;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.name, self.srcs, self.gen_groups, self.deps, self.cxxflags, self.cflags);
            }

            auto print() const -> void
            {
                fmt::println("name: {}", name);
                fmt::println("  deps: [{}]", fmt::join(deps, ", "));
                fmt::println("  cxxflags.public:  [{}]", fmt::join(cxxflags.public_, " "));
                fmt::println("  cxxflags.private: [{}]", fmt::join(cxxflags.private_, " "));
                fmt::println("  cflags.public:    [{}]", fmt::join(cflags.public_, " "));
                fmt::println("  cflags.private:   [{}]", fmt::join(cflags.private_, " "));
                fmt::println("  srcs ({}):", srcs.size());
                for (const auto &sg : srcs)
                {
                    fmt::println("    kind: {}", sg.kind);
                    for (const auto &src : sg.srcs)
                    {
                        fmt::println("      - {}", src.string());
                    }
                }
                fmt::println("  gen_groups ({}):", gen_groups.size());
                for (const auto &gg : gen_groups)
                {
                    fmt::println("    command: [{}]", fmt::join(gg.command, " "));
                    for (const auto &out : gg.outputs)
                    {
                        fmt::println("      - path={} kind={}", out.path.string(), out.kind);
                    }
                }
            }
    };

    class archive_target
    {
        public:
            std::string name;
            std::vector<std::string> deps;
            std::vector<std::string> arflags;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.name, self.deps, self.arflags);
            }
    };

    class link_target
    {
        public:
            std::string name;
            std::string kind;
            std::vector<std::string> deps;
            std::vector<std::string> ldflags;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.name, self.kind, self.deps, self.ldflags);
            }
    };

    class install_target
    {
        public:
            std::string name;
            std::filesystem::path install_dir;
            std::vector<std::string> files;
            std::vector<std::string> build_targets;
            std::vector<std::string> archive_targets;
            std::vector<std::string> link_targets;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                std::string install_dir_str;
                if constexpr (archive.kind() == zpp::bits::kind::out)
                {
                    install_dir_str = self.install_dir.string();
                }
                auto result = archive(self.name,
                                      install_dir_str,
                                      self.files,
                                      self.build_targets,
                                      self.archive_targets,
                                      self.link_targets);
                if constexpr (archive.kind() == zpp::bits::kind::in)
                {
                    self.install_dir = install_dir_str;
                }
                return result;
            }
    };

    class toolchain
    {
        public:
            std::string cxx_compiler;
            std::string c_compiler;
            std::string archiver;
            std::string cxx_scanner;
            std::string libcxx_modules_manifest;
            std::vector<std::string> cxxflags;
            std::vector<std::string> cflags;
            std::vector<std::string> arflags;
            std::vector<std::string> exe_ldflags;
            std::vector<std::string> shared_ldflags;

            auto parse(const std::filesystem::path &path) -> void
            {
                const auto tc_dir = std::filesystem::absolute(path).parent_path();
                auto prefix_if_relative = [&](const std::string &val) -> std::string {
                    const std::filesystem::path p(val);
                    if (p.is_absolute() || p.parent_path().empty())
                    {
                        return val;
                    }
                    return (tc_dir / p).lexically_normal().string();
                };

                std::string buffer;
                {
                    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
                    const auto size = ifs.tellg();
                    ifs.seekg(0);
                    buffer.resize(static_cast<std::size_t>(size));
                    ifs.read(buffer.data(), size);
                }

                glz::generic json {};
                if (auto ec = glz::read_json(json, buffer); ec)
                {
                    throw std::runtime_error(fmt::format("failed to parse toolchain: {}", path.string()));
                }

                auto &obj = json.get<glz::generic::object_t>();

                auto try_string = [&](const std::string &key, std::string &dest) {
                    if (auto it = obj.find(key); it != obj.end())
                    {
                        dest = prefix_if_relative(it->second.get<std::string>());
                    }
                };

                auto parse_flags = [&](const std::string &key) -> std::vector<std::string> {
                    std::vector<std::string> out;
                    if (auto it = obj.find(key); it != obj.end())
                    {
                        auto &arr = it->second.get<glz::generic::array_t>();
                        out.reserve(arr.size());
                        for (auto &f : arr)
                        {
                            out.emplace_back(f.get<std::string>());
                        }
                    }
                    return out;
                };

                try_string("cxx_compiler", cxx_compiler);
                try_string("c_compiler", c_compiler);
                try_string("archiver", archiver);
                try_string("cxx_scanner", cxx_scanner);
                try_string("libcxx_modules_manifest", libcxx_modules_manifest);

                cxxflags = parse_flags("cxxflags");
                cflags = parse_flags("cflags");
                arflags = parse_flags("arflags");
                exe_ldflags = parse_flags("exe_ldflags");
                shared_ldflags = parse_flags("shared_ldflags");
            }

            auto print() const -> void
            {
                fmt::println("cxx_compiler:            {}", cxx_compiler);
                fmt::println("c_compiler:              {}", c_compiler);
                fmt::println("archiver:                {}", archiver);
                fmt::println("cxx_scanner:             {}", cxx_scanner);
                fmt::println("libcxx_modules_manifest: {}", libcxx_modules_manifest);
                fmt::println("cxxflags:                {}", fmt::join(cxxflags, " "));
                fmt::println("cflags:                  {}", fmt::join(cflags, " "));
                fmt::println("arflags:                 {}", fmt::join(arflags, " "));
                fmt::println("exe_ldflags:             {}", fmt::join(exe_ldflags, " "));
                fmt::println("shared_ldflags:          {}", fmt::join(shared_ldflags, " "));
            }

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.cxx_compiler,
                               self.c_compiler,
                               self.archiver,
                               self.cxx_scanner,
                               self.libcxx_modules_manifest,
                               self.cxxflags,
                               self.cflags,
                               self.arflags,
                               self.exe_ldflags,
                               self.shared_ldflags);
            }

            auto save(const std::filesystem::path &path) const -> void
            {
                auto [data, out] = zpp::bits::data_out();
                out(*this).or_throw();
                std::ofstream f(path, std::ios::binary);
                f.write(reinterpret_cast<const char *>(data.data()), data.size());
            }

            static auto load(const std::filesystem::path &path) -> toolchain
            {
                std::ifstream ifs(path, std::ios::binary | std::ios::ate);
                const auto size = ifs.tellg();
                ifs.seekg(0);
                std::vector<std::byte> data(static_cast<std::size_t>(size));
                ifs.read(reinterpret_cast<char *>(data.data()), size);
                toolchain tc;
                auto in = zpp::bits::in(data);
                in(tc).or_throw();
                return tc;
            }
    };

    class build_graph
    {
        public:
            graaf::directed_graph<build_target, int> graph {};
            std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
            std::vector<graaf::vertex_id_t> topo_order;
            std::vector<archive_target> archive_targets;
            std::vector<link_target> link_targets;
            std::vector<install_target> install_targets;

            auto parse(modules_cppbuild::results_cxx results,
                       const std::filesystem::path &script_path,
                       const std::filesystem::path &build_dir) -> void
            {
                std::string target_name {};
                target_name.reserve(256);
                const std::filesystem::path src_dir = script_path.parent_path();

                name_to_id.reserve(results.build_targets.size());
                for (auto &bt_raw : results.build_targets)
                {
                    types::build_target target {.name = std::move(bt_raw.name),
                                                .deps = std::move(bt_raw.deps),
                                                .cxxflags = {.public_ = std::move(bt_raw.cxxflags.public_),
                                                             .private_ = std::move(bt_raw.cxxflags.private_)},
                                                .cflags = {.public_ = std::move(bt_raw.cflags.public_),
                                                           .private_ = std::move(bt_raw.cflags.private_)}};

                    target.srcs.reserve(bt_raw.srcs.size());
                    for (auto &sg_raw : bt_raw.srcs)
                    {
                        for (auto &src : sg_raw.srcs)
                        {
                            src = std::filesystem::weakly_canonical(src_dir / src);
                        }
                        target.srcs.push_back(
                            types::source_group {.kind = std::move(sg_raw.kind), .srcs = std::move(sg_raw.srcs)});
                    }

                    target.gen_groups.reserve(bt_raw.gen_groups.size());
                    for (auto &gg_raw : bt_raw.gen_groups)
                    {
                        types::gen_group gg;
                        gg.command = std::move(gg_raw.command);
                        gg.inputs.reserve(gg_raw.inputs.size());
                        for (auto &inp : gg_raw.inputs)
                        {
                            gg.inputs.push_back(std::filesystem::weakly_canonical(src_dir / inp));
                        }
                        gg.outputs.reserve(gg_raw.outputs.size());
                        for (auto &out : gg_raw.outputs)
                        {
                            gg.outputs.push_back({.path = build_dir / out.path, .kind = std::move(out.kind)});
                        }
                        target.gen_groups.push_back(std::move(gg));
                    }

                    target_name = target.name;
                    name_to_id[target_name] = graph.add_vertex(std::move(target));
                    target_name.clear();
                }

                for (const auto &[name, id] : name_to_id)
                {
                    const auto &vertex_data = graph.get_vertex(id);
                    for (const auto &dep_name : vertex_data.deps)
                    {
                        if (auto it = name_to_id.find(dep_name); it != name_to_id.end())
                        {
                            graph.add_edge(id, it->second, 1);
                        }
                    }
                }

                archive_targets.reserve(results.archive_targets.size());
                for (auto &at_raw : results.archive_targets)
                {
                    archive_targets.push_back({.name = std::move(at_raw.name),
                                               .deps = std::move(at_raw.deps),
                                               .arflags = std::move(at_raw.arflags)});
                }

                link_targets.reserve(results.link_targets.size());
                for (auto &lt_raw : results.link_targets)
                {
                    link_targets.push_back({.name = std::move(lt_raw.name),
                                            .kind = std::move(lt_raw.kind),
                                            .deps = std::move(lt_raw.deps),
                                            .ldflags = std::move(lt_raw.ldflags)});
                }

                install_targets.reserve(results.install_targets.size());
                for (auto &it_raw : results.install_targets)
                {
                    std::vector<std::string> files;
                    files.reserve(it_raw.files.size());
                    for (auto &f : it_raw.files)
                    {
                        files.push_back(std::filesystem::weakly_canonical(src_dir / f).string());
                    }

                    install_targets.push_back({.name = std::move(it_raw.name),
                                               .install_dir = std::move(it_raw.install_dir),
                                               .files = std::move(files),
                                               .build_targets = std::move(it_raw.build_targets),
                                               .archive_targets = std::move(it_raw.archive_targets),
                                               .link_targets = std::move(it_raw.link_targets)});
                }
            }

            auto order() -> bool
            {
                auto result = graaf::algorithm::dfs_topological_sort(graph);
                if (!result)
                {
                    fmt::print("Cycle detected in build graph\n");
                    std::terminate();
                }
                topo_order = std::move(result.value());
                return true;
            }
    };

    class cxx_module
    {
        public:
            std::string logical_name;
            std::filesystem::path source_path;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                std::string sp;
                if constexpr (archive.kind() == zpp::bits::kind::out)
                {
                    sp = self.source_path.string();
                }
                auto result = archive(self.logical_name, sp);
                if constexpr (archive.kind() == zpp::bits::kind::in)
                {
                    self.source_path = sp;
                }
                return result;
            }

            auto print() const -> void
            {
                fmt::println("{} -> {}", logical_name, source_path.string());
            }
    };

    class dyndep_entry
    {
        public:
            cxx_module src;
            std::vector<std::string> deps;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.src, self.deps);
            }

            auto save(const std::filesystem::path &path) const -> void
            {
                auto [data, out] = zpp::bits::data_out();
                out(*this).or_throw();
                std::ofstream f(path, std::ios::binary);
                f.write(reinterpret_cast<const char *>(data.data()), data.size());
            }

            auto static load(const std::filesystem::path &path) -> dyndep_entry
            {
                std::ifstream ifs(path, std::ios::binary | std::ios::ate);
                const auto size = ifs.tellg();
                ifs.seekg(0);
                std::vector<std::byte> data(static_cast<std::size_t>(size));
                ifs.read(reinterpret_cast<char *>(data.data()), size);
                dyndep_entry entry {};
                auto in = zpp::bits::in(data);
                in(entry).or_throw();
                return entry;
            }

            auto print() const -> void
            {
                src.print();
                for (auto const &dep : deps)
                {
                    fmt::print("  {}\n", dep);
                }
            }

            auto static load_from_buffer(std::span<const std::byte> data) -> dyndep_entry
            {
                dyndep_entry entry {};
                auto in = zpp::bits::in(data);
                in(entry).or_throw();
                return entry;
            }
    };

    class build_cache
    {
        public:
            std::vector<build_target> build_targets;
            std::vector<archive_target> archive_targets;
            std::vector<link_target> link_targets;

            constexpr static auto serialize(auto &archive, auto &self)
            {
                return archive(self.build_targets, self.archive_targets, self.link_targets);
            }

            auto save(const std::filesystem::path &path) const -> void
            {
                auto [data, out] = zpp::bits::data_out();
                out(*this).or_throw();
                std::ofstream f(path, std::ios::binary);
                f.write(reinterpret_cast<const char *>(data.data()), data.size());
            }

            static auto load(const std::filesystem::path &path) -> build_cache
            {
                std::ifstream ifs(path, std::ios::binary | std::ios::ate);
                const auto size = ifs.tellg();
                ifs.seekg(0);
                std::vector<std::byte> data(static_cast<std::size_t>(size));
                ifs.read(reinterpret_cast<char *>(data.data()), size);
                build_cache result;
                auto in = zpp::bits::in(data);
                in(result).or_throw();
                return result;
            }
    };
} // namespace cppbuild::types
