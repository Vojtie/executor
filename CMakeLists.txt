cmake_minimum_required(VERSION 3.1)
project(MIMUW-FORK C)

set(CMAKE_CXX_STANDARD "17")
set(CMAKE_C_STANDARD "11")
set(CMAKE_C_FLAGS "-g -Wall -Wextra -Wno-sign-compare")

file(GLOB MY_FILES "temp/*.txt")
configure_file(${MY_FILES} ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)

function(add_example_program target_name)
  add_executable(${target_name} "${target_name}.c")
  target_link_libraries(${target_name} err)
  target_link_libraries(${target_name} utils)
  target_link_libraries(${target_name} pthread)
endfunction()

add_library(err err.c)
add_library(utils utils.c)
add_executable(wsr writesleeprepeat.c)

add_example_program(stdinlinereader)
add_example_program(output)
add_example_program(executor)

