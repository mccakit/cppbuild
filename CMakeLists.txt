cmake_minimum_required(VERSION 4.2)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_MODULE_STD ON)
project(
    cppbuild
    VERSION 0.0.0
    DESCRIPTION "placeholder"
    LANGUAGES C CXX
)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/conan)
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_CURRENT_SOURCE_DIR}/conan:$ENV{PKG_CONFIG_LIBDIR}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(FetchContent)
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

add_subdirectory(deps)
add_subdirectory(src)
add_subdirectory(apps)
add_subdirectory(tests)
