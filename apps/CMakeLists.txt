add_executable(cppbuild_app)
target_sources(cppbuild_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app.cpp)
target_link_libraries(cppbuild_app PRIVATE cppbuild_library)
