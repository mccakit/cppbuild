cmake_minimum_required(VERSION 4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)
project(
    cppbuild
    VERSION 0.0.0
    DESCRIPTION "placeholder"
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
add_subdirectory(deps)
add_subdirectory(src)
add_subdirectory(apps)
add_subdirectory(tests)
