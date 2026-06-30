cmake_minimum_required(VERSION 3.21)
project(regex_engine C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(regex_engine
    src/regex_engine.c
)
target_include_directories(regex_engine PUBLIC include)

if(MSVC)
    target_compile_options(regex_engine PRIVATE /W4)
else()
    target_compile_options(regex_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(test_api tests/test_api.c)
target_link_libraries(test_api PRIVATE regex_engine)
add_test(NAME test_api COMMAND test_api)

add_executable(rx_cli tools/rx_cli.c)
target_link_libraries(rx_cli PRIVATE regex_engine)

