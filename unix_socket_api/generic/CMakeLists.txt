add_library(generic_headers INTERFACE)
target_include_directories(generic_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(generic STATIC generic.c)
target_link_libraries(generic PUBLIC generic_headers)
