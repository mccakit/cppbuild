find_package(PkgConfig REQUIRED)
file(GLOB CPPBUILD_MODULES ${CMAKE_SOURCE_DIR}/src/cppbuild/*.cpp)
add_executable(cppbuild main.cpp)
target_sources(cppbuild PRIVATE FILE_SET CXX_MODULES BASE_DIRS "${CMAKE_SOURCE_DIR}/src/cppbuild" FILES ${CPPBUILD_MODULES})
pkg_check_modules(FMT REQUIRED IMPORTED_TARGET fmt)
pkg_check_modules(SIMDJSON REQUIRED IMPORTED_TARGET simdjson)
pkg_check_modules(GRAAF REQUIRED IMPORTED_TARGET graaf)
pkg_check_modules(UMKA REQUIRED IMPORTED_TARGET umka)
target_link_libraries(
    cppbuild
    PRIVATE
        PkgConfig::SIMDJSON
        PkgConfig::FMT
        PkgConfig::GRAAF
        PkgConfig::UMKA
        cxxopts
        subprocess
)
target_include_directories(
    cppbuild
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
