find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(CLI11 REQUIRED IMPORTED_TARGET cli11)

add_executable(cppbuild main.cpp)
target_link_libraries(
    cppbuild
    PRIVATE
        fmt::fmt
        cppbuild_lib
        PkgConfig::CLI11
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
