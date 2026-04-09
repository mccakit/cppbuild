find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
pkg_check_modules(SIMDJSON REQUIRED IMPORTED_TARGET simdjson)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(UMKACXX REQUIRED IMPORTED_TARGET umkacxx)
pkg_check_modules(SUBPROCESS REQUIRED IMPORTED_TARGET subprocess)

file(
    GLOB_RECURSE cppbuild_srcs
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/cppbuild/*.cpp
)
add_library(cppbuild_lib STATIC)
target_sources(cppbuild_lib PUBLIC FILE_SET CXX_MODULES FILES ${cppbuild_srcs})
target_link_libraries(
    cppbuild_lib
    PUBLIC
        PkgConfig::SIMDJSON
        fmt::fmt-module
        PkgConfig::GRAAF
        PkgConfig::UMKACXX
        PkgConfig::SUBPROCESS
        zpp
)
