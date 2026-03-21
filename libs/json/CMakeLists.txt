find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS json)

add_library(shared_json STATIC
    JsonUtils.cpp
)

target_include_directories(shared_json
    PUBLIC ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(shared_json
    PUBLIC
        Boost::json
)
