add_library(lmdb STATIC)
target_sources(
    lmdb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lmdb/mdb.c
        ${CMAKE_CURRENT_SOURCE_DIR}/lmdb/midl.c
)
target_include_directories(lmdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lmdb)
