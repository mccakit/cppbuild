import os, shutil, glob

def deploy(graph, output_folder, **kwargs):
    for dep in graph.nodes:
        folder = dep.conanfile.package_folder
        if folder is None:
            continue
        for ext in ("*.pc", "*.cmake"):
            for f in glob.glob(os.path.join(folder, "**", ext), recursive=True):
                shutil.copy2(f, output_folder)
