# cppbuild
A minimal C++ build system with an [Umka](https://github.com/vtereshkov/umka-lang) scripting frontend and a C++ backend that generates [Ninja](https://ninja-build.org/) build files. Designed for C++26 modules. No dependency fetching, no invoking other build systems, flags pass through verbatim.

## Build
```sh
conan install . --build=missing --profile=native -of ./conan --deployer=full_deploy --envs-generation=false
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=... && cmake --build build && cmake --install build
```

## Usage
```sh
cppbuild configure --script-path ./build.um --build-dir ./build --toolchain-path ./tc.json --install-dir ./INSTALL
ninja -C build
ninja -f build/install.ninja
```

## Example
```umka
import (
    "std.um"
    "cppbuild.um"
)
fn configure*(): cppbuild::results {
    cppbuild::add_build_target({
        name: "target0",
        srcs: {
            {kind: "named_module",     srcs: {"tgt1_mod.cppm"}},
            {kind: "translation_unit", srcs: {"tgt1_src.cpp"}},
            {kind: "header_unit",      srcs: {"tgt1_hu.hpp"}},
        },
        cxxflags: {public: {"-I/path/to/include"}, private: {"-I."}},
    })
    cppbuild::add_build_target({
        name: "target1",
        srcs: {
            {kind: "named_module",     srcs: {"m4.c++", "m3.cppm", "m2.cc", "m1.cpp"}},
            {kind: "translation_unit", srcs: {"app0.cpp", "myc.c"}},
            {kind: "header_unit",      srcs: {"h1.hpp"}},
        },
        gen_groups: {
            {
                command: {
                    "python3", "codegen.py",
                    "--inputs",  "in0.txt", "in1.txt",
                    "--outputs", "/path/to/build/gen0.cpp", "/path/to/build/gen1.cpp",
                },
                inputs:  {"in0.txt", "in1.txt"},
                outputs: {
                    {path: "gen0.cpp", kind: "named_module"},
                    {path: "gen1.cpp", kind: "translation_unit"},
                },
            },
        },
        deps:     {"target0"},
        cxxflags: {public: {"-I/path/to/include"}, private: {"-I/path/to/build"}},
        cflags:   {public: {"-I/path/to/include"}, private: {"-I/path/to/build"}},
    })
    cppbuild::add_link_target({name: "myapp",    kind: "executable",     deps: {"target1"}})
    cppbuild::add_link_target({name: "mylib_s",  kind: "static_library", deps: {"target0"}})
    cppbuild::add_link_target({name: "mylib_so", kind: "shared_library", deps: {"target0"}})
    cppbuild::add_install_target({
        name:          "default",
        install_dir:   "lib",
        build_targets: {"target0", "target1"},
        link_targets:  {"myapp", "mylib_s", "mylib_so"},
        files:         {"tgt1_hu.hpp", "h1.hpp"},
    })
    return cppbuild::build_config
}
```

## Source kinds
- `named_module` — C++20 module interface units, compiled to `.pcm` then `.pcm.o`
- `translation_unit` — regular `.cpp` and `.c` files
- `header_unit` — importable headers

## Notes
- `deps` between build targets are resolved transitively for linking and header unit propagation
- `gen_groups` run before compilation; outputs declare their kind so cppbuild knows how to compile them
- `cxxflags`/`cflags` split into `public` (propagated to dependents) and `private` (local only)
- Module deps scanned via `clang-scan-deps` P1689, dyndep used for correct incremental ordering
- `install_dir` is relative to `--install-dir` prefix
