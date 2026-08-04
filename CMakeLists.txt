cmake_minimum_required(VERSION 4.4.2)
project(
    cppbuild
    VERSION 0.1.0
    DESCRIPTION
        "A minimal C++ build system with an Umka scripting frontend and a C++ backend"
    LANGUAGES C CXX)
set(CMAKE_EXPERIMENTAL_FIND_CPS_PACKAGES ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS ON)
include(GNUInstallDirs)
add_subdirectory(src)
add_subdirectory(apps)
add_subdirectory(tests)
