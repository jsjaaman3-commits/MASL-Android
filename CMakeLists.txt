cmake_minimum_required(VERSION 3.25)

project(MASL VERSION 0.10.0 DESCRIPTION "Mahdi Architecture Simulation Language" LANGUAGES CXX)

option(MASL_BUILD_TESTS "Build MASL tests" ON)
option(MASL_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MASL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MASL_USE_SYSTEM_GTEST "Use an installed GoogleTest package when available" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(masl_project_options INTERFACE)
target_compile_features(masl_project_options INTERFACE cxx_std_20)
if(MSVC)
  target_compile_options(masl_project_options INTERFACE /W4 /permissive-)
  if(MASL_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(masl_project_options INTERFACE /WX)
  endif()
else()
  target_compile_options(masl_project_options INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
    -Wformat=2 -Wnull-dereference -Wdouble-promotion)
  if(MASL_ENABLE_WARNINGS_AS_ERRORS)
    target_compile_options(masl_project_options INTERFACE -Werror)
  endif()
  if(MASL_ENABLE_SANITIZERS)
    target_compile_options(masl_project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(masl_project_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

add_library(masl_core STATIC
  src/core/version.cpp
  src/lexer/token.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/semantic/analyzer.cpp
  src/ir/ir.cpp
  src/bytecode/bytecode.cpp
  src/vm/vm.cpp
  src/runtime/kernel.cpp
  src/runtime/resource.cpp
  src/sim/simulator.cpp
)
target_include_directories(masl_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(masl_core PUBLIC masl_project_options)
target_compile_definitions(masl_core PRIVATE
  MASL_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  MASL_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  MASL_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

add_executable(maslc cli/maslc/main.cpp)
target_link_libraries(maslc PRIVATE masl_core masl_project_options)

add_executable(masl cli/masl/main.cpp)
target_link_libraries(masl PRIVATE masl_core masl_project_options)

add_executable(maslsim cli/maslsim/main.cpp)
target_link_libraries(maslsim PRIVATE masl_core masl_project_options)

# Shared application bridge used by the Android Studio front end and host tests.
add_library(masl_mobile_bridge STATIC
  android/app/src/main/cpp/masl_mobile_bridge.cpp
)
target_include_directories(masl_mobile_bridge PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp>
)
target_link_libraries(masl_mobile_bridge PUBLIC masl_core masl_project_options)

if(MASL_BUILD_TESTS)
  include(CTest)
  enable_testing()

  if(MASL_USE_SYSTEM_GTEST)
    find_package(GTest QUIET)
  endif()

  if(GTest_FOUND)
    add_executable(masl_foundation_tests tests/gtest_foundation_tests.cpp)
    target_link_libraries(masl_foundation_tests PRIVATE masl_core GTest::gtest_main masl_project_options)
    include(GoogleTest)
    gtest_discover_tests(masl_foundation_tests)
    set(MASL_TEST_FRAMEWORK_NAME "GoogleTest")
  else()
    add_executable(masl_foundation_tests tests/foundation_tests.cpp)
    target_link_libraries(masl_foundation_tests PRIVATE masl_core masl_project_options)
    add_test(NAME masl_foundation_unit_tests COMMAND masl_foundation_tests)
    set(MASL_TEST_FRAMEWORK_NAME "C++20 self-contained test harness (GoogleTest unavailable)")
  endif()

  add_executable(masl_lexer_tests tests/lexer_tests.cpp)
  target_link_libraries(masl_lexer_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_lexer_tests COMMAND masl_lexer_tests)

  add_executable(masl_parser_tests tests/parser_tests.cpp)
  target_link_libraries(masl_parser_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_parser_tests COMMAND masl_parser_tests)

  add_executable(masl_semantic_tests tests/semantic_tests.cpp)
  target_link_libraries(masl_semantic_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_semantic_tests COMMAND masl_semantic_tests)

  add_executable(masl_ir_tests tests/ir_tests.cpp)
  target_link_libraries(masl_ir_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_ir_tests COMMAND masl_ir_tests)

  add_executable(masl_bytecode_tests tests/bytecode_tests.cpp)
  target_link_libraries(masl_bytecode_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_bytecode_tests COMMAND masl_bytecode_tests)

  add_executable(masl_vm_tests tests/vm_tests.cpp)
  target_link_libraries(masl_vm_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_vm_tests COMMAND masl_vm_tests)

  add_executable(masl_runtime_tests tests/runtime_tests.cpp)
  target_link_libraries(masl_runtime_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_runtime_tests COMMAND masl_runtime_tests)

  add_executable(masl_queue_tests tests/queue_tests.cpp)
  target_link_libraries(masl_queue_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_queue_tests COMMAND masl_queue_tests)

  add_executable(masl_resource_tests tests/resource_tests.cpp)
  target_link_libraries(masl_resource_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_resource_tests COMMAND masl_resource_tests)

  add_executable(masl_simulator_tests tests/simulator_tests.cpp)
  target_link_libraries(masl_simulator_tests PRIVATE masl_core masl_project_options)
  add_test(NAME masl_simulator_tests COMMAND masl_simulator_tests)

  add_executable(masl_mobile_bridge_tests tests/mobile_bridge_tests.cpp)
  target_link_libraries(masl_mobile_bridge_tests PRIVATE masl_mobile_bridge masl_project_options)
  add_test(NAME masl_mobile_bridge_tests COMMAND masl_mobile_bridge_tests)

  add_test(NAME maslc_parse_fixture
    COMMAND maslc parse ${CMAKE_CURRENT_SOURCE_DIR}/examples/language_basics/arithmetic_pipeline.masl)
  set_tests_properties(maslc_parse_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Syntax OK: model ArithmeticPipeline")

  add_test(NAME maslc_parse_invalid
    COMMAND maslc parse ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/parser_invalid.masl)
  set_tests_properties(maslc_parse_invalid PROPERTIES WILL_FAIL TRUE)

  add_test(NAME maslc_ir_arithmetic_fixture
    COMMAND maslc ir ${CMAKE_CURRENT_SOURCE_DIR}/examples/language_basics/arithmetic_pipeline.masl)
  set_tests_properties(maslc_ir_arithmetic_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "MASL Typed IR 1.0.*model ArithmeticPipeline")

  add_test(NAME maslc_ir_component_hierarchy_fixture
    COMMAND maslc ir ${CMAKE_CURRENT_SOURCE_DIR}/examples/hierarchy/component_array.masl)
  set_tests_properties(maslc_ir_component_hierarchy_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "MASL Typed IR 1.0.*model ComponentArrayCheck")

  add_test(NAME maslc_ir_invalid
    COMMAND maslc ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/semantic_invalid.masl)
  set_tests_properties(maslc_ir_invalid PROPERTIES WILL_FAIL TRUE)

  add_test(NAME maslc_check_arithmetic_fixture
    COMMAND maslc check ${CMAKE_CURRENT_SOURCE_DIR}/examples/language_basics/arithmetic_pipeline.masl)
  set_tests_properties(maslc_check_arithmetic_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Semantic check OK: model ArithmeticPipeline")

  add_test(NAME maslc_check_component_array_fixture
    COMMAND maslc check ${CMAKE_CURRENT_SOURCE_DIR}/examples/hierarchy/component_array.masl)
  set_tests_properties(maslc_check_component_array_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Semantic check OK: model ComponentArrayCheck")

  add_test(NAME maslc_check_semantic_invalid
    COMMAND maslc check ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/semantic_invalid.masl)
  set_tests_properties(maslc_check_semantic_invalid PROPERTIES WILL_FAIL TRUE)

  add_test(NAME maslc_lex_fixture
    COMMAND maslc lex ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexer_sample.masl)
  set_tests_properties(maslc_lex_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Lexed [0-9]+ token")

  add_test(NAME maslc_lex_invalid
    COMMAND maslc lex ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexer_invalid.masl)
  set_tests_properties(maslc_lex_invalid PROPERTIES WILL_FAIL TRUE)

  set(MASL_TEST_MASB "${CMAKE_CURRENT_BINARY_DIR}/ExecutableFunctions.masb")
  add_test(NAME maslc_compile_rejects_hardware_loss
    COMMAND ${CMAKE_COMMAND}
      -DMASLC=$<TARGET_FILE:maslc>
      -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/examples/language_basics/arithmetic_pipeline.masl
      -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/must_not_exist_hardware.masb
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tests/expect_compile_rejected.cmake)

  add_test(NAME maslc_compile_executable_fixture
    COMMAND maslc compile ${CMAKE_CURRENT_SOURCE_DIR}/examples/language_basics/executable_functions.masl -o ${MASL_TEST_MASB})
  set_tests_properties(maslc_compile_executable_fixture PROPERTIES
    PASS_REGULAR_EXPRESSION "MASB compiled:.*5 function")

  add_test(NAME masl_verify_masb_fixture COMMAND masl verify ${MASL_TEST_MASB})
  set_tests_properties(masl_verify_masb_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "MASB verified: ExecutableFunctions")

  add_test(NAME maslc_disasm_masb_fixture COMMAND maslc disasm ${MASL_TEST_MASB})
  set_tests_properties(maslc_disasm_masb_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "PUSH_I64")

  add_test(NAME masl_run_answer_fixture COMMAND masl run ${MASL_TEST_MASB} answer)
  set_tests_properties(masl_run_answer_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "42")

  add_test(NAME masl_run_sum_fixture COMMAND masl run ${MASL_TEST_MASB} sum_to)
  set_tests_properties(masl_run_sum_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "45")

  add_test(NAME masl_run_call_arguments_fixture COMMAND masl run ${MASL_TEST_MASB} twice 21)
  set_tests_properties(masl_run_call_arguments_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "42")

  add_test(NAME masl_run_negative_branch_fixture COMMAND masl run ${MASL_TEST_MASB} choose -7)
  set_tests_properties(masl_run_negative_branch_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture
    PASS_REGULAR_EXPRESSION "0")

  add_test(NAME masl_run_wrong_arguments_fixture COMMAND masl run ${MASL_TEST_MASB} twice not-an-integer)
  set_tests_properties(masl_run_wrong_arguments_fixture PROPERTIES
    DEPENDS maslc_compile_executable_fixture WILL_FAIL TRUE)

  add_test(NAME maslsim_cpu_smoke COMMAND maslsim cpu 5000)
  set_tests_properties(maslsim_cpu_smoke PROPERTIES PASS_REGULAR_EXPRESSION "Status: completed")
  add_test(NAME maslsim_gpu_smoke COMMAND maslsim gpu 16 4096 42 coalesced)
  set_tests_properties(maslsim_gpu_smoke PROPERTIES PASS_REGULAR_EXPRESSION "Status: completed")
  add_test(NAME maslsim_gpu_strided_smoke COMMAND maslsim gpu 16 4096 42 strided)
  set_tests_properties(maslsim_gpu_strided_smoke PROPERTIES PASS_REGULAR_EXPRESSION "Status: completed")

  add_test(NAME maslsim_version COMMAND maslsim --version)
  set_tests_properties(maslsim_version PROPERTIES PASS_REGULAR_EXPRESSION "MASL Architecture Simulator 0.10.0")

  add_test(NAME maslc_version COMMAND maslc --version)
  set_tests_properties(maslc_version PROPERTIES PASS_REGULAR_EXPRESSION "MASL Compiler 0.10.0")

  add_test(NAME masl_version COMMAND masl --version)
  set_tests_properties(masl_version PROPERTIES PASS_REGULAR_EXPRESSION "MASL Runtime 0.10.0")

  add_test(NAME maslc_help COMMAND maslc --help)
  set_tests_properties(maslc_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: maslc")

  add_test(NAME masl_help COMMAND masl --help)
  set_tests_properties(masl_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: masl")

  message(STATUS "MASL test framework: ${MASL_TEST_FRAMEWORK_NAME}")
endif()

message(STATUS "MASL version: ${PROJECT_VERSION}")
message(STATUS "MASL C++ standard: C++${CMAKE_CXX_STANDARD}")
