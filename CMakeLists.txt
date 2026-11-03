cmake_minimum_required(VERSION 3.16)
project(Cyrk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CYRK_BUILD_TESTS "Build unit tests (GoogleTest)" ON)
option(CYRK_TSAN "Build with ThreadSanitizer" OFF)

find_package(Threads REQUIRED)

# Biblioteka nagłówkowa: system.hpp + machine.hpp
add_library(cyrk INTERFACE)
target_include_directories(cyrk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cyrk INTERFACE Threads::Threads)

if(CYRK_TSAN)
  add_compile_options(-fsanitize=thread -g)
  add_link_options(-fsanitize=thread)
endif()

if(CYRK_BUILD_TESTS)
  enable_testing()

  # GoogleTest: najpierw z Conana / systemu, a jeśli go nie ma, pobierany przez FetchContent
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(cyrk_tests tests/system_tests.cpp)
  target_link_libraries(cyrk_tests PRIVATE cyrk GTest::gtest_main)
  target_compile_options(cyrk_tests PRIVATE -Wall -Wextra -Wpedantic)

  include(GoogleTest)
  gtest_discover_tests(cyrk_tests)
endif()
