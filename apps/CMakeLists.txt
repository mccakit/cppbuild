find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(CLI11 REQUIRED IMPORTED_TARGET cli11)
pkg_check_modules(THREAD_POOL REQUIRED IMPORTED_TARGET thread_pool)
pkg_check_modules(LMDBXX REQUIRED IMPORTED_TARGET lmdbxx)
add_executable(cppbuild main.cpp)
target_link_libraries(
    cppbuild
    PRIVATE
        fmt::fmt
        cppbuild_lib
        PkgConfig::CLI11
        PkgConfig::THREAD_POOL
        PkgConfig::LMDBXX
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
