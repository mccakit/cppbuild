conan install . --build=missing --profile=native -of ./conan --deployer=./conandeploy.py --envs-generation=false
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=... && cmake --build build && cmake --install build

./cppbuild --mode=build --build_dir=build
./cppbuild --mode=configure --src_dir=. --build_dir=build --toolchain=./tc.json
