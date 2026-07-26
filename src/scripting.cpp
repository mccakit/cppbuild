module;
export module cppbuild:scripting;
import std;
import umka;
import :modules_cppbuild;

export namespace cppbuild
{
    class script
    {
        public:
            umka::vm_t vm;

            // Looked up once, in declaration order after vm, so both handles
            // outlive nothing and are reused on every run().
            umka::fn_t set_config;
            umka::fn_t configure;

            // Stack size is counted in slots, not bytes: this is 16 MiB.
            explicit script(const std::filesystem::path &script_path)
                : vm {script_path, 2 * 1024 * 1024, {modules_cppbuild::mod}},
                  set_config {vm.function("cppbuild.um", "set_config")},
                  configure {vm.function(umka::main_module, "configure")}
            {
            }

            // Not const: calling into the VM mutates it.
            auto run(const std::filesystem::path &build_dir, const std::filesystem::path &script_dir)
                -> modules_cppbuild::results_cxx
            {
                // Held in locals rather than passed as temporaries: the wrapper
                // converts a const char* with umkaMakeStr, so the buffer must
                // outlive the argument expression.
                const std::string build = build_dir.string();
                const std::string dir = script_dir.string();

                set_config.call(build.c_str(), dir.c_str());
                return modules_cppbuild::results_cxx {vm, configure.call<modules_cppbuild::results_umka>()};
            }
    };
} // namespace cppbuild
