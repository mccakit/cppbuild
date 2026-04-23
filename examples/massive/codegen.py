#!/usr/bin/env python3
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
                out.write(f"export module {name};\n\n")
                out.write(f"export int {name}_val() {{ return {idx}; }}\n")
            else:
                dep = mod_name(level - 1, idx)
                out.write(f"export module {name};\n")
                out.write(f"import {dep};\n\n")
                out.write(f"export int {name}_val() {{ return {dep}_val() + 1; }}\n")
