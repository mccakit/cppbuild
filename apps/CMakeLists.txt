find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)

add_executable(cppbuild main.cpp)
target_link_libraries(
    cppbuild
    PRIVATE
        fmt::fmt
        cppbuild_lib
        CLI11::CLI11
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
