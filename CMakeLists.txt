cmake_minimum_required(VERSION 3.19)

project(flyweight.hpp)

option(FLYWEIGHT_BUILD_TESTS "Whether to build automated tests" OFF)

add_library(flyweight.hpp INTERFACE flyweight.hpp)
target_include_directories(flyweight.hpp INTERFACE .)

if(FLYWEIGHT_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
