add_executable(unix_astrolog astrolog.c)
target_link_libraries(unix_astrolog PRIVATE binary_heap unix_common)

add_executable(unix_proxy proxy.c)
target_link_libraries(unix_proxy PRIVATE unix_common)

add_executable(unix_client client.c)
target_link_libraries(unix_client PRIVATE unix_common)
