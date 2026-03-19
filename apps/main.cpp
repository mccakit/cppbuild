#include "cxxopts.hpp"

import std;
import fmt;
import cppbuild;

auto main(int argc, char **argv) -> int
{
    using namespace cppbuild;
    cxxopts::Options options{"cppbuild", "C++ build system"};
    auto opts = options.add_options();
    opts("mode", "Program mode", cxxopts::value<std::string>());
    opts("build_dir", "Build directory", cxxopts::value<std::string>());
    opts("src_dir", "Source directory", cxxopts::value<std::string>());
    opts("toolchain", "Toolchain file", cxxopts::value<std::string>());
    opts("prefix", "Install prefix", cxxopts::value<std::string>());
    opts("compdb_path", "Compilation database path", cxxopts::value<std::string>());
    cxxopts::ParseResult result = options.parse(argc, argv);
    std::filesystem::path self_path = std::filesystem::weakly_canonical(argv[0]);
    std::string mode = result["mode"].as<std::string>();
    if (mode == "configure")
    {
        return configure({.src_dir = result["src_dir"].as<std::string>(),
                          .build_dir = result["build_dir"].as<std::string>(),
                          .toolchain_path = result["toolchain"].as<std::string>(),
                          .self_path = self_path,
                          .prefix = std::filesystem::weakly_canonical(result["prefix"].as<std::string>())});
    }
    else if (mode == "scan")
    {
        generate_dyndep(result["compdb_path"].as<std::string>());
    }
    else if (mode == "reconfigure")
    {
        return reconfigure({.build_dir = result["build_dir"].as<std::string>(), .self_path = self_path});
    }
    else if (mode == "build")
    {
        if (reconfigure({.build_dir = result["build_dir"].as<std::string>(), .self_path = self_path}) != 0)
        {
            return 1;
        }
        return std::system(fmt::format("ninja -C {}", result["build_dir"].as<std::string>()).c_str());
    }
    else if (mode == "install")
    {
        return std::system(fmt::format("ninja -C {} -f install.ninja", result["build_dir"].as<std::string>()).c_str());
    }
    return 0;
}
