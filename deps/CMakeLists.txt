add_library(subprocess INTERFACE)
target_include_directories(subprocess INTERFACE ${CMAKE_CURRENT_LIST_DIR}/subprocess)

add_library(zpp INTERFACE)
target_include_directories(zpp INTERFACE ${CMAKE_CURRENT_LIST_DIR}/zpp)
