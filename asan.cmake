set(CMAKE_C_COMPILER /home/mccakit/dev/llvm/bin/clang)
set(CMAKE_CXX_COMPILER /home/mccakit/dev/llvm/bin/clang++)
set(CMAKE_ASM_COMPILER /home/mccakit/dev/llvm/bin/clang)
set(CMAKE_RC_COMPILER /home/mccakit/dev/llvm/bin/llvm-rc)
set(CMAKE_AR /home/mccakit/dev/llvm/bin/llvm-ar)
set(CMAKE_RANLIB /home/mccakit/dev/llvm/bin/llvm-ranlib)
set(CMAKE_MT /home/mccakit/dev/llvm/bin/llvm-mt)
set(PKG_CONFIG_EXECUTABLE "/home/mccakit/dev/pkgconf/bin/pkgconf.py" CACHE FILEPATH "" FORCE)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STDLIB_MODULES_JSON "/home/mccakit/dev/libcxx/native/lib/libc++.modules.json")
set(CMAKE_BUILD_TYPE DEBUG)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)


set(CMAKE_CXX_FLAGS_INIT "-std=c++26 -nostdinc++ -nostdlib++ -isystem /home/mccakit/dev/libcxx/native/include/c++/v1 -g -O0 -fno-omit-frame-pointer -fsanitize=address,undefined")
set(CMAKE_C_FLAGS_INIT "-std=gnu23 -g -O0 -fno-omit-frame-pointer -fsanitize=address,undefined")
add_link_options(-fuse-ld=lld -rtlib=compiler-rt -nostdlib++ -L/home/mccakit/dev/libcxx/native/lib -Wl,-Bstatic -lunwind -lc++abi -lc++ -Wl,-Bdynamic -fsanitize=address,undefined)

