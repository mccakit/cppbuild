module;
#include <fmt/base.h>
#include <fmt/os.h>
#include <fmt/ranges.h>
#include <simdjson.h>
#include <subprocess.h>

export module cppbuild.scanner;
import std;
import cppbuild.types;
export namespace cppbuild::scanner
{
    using namespace cppbuild;
    void write_dyndep(const std::filesystem::path &path)
    {
        std::string input;
        for (std::string line; std::getline(std::cin, line);)
        {
            input += line + "\n";
        }
        simdjson::dom::parser parser;
        auto doc = parser.parse(input);
        std::unordered_map<std::string, std::string> name_to_pcm;
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }
            for (auto p : provides)
            {
                std::string name = std::string(p["logical-name"].get_string().value());
                name_to_pcm[name] = name + ".pcm";
            }
        }
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path);
        out << "ninja_dyndep_version = 1\n";
        for (auto rule : doc["rules"])
        {
            simdjson::dom::array provides;
            if (rule["provides"].get(provides) != simdjson::SUCCESS)
            {
                continue;
            }
            std::string pcm = std::string(provides.at(0)["logical-name"].get_string().value()) + ".pcm";
            std::string deps;
            simdjson::dom::array requires_;
            if (rule["requires"].get(requires_) == simdjson::SUCCESS)
            {
                for (auto r : requires_)
                {
                    auto it = name_to_pcm.find(std::string(r["logical-name"].get_string().value()));
                    if (it != name_to_pcm.end())
                    {
                        deps += " " + it->second;
                    }
                }
            }
            out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
        }
    }
    auto run_scan_deps(const std::filesystem::path &output_dir, const types::toolchain &toolchain)
        -> std::string
    {
        const auto module_commands = (output_dir / "module_commands.json").string();
        const auto scanner = std::string{toolchain.cxx_scanner};
        const char *command_line[] = {
            scanner.c_str(), "-format=p1689", "-compilation-database", module_commands.c_str(), NULL};
        struct subprocess_s subprocess;
        int options =
            subprocess_option_search_user_path | subprocess_option_inherit_environment | subprocess_option_enable_async;
        if (subprocess_create(command_line, options, &subprocess) != 0)
        {
            return "";
        }
        std::string scan_output;
        char buf[4096];
        unsigned bytes_read;
        while ((bytes_read = subprocess_read_stdout(&subprocess, buf, sizeof(buf))) != 0)
        {
            scan_output.append(buf, bytes_read);
        }
        int process_return;
        subprocess_join(&subprocess, &process_return);
        subprocess_destroy(&subprocess);
        return scan_output;
    }
} // namespace cppbuild::scanner
