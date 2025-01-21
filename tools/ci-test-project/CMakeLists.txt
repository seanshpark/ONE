cmake_minimum_required(VERSION 3.10)

project(ci-test-project)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Type of build" FORCE)
endif(NOT CMAKE_BUILD_TYPE)

SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
SET(CMAKE_CXX_STANDARD 17)

# ci-test main
add_executable(ci_test_project src/main.cpp)

# ci-test unit test with gtest
enable_testing()

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIR})

add_executable(ci_test_test src/main.test.cpp)
target_link_libraries(ci_test_test GTest::gtest_main)
add_test(NAME ci_test_test COMMAND ci_test_test)
