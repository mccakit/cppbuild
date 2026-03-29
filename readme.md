# cppbuild

A minimal C++ build system with an [Umka](https://github.com/vtereshkov/umka-lang) scripting frontend and a C++ backend that generates [Ninja](https://ninja-build.org/) build files.

---

## Usage

Write an Umka script that returns a `cppbuild::results` value, then invoke the build system with the desired parameters:

```sh
cppbuild configure \
    --script-path ./build.um \
    --build-dir ./build \
    --toolchain-path ./tc.json \
    --install-dir ./INSTALL

ninja -C build
ninja -f build/install.ninja
```

---

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
            {kind: "named_module", srcs: {"tgt1_mod.cppm"}},
            {kind: "translation_unit", srcs: {"tgt1_src.cpp"}},
            {kind: "header_unit", srcs: {"tgt1_hu.hpp"}}
        },
        gen_groups: {},
        deps: {},
        cxxflags: {public: {"-I" + cppbuild::script_dir}},
        cflags: {public: {}, private: {}}
    })

    cppbuild::add_build_target({
        name: "target1",
        srcs: {
            {kind: "named_module", srcs: {"m4.c++", "m3.cppm", "m2.cc", "m1.cpp", "h1_wrap.cpp"}},
            {kind: "translation_unit", srcs: {"app0.cpp", "myc.c"}},
            {kind: "header_unit", srcs: {"h1.hpp"}}
        },
        gen_groups: {
            {
                command: {
                    "python3",
                    cppbuild::script_dir + "/codegen.py",
                    "--inputs",
                    cppbuild::script_dir + "/in0.txt",
                    cppbuild::script_dir + "/in1.txt",
                    "--outputs",
                    cppbuild::build_dir + "/gen0.cpp",
                    cppbuild::build_dir + "/gen1.cpp",
                    cppbuild::build_dir + "/gen1.hpp"
                },
                inputs: {"in0.txt", "in1.txt"},
                outputs: {
                    {path: "gen0.cpp", kind: "named_module"},
                    {path: "gen1.cpp", kind: "translation_unit"}
                }
            }
        },
        deps: {"target0"},
        cxxflags: {
            public: {"-I" + cppbuild::script_dir},
            private: {"-I" + cppbuild::build_dir}
        },
        cflags: {
            public: {"-I" + cppbuild::script_dir},
            private: {"-I" + cppbuild::build_dir}
        }
    })

    cppbuild::add_link_target({
        name: "target3",
        kind: "executable",
        deps: {"target1"}
    })

    cppbuild::add_link_target({
        name: "target4",
        kind: "static_library",
        deps: {"target0"}
    })

    cppbuild::add_link_target({
        name: "target5",
        kind: "shared_library",
        deps: {"target0"}
    })

    cppbuild::add_install_target({
        name: "default",
        install_dir: "./install",
        build_targets: {"target0", "target1"},
        link_targets: {"target3", "target4", "target5"},
        files: {"tgt1_hu.hpp", "h1.hpp"}
    })

    return cppbuild::build_config
}
```

---

## Overview

### Source kinds

- `named_module` — C++20 module interface units (compiled to `.pcm`, then `.pcm.o`)
- `translation_unit` — Regular `.cpp` and `.c` files
- `header_unit` — Importable headers

### Link kinds

- `executable` — Standalone binary
- `static_library` — Archived object files
- `shared_library` — Dynamically linked library

### API

`add_build_target()`, `add_link_target()`, and `add_install_target()` append to the `cppbuild::results` value. Return this value at the end of your script to let the backend generate the Ninja files.

Each function accepts a struct as input. See the full API definition here:

```
./src/cppbuild/modules/cppbuild/mod.um
```

---

## Support

Tested with upstream Clang/LLVM 22 on Linux.

Release binaries are fully static and built with:

- musl libc (Kernel headers 6.1)
- LLVM `compiler-rt` and `libc++` from LLVM 23

Only this configuration is officially supported for Linux builds.

You *can* build it yourself, but it is not recommended unless you are comfortable packaging dependencies (e.g., with Conan).

---

## Notes

- `deps` between build targets are resolved transitively for linking and header unit propagation
- `gen_groups` run before compilation; outputs declare their kind for proper handling
- `cxxflags` / `cflags` are split into:
  - `public` — propagated to dependents
  - `private` — local to the target
- Module dependencies are scanned via `clang-scan-deps` (P1689); `dyndep` ensures correct incremental builds
- `install_dir` is relative to the `--install-dir` prefix

---

## Build

Conan recipes used for building:

https://github.com/mccakit/conan-recipes

### Dependencies

- libfmt — https://github.com/fmtlib/fmt
- neograaf — https://github.com/mccakit/neograaf
- simdjson — https://github.com/simdjson/simdjson
- umkacxx — https://github.com/mccakit/umkacxx
- cli11 — https://github.com/CLIUtils/CLI11

### Build commands

```sh
conan install . \
    --build=missing \
    --profile=native \
    -of ./conan \
    --deployer=full_deploy \
    --envs-generation=false

cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=...
cmake --build build
cmake --install build
```
