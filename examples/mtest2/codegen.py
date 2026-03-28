import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument("--inputs", nargs="+", default=[])
parser.add_argument("--outputs", nargs="+", default=[])
args = parser.parse_args()

# Read content from the first input (int0.txt)
input_text = "No data"
if args.inputs:
    try:
        with open(args.inputs[0], "r") as f:
            input_text = f.read().strip().replace('"', '\\"')
    except:
        pass

for out in args.outputs:
    filename = os.path.basename(out)
    with open(out, "w") as f:
        # 1. THE NAMED MODULE (gen0.cpp)
        if filename == "gen0.cpp":
            f.write("module;\n")
            f.write("#include <iostream>\n")
            f.write("export module gen0;\n")
            f.write("export void print_module_msg() {\n")
            f.write(f'    std::cout << "Module gen0 says: {input_text}" << std::endl;\n')
            f.write("}\n")

        # 2. THE HEADER (gen1.hpp)
        elif filename == "gen1.hpp":
            f.write("#pragma once\n\n")
            f.write("namespace gen1 {\n")
            f.write("    void print_pair_msg();\n")
            f.write("}\n")

        # 3. THE IMPLEMENTATION (gen1.cpp)
        elif filename == "gen1.cpp":
            f.write('#include "gen1.hpp"\n')
            f.write("#include <iostream>\n\n")
            f.write("namespace gen1 {\n")
            f.write("    void print_pair_msg() {\n")
            f.write(f'        std::cout << "Header/Cpp pair says: {input_text}" << std::endl;\n')
            f.write("    }\n")
            f.write("}\n")
