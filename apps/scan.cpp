#include <fstream>
#include <simdjson.h>
#include <string>
#include <unordered_map>

int main(int argc, char **argv)
{
    std::string input;
    for (std::string line; std::getline(std::cin, line);)
        input += line + "\n";

    simdjson::dom::parser parser;
    auto doc = parser.parse(input);

    std::unordered_map<std::string, std::string> name_to_pcm;
    for (auto rule : doc["rules"])
    {
        simdjson::dom::array provides;
        if (rule["provides"].get(provides) != simdjson::SUCCESS)
            continue;
        for (auto p : provides)
        {
            std::string name = std::string(p["logical-name"].get_string().value());
            name_to_pcm[name] = name + ".pcm";
        }
    }

    std::ofstream out(argv[1]);
    out << "ninja_dyndep_version = 1\n";

    for (auto rule : doc["rules"])
    {
        simdjson::dom::array provides;
        if (rule["provides"].get(provides) != simdjson::SUCCESS)
            continue;
        std::string pcm = std::string(provides.at(0)["logical-name"].get_string().value()) + ".pcm";
        std::string deps;
        simdjson::dom::array requires_;
        if (rule["requires"].get(requires_) == simdjson::SUCCESS)
            for (auto r : requires_)
            {
                auto it = name_to_pcm.find(std::string(r["logical-name"].get_string().value()));
                if (it != name_to_pcm.end())
                    deps += " " + it->second;
            }
        out << "build " << pcm << ": dyndep" << (deps.empty() ? "" : " |" + deps) << "\n";
    }
}
