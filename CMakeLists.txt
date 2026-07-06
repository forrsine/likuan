cmake_minimum_required(VERSION 3.21)
project(regex_engine C)

include(CheckIncludeFile)
check_include_file("regex.h" RX_HAVE_POSIX_REGEX_H)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(RX_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(RX_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "RX_ENABLE_SANITIZERS is unsupported by this compiler")
    endif()
endif()

add_library(regex_engine
    src/charset.c
    src/ast.c
    src/lexer.c
    src/parser.c
    src/nfa.c
    src/matcher.c
    src/capture.c
    src/dfa.c
    src/dfa_minimize.c
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

add_executable(test_nfa tests/test_nfa.c)
target_include_directories(test_nfa PRIVATE src)
target_link_libraries(test_nfa PRIVATE regex_engine)
rx_enable_warnings(test_nfa)
add_test(NAME test_nfa COMMAND test_nfa)

add_executable(test_dfa tests/test_dfa.c)
target_include_directories(test_dfa PRIVATE src)
target_link_libraries(test_dfa PRIVATE regex_engine)
rx_enable_warnings(test_dfa)
add_test(NAME test_dfa COMMAND test_dfa)

add_executable(test_stress tests/test_stress.c)
target_link_libraries(test_stress PRIVATE regex_engine)
rx_enable_warnings(test_stress)
add_test(NAME test_stress COMMAND test_stress)
set_tests_properties(test_stress PROPERTIES TIMEOUT 60)

add_executable(test_conformance tests/test_conformance.c)
target_link_libraries(test_conformance PRIVATE regex_engine)
rx_enable_warnings(test_conformance)
add_test(NAME test_conformance COMMAND test_conformance)

if(RX_HAVE_POSIX_REGEX_H)
    add_executable(test_posix tests/test_posix.c)
    target_link_libraries(test_posix PRIVATE regex_engine)
    rx_enable_warnings(test_posix)
    add_test(NAME test_posix COMMAND test_posix)
endif()

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

add_executable(rx_dump_nfa tools/rx_dump_nfa.c)
target_include_directories(rx_dump_nfa PRIVATE src)
target_link_libraries(rx_dump_nfa PRIVATE regex_engine)
rx_enable_warnings(rx_dump_nfa)

add_executable(rx_dump_dfa tools/rx_dump_dfa.c)
target_include_directories(rx_dump_dfa PRIVATE src)
target_link_libraries(rx_dump_dfa PRIVATE regex_engine)
rx_enable_warnings(rx_dump_dfa)

add_executable(rx_bench tools/rx_bench.c)
target_link_libraries(rx_bench PRIVATE regex_engine)
if(RX_HAVE_POSIX_REGEX_H)
    target_compile_definitions(rx_bench PRIVATE RX_HAVE_POSIX_REGEX=1)
else()
    target_compile_definitions(rx_bench PRIVATE RX_HAVE_POSIX_REGEX=0)
endif()
rx_enable_warnings(rx_bench)
add_test(NAME test_bench_smoke COMMAND rx_bench --iterations 100)
set_tests_properties(test_bench_smoke PROPERTIES TIMEOUT 30)
