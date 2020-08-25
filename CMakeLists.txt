cmake_minimum_required(VERSION 3.15)
project(minifs C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu11 -Wall")

include_directories("include")
file(GLOB SRCS src/*.c)
add_executable(minifs ${SRCS})
