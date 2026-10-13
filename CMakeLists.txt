cmake_minimum_required(VERSION 3.20)

project(GraphJIT VERSION 0.9.0 LANGUAGES C CXX)

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

add_library(graphjit_optimizer constant_folding.cpp kernel_fusion.cpp)
target_include_directories(graphjit_optimizer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(graphjit_optimizer PUBLIC graphjit_ast)
target_compile_features(graphjit_optimizer PUBLIC cxx_std_20)
graphjit_enable_warnings(graphjit_optimizer)

add_executable(graphjit_opt_demo opt_main.cpp)
target_link_libraries(
    graphjit_opt_demo
    PRIVATE graphjit_optimizer graphjit_parser
)
graphjit_enable_warnings(graphjit_opt_demo)

add_executable(graphjit_fusion_demo fusion_main.cpp)
target_link_libraries(
    graphjit_fusion_demo
    PRIVATE graphjit_optimizer graphjit_parser
)
graphjit_enable_warnings(graphjit_fusion_demo)

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

option(GRAPHJIT_ENABLE_LLVM "Build the LLVM IR backend" ON)

if(GRAPHJIT_ENABLE_LLVM)
    find_package(LLVM REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake from ${LLVM_DIR}")

    separate_arguments(
        GRAPHJIT_LLVM_DEFINITIONS
        NATIVE_COMMAND
        "${LLVM_DEFINITIONS}"
    )
    llvm_map_components_to_libnames(GRAPHJIT_LLVM_LIBS core support)
    llvm_map_components_to_libnames(
        GRAPHJIT_JIT_LLVM_LIBS
        core orcjit native support
    )

    add_library(graphjit_ir_backend ir_generator.cpp)
    target_include_directories(
        graphjit_ir_backend
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(
        graphjit_ir_backend
        SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS}
    )
    target_compile_options(
        graphjit_ir_backend
        PUBLIC ${GRAPHJIT_LLVM_DEFINITIONS}
    )
    target_link_libraries(
        graphjit_ir_backend
        PUBLIC graphjit_ast graphjit_shape_analysis ${GRAPHJIT_LLVM_LIBS}
    )
    target_compile_features(graphjit_ir_backend PUBLIC cxx_std_20)
    graphjit_enable_warnings(graphjit_ir_backend)

    add_executable(graphjit_ir_demo ir_main.cpp)
    target_link_libraries(
        graphjit_ir_demo
        PRIVATE graphjit_ir_backend
    )
    graphjit_enable_warnings(graphjit_ir_demo)

    add_library(graphjit_jit jit_engine.cpp)
    target_include_directories(
        graphjit_jit
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(
        graphjit_jit
        SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS}
    )
    target_compile_options(graphjit_jit PUBLIC ${GRAPHJIT_LLVM_DEFINITIONS})
    target_link_libraries(
        graphjit_jit
        PUBLIC ${GRAPHJIT_JIT_LLVM_LIBS}
    )
    target_compile_features(graphjit_jit PUBLIC cxx_std_20)
    graphjit_enable_warnings(graphjit_jit)

    add_executable(graphjit_jit_demo jit_main.cpp)
    target_link_libraries(
        graphjit_jit_demo
        PRIVATE graphjit_jit graphjit_ir_backend
    )
    graphjit_enable_warnings(graphjit_jit_demo)
endif()

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

    add_executable(graphjit_opt_tests tests/constant_folding_tests.cpp)
    target_link_libraries(
        graphjit_opt_tests
        PRIVATE graphjit_optimizer graphjit_parser
    )
    graphjit_enable_warnings(graphjit_opt_tests)
    add_test(NAME graphjit_opt_tests COMMAND graphjit_opt_tests)

    add_executable(graphjit_fusion_tests tests/kernel_fusion_tests.cpp)
    target_link_libraries(
        graphjit_fusion_tests
        PRIVATE
            graphjit_optimizer
            graphjit_parser
            graphjit_shape_analysis
    )
    graphjit_enable_warnings(graphjit_fusion_tests)
    add_test(NAME graphjit_fusion_tests COMMAND graphjit_fusion_tests)

    if(GRAPHJIT_ENABLE_LLVM)
        add_executable(graphjit_ir_tests tests/ir_generator_tests.cpp)
        target_link_libraries(
            graphjit_ir_tests
            PRIVATE graphjit_ir_backend graphjit_parser
        )
        graphjit_enable_warnings(graphjit_ir_tests)
        add_test(NAME graphjit_ir_tests COMMAND graphjit_ir_tests)

        add_executable(graphjit_tensor_ir_tests tests/tensor_ir_generator_tests.cpp)
        target_link_libraries(
            graphjit_tensor_ir_tests
            PRIVATE
                graphjit_ir_backend
                graphjit_shape_analysis
        )
        graphjit_enable_warnings(graphjit_tensor_ir_tests)
        add_test(NAME graphjit_tensor_ir_tests COMMAND graphjit_tensor_ir_tests)

        add_executable(graphjit_jit_tests tests/jit_engine_tests.cpp)
        target_link_libraries(
            graphjit_jit_tests
            PRIVATE graphjit_jit graphjit_ir_backend
        )
        graphjit_enable_warnings(graphjit_jit_tests)
        add_test(NAME graphjit_jit_tests COMMAND graphjit_jit_tests)
    endif()
endif()
