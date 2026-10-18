cmake_minimum_required(VERSION 3.16)
project(tennis_swing_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# Core library: everything except main(), shared by the CLI and the tests.
add_library(swingcore STATIC
  host/src/analysis.cpp
  host/src/logger.cpp
  host/src/protocol.cpp
  host/src/report.cpp
  host/src/serial_port.cpp
  host/src/session_io.cpp
  host/src/simulator.cpp
  host/src/svg_chart.cpp
)
target_include_directories(swingcore PUBLIC
  host/include
  firmware/swing_tracker   # SwingDetector.h is shared with the firmware
)
target_link_libraries(swingcore PUBLIC Threads::Threads)

add_executable(swingtrack host/src/main.cpp)
target_link_libraries(swingtrack PRIVATE swingcore)

enable_testing()
add_executable(swingtrack_tests tests/test_main.cpp)
target_link_libraries(swingtrack_tests PRIVATE swingcore)
add_test(NAME swingtrack_tests COMMAND swingtrack_tests)
