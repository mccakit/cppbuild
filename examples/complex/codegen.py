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
        if filename == "config.cppm":
            f.write("export module config;\n")
            f.write(f"export int config_value() {{ return {total}; }}\n")
