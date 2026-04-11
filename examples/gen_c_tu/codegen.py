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
        if filename == "d.h":
            f.write("#pragma once\n")
            f.write("int d_value(void);\n")
        elif filename == "d.c":
            f.write('#include "d.h"\n')
            f.write(f"int d_value(void) {{ return {total}; }}\n")
        elif filename == "c.h":
            f.write("#pragma once\n")
            f.write("int c_value(void);\n")
        elif filename == "c.c":
            f.write('#include "c.h"\n')
            f.write('#include "d.h"\n')
            f.write("int c_value(void) { return d_value() + 2; }\n")
        elif filename == "b.h":
            f.write("#pragma once\n")
            f.write("int b_value(void);\n")
        elif filename == "b.c":
            f.write('#include "b.h"\n')
            f.write('#include "d.h"\n')
            f.write("int b_value(void) { return d_value() + 1; }\n")
        elif filename == "a.h":
            f.write("#pragma once\n")
            f.write("int run(void);\n")
        elif filename == "a.c":
            f.write('#include "a.h"\n')
            f.write('#include "b.h"\n')
            f.write('#include "c.h"\n')
            f.write("int run(void) { return b_value() + c_value(); }\n")
