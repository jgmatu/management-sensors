find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS json)

add_library(json STATIC
    JsonUtils.cpp
)

target_include_directories(json
    PUBLIC ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(json
    PUBLIC
        Boost::json
)
