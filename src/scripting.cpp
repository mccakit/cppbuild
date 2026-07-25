module;
export module cppbuild:scripting;
import std;
import umka;
import :modules_cppbuild;
export namespace cppbuild
{
    using namespace cppbuild;
    class script
    {
        public:
            umka::umka vm;

            // Stack size is counted in slots, not bytes: this is 16 MiB.
            script(const std::filesystem::path &script_path)
                : vm {script_path, 2 * 1024 * 1024, {modules_cppbuild::mod}}
            {
            }

            auto run(const std::filesystem::path &build_dir, const std::filesystem::path &script_dir) const
                -> modules_cppbuild::results_cxx
            {
                vm.call("cppbuild.um", "set_config", build_dir.string(), script_dir.string());
                return modules_cppbuild::results_cxx {vm.call<modules_cppbuild::results_umka>("", "configure")};
            }
    };
} // namespace cppbuild
