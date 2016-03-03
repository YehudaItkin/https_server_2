cmake_minimum_required(VERSION 2.8)
project(http_server)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Werror")

set(SOURCE_FILES main.cpp http_parser.h http_parser.c)

file(MAKE_DIRECTORY /home/box/final)
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY /home/box/final )

#file(MAKE_DIRECTORY /home/yehuda/final)
#set( CMAKE_RUNTIME_OUTPUT_DIRECTORY /home/yehuda/final )


add_executable(final  ${SOURCE_FILES})