cmake_minimum_required(VERSION 3.20)
project(QuantumSafeSystem LANGUAGES CXX)

# Global C++20 requirement for jthread and coroutines
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 1. First, build the shared library (The "Engine" and "DB")
add_subdirectory(libs/db)

# 2. Then, build the executables that link to it
add_subdirectory(server)
add_subdirectory(controller)
add_subdirectory(sensor)
add_subdirectory(proxy)