find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
find_package(mimalloc REQUIRED)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(CLI11 REQUIRED IMPORTED_TARGET cli11)
pkg_check_modules(THREAD_POOL REQUIRED IMPORTED_TARGET thread_pool)
add_executable(cppbuild main.cpp)
target_link_libraries(
    cppbuild
    PRIVATE
        mimalloc-static
        fmt::fmt
        cppbuild_lib
        PkgConfig::CLI11
        PkgConfig::THREAD_POOL
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
