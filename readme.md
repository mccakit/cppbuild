# cppbuild

A minimal C++ build system with an [Umka](https://github.com/vtereshkov/umka-lang) scripting frontend and a C++ backend that generates [Ninja](https://ninja-build.org/) build files.

Built as a direct response to the bloat and overreach of existing tools like CMake and Meson. cppbuild has a defined scope: C and C++, honest flag handling (flags pass through verbatim), no dependency fetching, no invoking other build systems, and a statically typed configuration language. Designed from the ground up for C++26 modules.

## Build

Install dependencies:
```sh
conan install . --build=missing --profile=native -of ./conan --deployer=full_deploy --envs-generation=false
```

Configure and build:
```sh
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=... && cmake --build build && cmake --install build
```

## Example
```umka
import (
    "std.um"
    "cppbuild.um"
)
fn configure*(): cppbuild::result {
    cppbuild::target({
        name:  "target0",
        kind:  "exe",
        sources: {
            {kind: "named_module",     files: {"m4.c++", "m3.cppm", "m2.cc", "m1.cpp"}},
            {kind: "translation_unit", files: {"app0.cpp"}}
        }
    })
    cppbuild::target({
        name:  "myshared",
        kind:  "shared",
        sources: {
            {kind: "translation_unit", files: {"lib0.cpp"}}
        }
    })
    cppbuild::install({
        targets: {{dir: "lib",     targets: {"myshared"}}},
        sources: {{dir: "include", files:   {"h1.hpp"}}}
    })
    return cppbuild::build_config
}
```

Build it:
```sh
cd examples/ex0
cppbuild --mode=configure --src_dir=. --build_dir=build --toolchain=./tc.json
cppbuild --mode=build --build_dir=build
cppbuild --mode=install --build_dir=build
```
