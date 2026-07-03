cmake_minimum_required(VERSION 3.21)
project(regex_engine C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(regex_engine
    src/charset.c
    src/ast.c
    src/lexer.c
    src/parser.c
    src/nfa.c
    src/matcher.c
    src/regex_engine.c
)
target_include_directories(regex_engine PUBLIC include)
target_include_directories(regex_engine PRIVATE src)

function(rx_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

rx_enable_warnings(regex_engine)

enable_testing()

add_executable(test_api tests/test_api.c)
target_link_libraries(test_api PRIVATE regex_engine)
rx_enable_warnings(test_api)
add_test(NAME test_api COMMAND test_api)

add_executable(test_lexer tests/test_lexer.c)
target_include_directories(test_lexer PRIVATE src)
target_link_libraries(test_lexer PRIVATE regex_engine)
rx_enable_warnings(test_lexer)
add_test(NAME test_lexer COMMAND test_lexer)

add_executable(test_parser tests/test_parser.c)
target_include_directories(test_parser PRIVATE src)
target_link_libraries(test_parser PRIVATE regex_engine)
rx_enable_warnings(test_parser)
add_test(NAME test_parser COMMAND test_parser)

add_executable(rx_cli tools/rx_cli.c)
target_link_libraries(rx_cli PRIVATE regex_engine)
rx_enable_warnings(rx_cli)

add_executable(rx_dump_ast tools/rx_dump_ast.c)
target_include_directories(rx_dump_ast PRIVATE src)
target_link_libraries(rx_dump_ast PRIVATE regex_engine)
rx_enable_warnings(rx_dump_ast)

add_executable(rx_dump_tokens tools/rx_dump_tokens.c)
target_include_directories(rx_dump_tokens PRIVATE src)
target_link_libraries(rx_dump_tokens PRIVATE regex_engine)
rx_enable_warnings(rx_dump_tokens)
