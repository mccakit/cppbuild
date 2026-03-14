#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include "cxxopts.hpp"

import std;
import cppbuild.umka;
import cppbuild.types;
import cppbuild.compdb;
import cppbuild.ninja;
import cppbuild.toolchain;
import cppbuild.scanner;
import cppbuild.helpers;
import cppbuild.cache;
import cppbuild.core;
import cppbuild.configure;

auto main(int argc, char **argv) -> int
{
    using namespace cppbuild;
    cxxopts::Options options("cppbuild", "builds and links c and cpp sources");
    options.add_options()("mode", "Program mode", cxxopts::value<std::string>())(
        "build_dir", "Build directory", cxxopts::value<std::string>())(
        "src_dir", "Source directory", cxxopts::value<std::string>())(
        "toolchain", "Toolchain file", cxxopts::value<std::string>())(
        "compdb_path", "Compilation database path", cxxopts::value<std::string>());
    cxxopts::ParseResult result = options.parse(argc, argv);
    std::filesystem::path self_path = std::filesystem::weakly_canonical(argv[0]);
    std::string mode = result["mode"].as<std::string>();
    if (mode == "configure")
    {
        return cppbuild::configure::conf(
            result["src_dir"].as<std::string>(),
            result["build_dir"].as<std::string>(),
            result["toolchain"].as<std::string>(),
            self_path);
    }
    else if (mode == "scan")
    {
        cppbuild::scanner::write_dyndep(result["compdb_path"].as<std::string>());
    }
    else if (mode == "reconfigure")
    {
        return cppbuild::configure::reconf(result["build_dir"].as<std::string>(), self_path);
    }
    else if (mode == "build")
    {
        std::string build_dir = result["build_dir"].as<std::string>();
        if (cppbuild::configure::reconf(build_dir, self_path) != 0)
        {
            return 1;
        }
        return std::system(fmt::format("ninja -C {}", build_dir).c_str());
    }
    return 0;
}
