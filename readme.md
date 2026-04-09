# cppbuild

A minimal C++ build system with an [Umka](https://github.com/vtereshkov/umka-lang) scripting frontend and a C++ backend that generates [Ninja](https://ninja-build.org/) build files.

---

## Usage

Write an Umka script that returns a `cppbuild::results` value, then invoke the build system:

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

### Building

1. Download both the **examples** and **release** tarballs from the Releases page
2. Extract them and navigate to:

```
examples/mtest2
```

3. Run the build system:

```sh
{your_install_loc}/cppbuild configure \
    --script-path ./build.um \
    --build-dir ./build \
    --toolchain-path ./tc.json \
    --install-dir ./INSTALL

{your_install_loc}/ninja -C build
{your_install_loc}/ninja -f build/install.ninja
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
## Running the Example

Create a container and run this, it extracts llvm, cppbuild and cppbuild example tarballs and runs the example
```
mkdir workspace && cd workspace && \
export DEBIAN_FRONTEND=noninteractive && \
apt update >/dev/null 2>&1 && \
apt install -y pixz curl aria2 libstdc++6 libstdc++-12-dev zstd unzip python3 >/dev/null 2>&1 && \
aria2c -x 16 -s 16 -k 1M -q "https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.2/LLVM-22.1.2-Linux-X64.tar.xz" >/dev/null 2>&1 && \
curl -sSL -o cppbuild.tar.zst "https://codeberg.org/mccakit/cppbuild/releases/download/v0.0/cppbuild.tar.zst" >/dev/null 2>&1 && \
curl -sSL -o examples.tar.zst "https://codeberg.org/mccakit/cppbuild/releases/download/v0.0/examples.tar.zst" >/dev/null 2>&1 && \
curl -sSL -o ninja.zip "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip" >/dev/null 2>&1 && \
mkdir -p llvm examples && \
tar -I pixz -xf LLVM-22.1.2-Linux-X64.tar.xz -C llvm --strip-components=1 >/dev/null 2>&1 && \
tar --use-compress-program=unzstd -xf cppbuild.tar.zst >/dev/null 2>&1 && \
tar --use-compress-program=unzstd -xf examples.tar.zst -C examples --strip-components=1 >/dev/null 2>&1 && \
unzip -q ninja.zip >/dev/null 2>&1 && \
./llvm/bin/clang++ --version >/dev/null 2>&1 && \
./llvm/bin/clang --version >/dev/null 2>&1 && \
./llvm/bin/clang-scan-deps --version >/dev/null 2>&1 && \
./cppbuild configure --script-path ./examples/mtest2/build.um --build-dir ./examples/mtest2/build --toolchain-path ./examples/mtest2/tc.json --install-dir ./examples/mtest2/install_prefix && \
./ninja -C ./examples/mtest2/build && \
./ninja -f ./examples/mtest2/build/install.ninja
```
---

## Support

Tested with upstream Clang/LLVM 22 on Linux.

Release binaries are fully static and built with:

- musl libc (Kernel headers 6.1)
- LLVM `compiler-rt` and `libc++` from LLVM 23

Only this configuration is officially supported for Linux builds.

You _can_ build it yourself, but it is not recommended unless you are comfortable packaging dependencies (e.g., with Conan).

---

## Notes

- Linux distribution toolchains are not supported, you can't use clang-scan-deps from llvm 22 with llvm 23 p1689 databases. Just use the release tarball for llvm like a normal person

---

## Build

Conan recipes used for building:

https://github.com/mccakit/conan-recipes

### Dependencies

- libfmt — https://github.com/fmtlib/fmt
- neograaf — https://github.com/mccakit/neograaf
- simdjson — https://github.com/simdjson/simdjson
- umkacxx — https://github.com/mccakit/umkacxx
- cli11 — https://github.com/mccakit/cli11
- subprocess - https://github.com/mccakit/subprocess
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

## To-do

- import std with toolchain caching,taking a toolchain file as input and precompiling std library to some location to not rebuilt it every build.
- bundle ninja build binary somehow(any recommendation welcome)
- Switch dependencies of the project to C++ named modules
- Introduce 4 quadrant documentation
- Introduce unit testing
- Build project itself with release tarball
