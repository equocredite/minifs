cmake_minimum_required(VERSION 3.15)
project(minifs C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu11 -Wall")

include_directories("include")

set(SERVER_SRCS src/bit_util.c src/block.c src/disk_io.c src/inode.c src/interface.c src/main.c src/net_io.c src/str_util.c)
add_executable(server ${SERVER_SRCS})

set(CLIENT_SRCS src/client.c src/str_util.c)
add_executable(client ${CLIENT_SRCS})
