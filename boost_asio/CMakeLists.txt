add_library(common INTERFACE)
target_compile_options(common INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -O2
)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(common INTERFACE ${Boost_LIBRARIES})

add_executable(server src/server.cpp)
target_link_libraries(server PRIVATE common)

add_executable(proxy src/proxy.cpp)
target_link_libraries(proxy PRIVATE common)

add_executable(client src/client.cpp)
target_link_libraries(client PRIVATE common)
