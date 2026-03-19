module;
export module cppbuild:types;
import std;
import fmt;
import graaf;
export namespace cppbuild
{
    struct target
    {
        public:
            std::string name;
            std::vector<std::filesystem::path> srcs;
            std::map<std::string, std::string> src_to_mname;
            std::string type;
            std::vector<std::string> deps;
            std::vector<std::string> cxxflags;
            std::vector<std::string> cflags;
            std::vector<std::string> ldflags;
    };
    struct toolchain
    {
        public:
            std::string cxx_compiler = "clang++";
            std::string c_compiler = "clang";
            std::string archiver = "llvm-ar";
            std::string cxx_scanner = "clang-scan-deps";
            std::vector<std::string> cxxflags = {"-std=c++26", "-Wno-experimental-header-units", "-flto=thin"};
            std::vector<std::string> cflags = {"-std=c23", "-flto=thin"};
            std::vector<std::string> exe_ldflags{"-flto=thin"};
            std::vector<std::string> shared_ldflags{"-flto=thin"};
    };
    struct graph_result
    {
        public:
            graaf::directed_graph<cppbuild::target, int> g;
            std::unordered_map<std::string, graaf::vertex_id_t> name_to_id;
    };
    struct cache_result
    {
        public:
            cppbuild::graph_result graph;
            cppbuild::toolchain toolchain;
    };
} // namespace cppbuild::types
