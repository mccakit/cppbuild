import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument("--inputs", nargs="+", default=[])
parser.add_argument("--outputs", nargs="+", default=[])
args = parser.parse_args()

input_texts = []
for inp in args.inputs:
    try:
        with open(inp, "r") as f:
            input_texts.append(f.read().strip().replace('"', '\\"'))
    except:
        input_texts.append("No data")

msg = " | ".join(input_texts) if input_texts else "No data"

for out in args.outputs:
    filename = os.path.basename(out)
    with open(out, "w") as f:
        if filename == "d.cpp":
            f.write("export module d;\n")
            f.write(f'export int d_value() {{ return {len(msg)}; }}\n')
        elif filename == "c.cpp":
            f.write("export module c;\n")
            f.write("import d;\n")
            f.write("export int c_value() { return d_value() + 2; }\n")
        elif filename == "b.cpp":
            f.write("export module b;\n")
            f.write("import d;\n")
            f.write("export int b_value() { return d_value() + 1; }\n")
        elif filename == "a.cpp":
            f.write("export module a;\n")
            f.write("import b;\n")
            f.write("import c;\n")
            f.write("export int run() { return b_value() + c_value(); }\n")
