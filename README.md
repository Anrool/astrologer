# What
Implementation of a toy "astrologer" protocol on TCP sockets using UNIX socket API and Boost.Asio.

# Description
[Ideco networking programming course blog](https://habr.com/ru/company/ideco/blog/147086/).\
The project allows comparison of two networking programming approaches - UNIX socket API versus Boost.Asio.\
The protocol consists of the following parts:
1. server - process simultaneously a huge number of client requests
2. client
    * star - sets a new prognosis for a particular zodiac. It must connect directly to the server and not through a proxy
    * ordinary - polls actual prognosis for the particular zodiac
3. proxy - prevents star client from accessing the server (access control)

# Requirements
Standard-compatible compiler with C++11 support.\
Boost.Asio

# CMake build workflow
```
mkdir <build_dir>
cd <build_dir>
cmake <source_dir> -DBOOST_ROOT=<boost_install_prefix>
cmake --build <build_dir>
```

# Usage
All executables can be launched directly from the top build directory.\
Unix executable names start with "unix_" prefix.\
Run executable without arguments to see help information.
1. Boost.Asio usage:
```
./server <port>
./proxy <address> <port> <listen_port>
./client star <address> <port> <zodiac> <prognosis> [<remainder>]
./client human <address> <port> <zodiac> [<remainder>]
./client multi <address> <port> <human_amount>
```
2. Unix socket API usage:
```
./unix_astrolog: <address> <port>
./unix_proxy: <address> <astrolog_port> <proxy_port>
./unix_client: <address> <port> <client_type>
```
3. Also, the Unix subfolder contains binary heap implementation.\
To test it run:
```
./binary_heap_test <heap_size>
```
