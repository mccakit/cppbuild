find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
find_package(SQLiteCpp REQUIRED)

pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(UMKACXX REQUIRED IMPORTED_TARGET umkacxx)
pkg_check_modules(SUBPROCESS REQUIRED IMPORTED_TARGET subprocess)
pkg_check_modules(GLAZE REQUIRED IMPORTED_TARGET glaze)
pkg_check_modules(ZPPBITS REQUIRED IMPORTED_TARGET zppbits)
pkg_check_modules(THREAD_POOL REQUIRED IMPORTED_TARGET thread_pool)
pkg_check_modules(LMDBXX REQUIRED IMPORTED_TARGET lmdbxx)

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
        fmt::fmt-module
        PkgConfig::GRAAF
        PkgConfig::UMKACXX
        PkgConfig::SUBPROCESS
        PkgConfig::GLAZE
        PkgConfig::ZPPBITS
        PkgConfig::THREAD_POOL
        PkgConfig::LMDBXX
        SQLiteCpp
)
