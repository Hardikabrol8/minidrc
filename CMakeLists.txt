cmake_minimum_required(VERSION 3.16)
project(minidrc CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(OpenMP)

add_library(minidrc_core src/layout.cpp src/drc.cpp src/gen.cpp src/svg.cpp)
target_include_directories(minidrc_core PUBLIC src)
if(OpenMP_CXX_FOUND)
  target_link_libraries(minidrc_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(minidrc src/main.cpp)
target_link_libraries(minidrc PRIVATE minidrc_core)

enable_testing()
add_executable(minidrc_tests tests/test_main.cpp)
target_link_libraries(minidrc_tests PRIVATE minidrc_core)
target_compile_definitions(minidrc_tests PRIVATE EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
add_test(NAME unit COMMAND minidrc_tests)
