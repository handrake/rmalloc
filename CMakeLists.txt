cmake_minimum_required(VERSION 3.28)
project(rmalloc C)

set(CMAKE_C_STANDARD 11)

#add_library(rmalloc STATIC rmalloc.c)
add_executable(rmalloc main.c rmalloc.c)
