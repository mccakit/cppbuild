find_package(fmt REQUIRED)
find_package(graaf REQUIRED)
find_package(umkacxx REQUIRED)
find_package(benman64-subprocess REQUIRED)
find_package(glaze REQUIRED)
find_package(bs-thread_pool REQUIRED)
find_package(lmdbxx REQUIRED)
find_package(zpp-bits REQUIRED)
find_package(cli11 REQUIRED)
file(
    GLOB_RECURSE srcs
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)
add_library(cppbuild_library)
target_sources(cppbuild_library PUBLIC FILE_SET CXX_MODULES FILES ${srcs})
target_link_libraries(
    cppbuild_library
    PUBLIC
        fmt::cxx_module
        graaf::cxx_module
        umkacxx::cxx_module
        benman64-subprocess::cxx_module
        glaze::cxx_module
        bs-thread_pool::cxx_module
        lmdbxx::cxx_module
        zpp-bits::cxx_module
        cli11::cxx_module
)
