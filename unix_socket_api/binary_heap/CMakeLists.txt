add_library(binary_heap STATIC binary_heap.c)
target_include_directories(binary_heap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(binary_heap_test binary_heap_test.c)
target_link_libraries(binary_heap_test PRIVATE binary_heap generic_headers)
