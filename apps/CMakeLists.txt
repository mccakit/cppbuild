find_package(PkgConfig REQUIRED)
add_executable(cppbuild main.cpp)
find_package(fmt REQUIRED)
target_link_libraries(
    cppbuild
    PRIVATE
        fmt::fmt
        cxxopts
        cppbuild_lib
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
