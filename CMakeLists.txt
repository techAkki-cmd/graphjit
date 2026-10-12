cmake_minimum_required(VERSION 3.20)

project(GraphJIT VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(graphjit_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
        )
    endif()
endfunction()

add_library(graphjit_lexer lexer.cpp)
target_include_directories(graphjit_lexer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(graphjit_lexer PUBLIC cxx_std_20)
graphjit_enable_warnings(graphjit_lexer)

add_executable(graphjit_lexer_demo main.cpp)
target_link_libraries(graphjit_lexer_demo PRIVATE graphjit_lexer)
graphjit_enable_warnings(graphjit_lexer_demo)

add_library(graphjit_ast ast.cpp ast_printer.cpp)
target_include_directories(graphjit_ast PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(graphjit_ast PUBLIC graphjit_lexer)
target_compile_features(graphjit_ast PUBLIC cxx_std_20)
graphjit_enable_warnings(graphjit_ast)

add_executable(graphjit_ast_demo ast_main.cpp)
target_link_libraries(graphjit_ast_demo PRIVATE graphjit_ast)
graphjit_enable_warnings(graphjit_ast_demo)

add_library(graphjit_parser parser.cpp)
target_include_directories(graphjit_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(graphjit_parser PUBLIC graphjit_ast)
target_compile_features(graphjit_parser PUBLIC cxx_std_20)
graphjit_enable_warnings(graphjit_parser)

add_executable(graphjit_parser_demo parser_main.cpp)
target_link_libraries(graphjit_parser_demo PRIVATE graphjit_parser)
graphjit_enable_warnings(graphjit_parser_demo)

add_library(graphjit_shape_analysis shape_inference.cpp)
target_include_directories(graphjit_shape_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(graphjit_shape_analysis PUBLIC graphjit_ast)
target_compile_features(graphjit_shape_analysis PUBLIC cxx_std_20)
graphjit_enable_warnings(graphjit_shape_analysis)

add_executable(graphjit_shape_demo shape_main.cpp)
target_link_libraries(
    graphjit_shape_demo
    PRIVATE graphjit_shape_analysis graphjit_parser
)
graphjit_enable_warnings(graphjit_shape_demo)

include(CTest)
if(BUILD_TESTING)
    add_executable(graphjit_lexer_tests tests/lexer_tests.cpp)
    target_link_libraries(graphjit_lexer_tests PRIVATE graphjit_lexer)
    graphjit_enable_warnings(graphjit_lexer_tests)
    add_test(NAME graphjit_lexer_tests COMMAND graphjit_lexer_tests)

    add_executable(graphjit_ast_tests tests/ast_tests.cpp)
    target_link_libraries(graphjit_ast_tests PRIVATE graphjit_ast)
    graphjit_enable_warnings(graphjit_ast_tests)
    add_test(NAME graphjit_ast_tests COMMAND graphjit_ast_tests)

    add_executable(graphjit_parser_tests tests/parser_tests.cpp)
    target_link_libraries(graphjit_parser_tests PRIVATE graphjit_parser)
    graphjit_enable_warnings(graphjit_parser_tests)
    add_test(NAME graphjit_parser_tests COMMAND graphjit_parser_tests)

    add_executable(graphjit_shape_tests tests/shape_inference_tests.cpp)
    target_link_libraries(
        graphjit_shape_tests
        PRIVATE graphjit_shape_analysis graphjit_parser
    )
    graphjit_enable_warnings(graphjit_shape_tests)
    add_test(NAME graphjit_shape_tests COMMAND graphjit_shape_tests)
endif()
