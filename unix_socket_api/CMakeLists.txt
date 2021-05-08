add_library(unix_common INTERFACE)
target_compile_options(unix_common INTERFACE
    -Wall
    -Wextra
    -O2
)
target_include_directories(unix_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(unix_common INTERFACE generic)

add_subdirectory(generic)
add_subdirectory(binary_heap)
add_subdirectory(src)
