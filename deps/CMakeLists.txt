add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_CURRENT_LIST_DIR}/cxxopts)

add_library(subprocess INTERFACE)
target_include_directories(subprocess INTERFACE ${CMAKE_CURRENT_LIST_DIR}/subprocess)
