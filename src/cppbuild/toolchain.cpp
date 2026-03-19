module;
#include <simdjson.h>

export module cppbuild:toolchain;
import std;
import fmt;
import :types;

export namespace cppbuild
{
    using namespace cppbuild;
    auto parse_toolchain(const std::filesystem::path &path, toolchain &tc) -> void
    {
        simdjson::dom::parser parser;
        auto doc = parser.load(path.string());
        if (auto val = doc["cxx_compiler"]; val.error() == simdjson::SUCCESS)
        {
            tc.cxx_compiler = std::string(val.get_string().value());
        }
        if (auto val = doc["c_compiler"]; val.error() == simdjson::SUCCESS)
        {
            tc.c_compiler = std::string(val.get_string().value());
        }
        if (auto val = doc["archiver"]; val.error() == simdjson::SUCCESS)
        {
            tc.archiver = std::string(val.get_string().value());
        }
        if (auto val = doc["cxx_scanner"]; val.error() == simdjson::SUCCESS)
        {
            tc.cxx_scanner = std::string(val.get_string().value());
        }
        auto parse_flags = [&](std::string_view key, std::vector<std::string> &out) {
            simdjson::dom::array arr;
            if (doc[key].get(arr) == simdjson::SUCCESS)
            {
                out.clear();
                for (auto f : arr)
                {
                    out.push_back(std::string(f.get_string().value()));
                }
            }
        };
        parse_flags("cxxflags", tc.cxxflags);
        parse_flags("cflags", tc.cflags);
        parse_flags("exe_ldflags", tc.exe_ldflags);
        parse_flags("shared_ldflags", tc.shared_ldflags);
    }
}
