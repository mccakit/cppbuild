module;
#include <simdjson.h>

export module cppbuild:helpers;
import std;
import fmt;
import graaf;


import :types;
export namespace cppbuild
{
    using namespace cppbuild;
    auto to_views(const std::vector<std::string> &vec) -> std::vector<std::string_view>
    {
        return {vec.begin(), vec.end()};
    }

    auto quote_flags(const std::vector<std::string> &flags) -> std::string
    {
        std::string result;
        for (size_t i = 0; i < flags.size(); i++)
        {
            result += fmt::format("\"{}\"", flags[i]);
            if (i + 1 < flags.size())
            {
                result += ",";
            }
        }
        return result;
    }
}
