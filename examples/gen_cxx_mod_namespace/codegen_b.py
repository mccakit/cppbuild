import sys
import os

out_foo = sys.argv[1]
out_bar = sys.argv[2]

os.makedirs(os.path.dirname(out_foo), exist_ok=True)
os.makedirs(os.path.dirname(out_bar), exist_ok=True)

with open(out_foo, "w") as f:
    f.write("export module foo;\n")
    f.write("export int foo_val() { return 200; }\n")

with open(out_bar, "w") as f:
    f.write("export module bar;\n")
    f.write("import foo;\n")  # Relies on target-scoped module resolution
    f.write("export int bar_val() { return foo_val() + 2; }\n")
