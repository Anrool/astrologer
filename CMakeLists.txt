cmake_minimum_required(VERSION 3.0)

project(astrolog)

find_package(Boost REQUIRED
    system
    thread
    date_time
    regex
    serialization
    coroutine
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(boost_asio)
add_subdirectory(unix_socket_api)
