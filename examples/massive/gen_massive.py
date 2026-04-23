#!/usr/bin/env python3
"""Generate a massive C++ modules test case using gen_groups.

Structure: DEPTH levels, WIDTH modules per level.
Each module at level N imports one module from level N-1.
Level 0 modules have no imports.
All generated .cppm files are produced at build time by codegen.py.

Also includes mod.c++ as a named module source.

Total generated modules = DEPTH * WIDTH
"""

import os
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--depth", type=int, default=20)
parser.add_argument("--width", type=int, default=500)
parser.add_argument("--output-dir", type=str, default=".")
args = parser.parse_args()

DEPTH = args.depth
WIDTH = args.width
OUT = args.output_dir
TOTAL = DEPTH * WIDTH

os.makedirs(OUT, exist_ok=True)

def mod_name(level, idx):
    return f"m_{level:04d}_{idx:04d}"

codegen_path = os.path.join(OUT, "codegen.py")
with open(codegen_path, "w") as f:
    f.write('''#!/usr/bin/env python3
"""Build-time codegen: generates .cppm files for the massive test."""
import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument("--output-dir", required=True)
parser.add_argument("--depth", type=int, required=True)
parser.add_argument("--width", type=int, required=True)
args = parser.parse_args()

os.makedirs(args.output_dir, exist_ok=True)

def mod_name(level, idx):
    return f"m_{level:04d}_{idx:04d}"

for level in range(args.depth):
    for idx in range(args.width):
        name = mod_name(level, idx)
        path = os.path.join(args.output_dir, f"{name}.cppm")
        with open(path, "w") as out:
            if level == 0:
                out.write(f"export module {name};\\n\\n")
                out.write(f"export int {name}_val() {{ return {idx}; }}\\n")
            else:
                dep = mod_name(level - 1, idx)
                out.write(f"export module {name};\\n")
                out.write(f"import {dep};\\n\\n")
                out.write(f"export int {name}_val() {{ return {dep}_val() + 1; }}\\n")
''')

try:
    os.chmod(codegen_path, 0o755)
except OSError:
    pass

build_um_path = os.path.join(OUT, "build.um")
with open(build_um_path, "w") as f:
    f.write('import (\n')
    f.write('    "std.um"\n')
    f.write('    "cppbuild.um"\n')
    f.write(')\n\n')
    f.write('fn configure*(): cppbuild::results {\n')

    f.write('    cppbuild::add_build_target({\n')
    f.write('        name: "massive",\n')
    f.write('        srcs: {\n')
    f.write('            {kind: "named_module", srcs: {"mod.c++"}}\n')
    f.write('        },\n')
    f.write('        gen_groups: {\n')
    f.write('            {\n')
    f.write('                command: {\n')
    f.write('                    "python3",\n')
    f.write('                    cppbuild::script_dir + "/codegen.py",\n')
    f.write('                    "--output-dir", cppbuild::build_dir,\n')
    f.write(f'                    "--depth", "{DEPTH}",\n')
    f.write(f'                    "--width", "{WIDTH}"\n')
    f.write('                },\n')
    f.write('                inputs: {"codegen.py"},\n')
    f.write('                outputs: {\n')

    for level in range(DEPTH):
        for idx in range(WIDTH):
            name = mod_name(level, idx)
            is_last = level == DEPTH - 1 and idx == WIDTH - 1
            comma = "" if is_last else ","
            f.write(f'                    {{path: "{name}.cppm", kind: "named_module"}}{comma}\n')

    f.write('                }\n')
    f.write('            }\n')
    f.write('        },\n')
    f.write('        deps: {},\n')
    f.write('        cxxflags: {public: {}, private: {}},\n')
    f.write('        cflags: {public: {}, private: {}}\n')
    f.write('    })\n\n')

    f.write('    return cppbuild::build_config\n')
    f.write('}\n')

print(f"Generated {TOTAL} modules ({DEPTH} levels x {WIDTH} wide)")
print(f"  {os.path.relpath(codegen_path)}")
print(f"  {os.path.relpath(build_um_path)}")
