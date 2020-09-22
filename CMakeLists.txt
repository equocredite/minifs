cmake_minimum_required(VERSION 3.15)
project(minifs C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu11 -Wall -pthread")

include_directories("include")

set(SERVER_SRCS src/bit_util.c src/block.c src/disk_io.c src/inode.c src/interface.c src/main.c src/net_io.c src/str_util.c src/lock.c)
add_executable(server ${SERVER_SRCS})
target_link_libraries(server pthread)

set(CLIENT_SRCS src/client.c src/str_util.c)
add_executable(client ${CLIENT_SRCS})
