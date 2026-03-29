module;
export module cppbuild:scripting;
import std;
import umkacxx;
import :modules_cppbuild;

export namespace cppbuild
{
    using namespace cppbuild;
    class script
    {
        public:
            umkacxx::umka umka;
            script(const std::filesystem::path &script_path)
                : umka {script_path.string(), (2 * 1024 * 1024), {modules_cppbuild::mod}} // 2MB of Stack
            {
            }

            auto run(const std::filesystem::path &build_dir, const std::filesystem::path &script_dir) const
                -> modules_cppbuild::results_cxx
            {
                umka.call<void>(
                    "set_config",
                    "cppbuild.um",
                    {umka.make_str(build_dir.string().c_str()), umka.make_str(script_dir.string().c_str())});
                return modules_cppbuild::results_cxx {umka.vm, umka.call<modules_cppbuild::results_umka>("configure")};
            }
    };
} // namespace cppbuild
