cmake_minimum_required(VERSION 3.1)
project(SIMULATION)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(simulation simulation.c dumb.c)
