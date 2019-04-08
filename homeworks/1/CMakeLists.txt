cmake_minimum_required(VERSION 3.10)
project(osy)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -W -Wall -Wextra -pedantic")

add_library(progtest_solver SHARED IMPORTED)
set_target_properties(progtest_solver PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/libprogtest_solver.a)

link_directories("/mnt/d/git/BI-OSY/homework/1")
add_executable(osy common.h solution.cpp progtest_solver.h sample_tester.cpp sample_tester.h)
target_link_libraries(osy pthread progtest_solver)

