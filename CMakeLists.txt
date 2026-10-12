cmake_minimum_required(VERSION 3.20)

project(GraphJITLexer VERSION 0.1.0 LANGUAGES CXX)

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

include(CTest)
if(BUILD_TESTING)
    add_executable(graphjit_lexer_tests tests/lexer_tests.cpp)
    target_link_libraries(graphjit_lexer_tests PRIVATE graphjit_lexer)
    graphjit_enable_warnings(graphjit_lexer_tests)
    add_test(NAME graphjit_lexer_tests COMMAND graphjit_lexer_tests)
endif()
