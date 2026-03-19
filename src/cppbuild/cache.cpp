module;
#include <simdjson.h>
export module cppbuild:cache;
import std;
import graaf;
import fmt;
import :types;
import :core;
import :helpers;
import :umka;
export namespace cppbuild
{
    using namespace cppbuild;
    auto save_cache(const toolchain &tc,
                    const graaf::directed_graph<target, int> &graph,
                    const std::filesystem::path &build_dir) -> void
    {
        fmt::ostream file = fmt::output_file((build_dir / "cache.json").string());
        file.print("{{\n");
        file.print("  \"toolchain\": {{\n");
        file.print("    \"cxx_compiler\": \"{}\",\n", tc.cxx_compiler);
        file.print("    \"c_compiler\": \"{}\",\n", tc.c_compiler);
        file.print("    \"archiver\": \"{}\",\n", tc.archiver);
        file.print("    \"cxxflags\": [{}],\n", quote_flags(tc.cxxflags));
        file.print("    \"cflags\": [{}],\n", quote_flags(tc.cflags));
        file.print("    \"exe_ldflags\": [{}],\n", quote_flags(tc.exe_ldflags));
        file.print("    \"shared_ldflags\": [{}]\n", quote_flags(tc.shared_ldflags));
        file.print("  }},\n");
        file.print("  \"targets\": [\n");
        bool first{true};
        for (const auto &[id, t] : graph.get_vertices())
        {
            if (!first)
            {
                file.print(",\n");
            }
            file.print("    {{\"name\":\"{}\",\"type\":\"{}\",\"srcs\":[", t.name, t.type);
            bool first_src{true};
            for (const auto &src : t.srcs)
            {
                if (!first_src)
                {
                    file.print(",");
                }
                file.print("\"{}\"", src.string());
                first_src = false;
            }
            file.print("],\"deps\":[");
            bool first_dep{true};
            for (const auto &dep : t.deps)
            {
                if (!first_dep)
                {
                    file.print(",");
                }
                file.print("\"{}\"", dep);
                first_dep = false;
            }
            file.print("]}}\n");
            first = false;
        }
        file.print("  ]\n");
        file.print("}}\n");
    }

    auto load_graph(const std::filesystem::path &path, const std::filesystem::path &src_dir) -> graph_result
    {
        simdjson::dom::parser parser;
        auto doc = parser.load(path.string());
        umka_cxx_result cxx_result;
        for (auto t : doc["targets"])
        {
            umka_cxx_build_target ct;
            ct.name = std::string(t["name"].get_string().value());
            ct.kind = std::string(t["type"].get_string().value());
            for (auto src : t["srcs"].get_array().value())
            {
                ct.srcs.push_back(std::string(src.get_string().value()));
            }
            for (auto dep : t["deps"].get_array().value())
            {
                ct.deps.push_back(std::string(dep.get_string().value()));
            }
            cxx_result.build_targets.push_back(std::move(ct));
        }
        return build_graph(cxx_result, src_dir);
    }

    auto load_cache(const std::filesystem::path &path) -> cache_result
    {
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        cache_result result{};
        if (parser.load(path.string()).get(doc) != simdjson::SUCCESS)
        {
            return result;
        }
        toolchain tc{};
        simdjson::dom::element tc_doc;
        if (doc["toolchain"].get(tc_doc) != simdjson::SUCCESS)
        {
            return result;
        }
        if (auto val = tc_doc["cxx_compiler"]; val.error() == simdjson::SUCCESS)
        {
            tc.cxx_compiler = std::string(val.get_string().value());
        }
        if (auto val = tc_doc["c_compiler"]; val.error() == simdjson::SUCCESS)
        {
            tc.c_compiler = std::string(val.get_string().value());
        }
        if (auto val = tc_doc["archiver"]; val.error() == simdjson::SUCCESS)
        {
            tc.archiver = std::string(val.get_string().value());
        }
        simdjson::dom::array arr;
        if (tc_doc["cxxflags"].get(arr) == simdjson::SUCCESS)
        {
            for (auto f : arr)
            {
                tc.cxxflags.push_back(std::string(f.get_string().value()));
            }
        }
        if (tc_doc["cflags"].get(arr) == simdjson::SUCCESS)
        {
            for (auto f : arr)
            {
                tc.cflags.push_back(std::string(f.get_string().value()));
            }
        }
        if (tc_doc["exe_ldflags"].get(arr) == simdjson::SUCCESS)
        {
            for (auto f : arr)
            {
                tc.exe_ldflags.push_back(std::string(f.get_string().value()));
            }
        }
        if (tc_doc["shared_ldflags"].get(arr) == simdjson::SUCCESS)
        {
            for (auto f : arr)
            {
                tc.shared_ldflags.push_back(std::string(f.get_string().value()));
            }
        }
        umka_cxx_result cxx_result;
        for (auto t : doc["targets"])
        {
            umka_cxx_build_target ct{};
            ct.name = std::string(t["name"].get_string().value());
            ct.kind = std::string(t["type"].get_string().value());
            for (auto src : t["srcs"].get_array().value())
            {
                ct.srcs.push_back(std::string(src.get_string().value()));
            }
            for (auto dep : t["deps"].get_array().value())
            {
                ct.deps.push_back(std::string(dep.get_string().value()));
            }
            cxx_result.build_targets.push_back(std::move(ct));
        }
        result.toolchain = tc;
        result.graph = build_graph(cxx_result, "");
        return result;
    }
} // namespace cppbuild::cache
