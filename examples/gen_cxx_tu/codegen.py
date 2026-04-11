import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument("--inputs", nargs="+", default=[])
parser.add_argument("--outputs", nargs="+", default=[])
args = parser.parse_args()

total = 0
for inp in args.inputs:
    try:
        with open(inp, "r") as f:
            total += int(f.read().strip())
    except:
        pass

for out in args.outputs:
    filename = os.path.basename(out)
    with open(out, "w") as f:
        if filename == "d.hpp":
            f.write("#pragma once\n")
            f.write("int d_value();\n")
        elif filename == "d.cpp":
            f.write('#include "d.hpp"\n')
            f.write(f"int d_value() {{ return {total}; }}\n")
        elif filename == "c.hpp":
            f.write("#pragma once\n")
            f.write("int c_value();\n")
        elif filename == "c.cpp":
            f.write('#include "c.hpp"\n')
            f.write('#include "d.hpp"\n')
            f.write("int c_value() { return d_value() + 2; }\n")
        elif filename == "b.hpp":
            f.write("#pragma once\n")
            f.write("int b_value();\n")
        elif filename == "b.cpp":
            f.write('#include "b.hpp"\n')
            f.write('#include "d.hpp"\n')
            f.write("int b_value() { return d_value() + 1; }\n")
        elif filename == "a.hpp":
            f.write("#pragma once\n")
            f.write("int run();\n")
        elif filename == "a.cpp":
            f.write('#include "a.hpp"\n')
            f.write('#include "b.hpp"\n')
            f.write('#include "c.hpp"\n')
            f.write("int run() { return b_value() + c_value(); }\n")
