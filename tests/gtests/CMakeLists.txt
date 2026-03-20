cmake_minimum_required(VERSION 3.20)
project(DispatcherTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_executable(dispatcher_tests
    src/main.cpp
    src/dispatcher/DispatcherTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/dispatcher/Dispatcher.cpp
)

target_include_directories(dispatcher_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dispatcher_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
)

add_executable(db_tests
    src/main.cpp
    src/db/DatabaseManagerTest.cpp
)

target_link_libraries(db_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
        shared_db
)

target_include_directories(db_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

enable_testing()
add_test(NAME dispatcher_tests COMMAND dispatcher_tests)
add_test(NAME db_tests COMMAND db_tests)
gtest_discover_tests(dispatcher_tests)
gtest_discover_tests(db_tests)