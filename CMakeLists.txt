cmake_minimum_required(VERSION 3.24)

project(quin_cpp_easier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json googletest)

add_library(elevator src/elevator.cpp)
target_include_directories(elevator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(NOT MSVC)
  target_compile_options(elevator PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
include(GoogleTest)

add_executable(elevator_tests tests/ElevatorTests.cpp)
target_link_libraries(elevator_tests PRIVATE elevator GTest::gtest_main)
gtest_discover_tests(elevator_tests)

set(MBT_TRACES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/traces CACHE PATH "Directory with Quint ITF traces")
add_executable(mbt_driver tests/MBTDriver.cpp)
target_link_libraries(mbt_driver PRIVATE elevator nlohmann_json::nlohmann_json GTest::gtest_main)
target_compile_definitions(mbt_driver PRIVATE MBT_DEFAULT_TRACES_DIR="${MBT_TRACES_DIR}")
add_test(NAME mbt_driver COMMAND mbt_driver)
