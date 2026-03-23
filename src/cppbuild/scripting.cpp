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
            script(std::string_view script_path)
                : umka{script_path.data(), 8 * 1024 * 1024, {modules_cppbuild::mod}}
            {
            }

            auto run() const -> modules_cppbuild::results_cxx
            {
                return modules_cppbuild::results_cxx{umka.vm, umka.call<modules_cppbuild::results_umka>("configure")};
            }
    };
} // namespace cppbuild
