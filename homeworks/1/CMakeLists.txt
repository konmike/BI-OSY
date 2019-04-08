cmake_minimum_required(VERSION 3.10)
project(osy)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -W -Wall -Wextra -pedantic")

add_executable(osy solution.cpp common.h progtest_solver.h sample_tester.cpp sample_tester.h)
