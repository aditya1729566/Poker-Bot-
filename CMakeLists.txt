cmake_minimum_required(VERSION 3.25)
project(PokerZero VERSION 0.1.0 LANGUAGES CXX)

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE POKERZERO_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE GIT_RESULT)
if(NOT GIT_RESULT EQUAL 0)
  set(POKERZERO_GIT_COMMIT "unborn")
endif()
execute_process(COMMAND git status --porcelain WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(GIT_STATUS)
  set(POKERZERO_GIT_DIRTY "true")
else()
  set(POKERZERO_GIT_DIRTY "false")
endif()
configure_file(cmake/build_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(pokerzero_core src/core.cpp src/agents.cpp src/match.cpp src/small_games.cpp src/restricted.cpp)
target_include_directories(pokerzero_core PUBLIC include)
target_compile_options(pokerzero_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pokerzero src/main.cpp)
target_link_libraries(pokerzero PRIVATE pokerzero_core)
target_include_directories(pokerzero PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

enable_testing()
add_executable(pokerzero_tests tests/test_core.cpp)
target_link_libraries(pokerzero_tests PRIVATE pokerzero_core)
add_test(NAME core COMMAND pokerzero_tests)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME build_preflight COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_build_preflight.py)
