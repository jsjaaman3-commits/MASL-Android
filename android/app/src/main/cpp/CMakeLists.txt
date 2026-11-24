cmake_minimum_required(VERSION 3.22.1)
project(masl_android LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(MASL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

add_library(masl_core STATIC
    "${MASL_ROOT}/src/core/version.cpp"
    "${MASL_ROOT}/src/lexer/token.cpp"
    "${MASL_ROOT}/src/lexer/lexer.cpp"
    "${MASL_ROOT}/src/parser/parser.cpp"
    "${MASL_ROOT}/src/semantic/analyzer.cpp"
    "${MASL_ROOT}/src/ir/ir.cpp"
    "${MASL_ROOT}/src/bytecode/bytecode.cpp"
    "${MASL_ROOT}/src/vm/vm.cpp"
    "${MASL_ROOT}/src/runtime/kernel.cpp"
    "${MASL_ROOT}/src/runtime/resource.cpp"
    "${MASL_ROOT}/src/sim/simulator.cpp"
)
target_include_directories(masl_core PUBLIC "${MASL_ROOT}/include")
target_compile_definitions(masl_core PRIVATE
    MASL_VERSION_MAJOR=0 MASL_VERSION_MINOR=10 MASL_VERSION_PATCH=0
)
target_compile_options(masl_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2
)

add_library(masl_jni SHARED
    jni_bridge.cpp
    masl_mobile_bridge.cpp
)
target_include_directories(masl_jni PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(masl_jni PRIVATE masl_core)
target_compile_options(masl_jni PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2
)
