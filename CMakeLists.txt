cmake_minimum_required(VERSION 3.25)
project(Gutenberg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(GUTENBERG_SANITIZE "Enable address and undefined behavior sanitizers" OFF)

find_package(ZLIB REQUIRED)
add_library(gutenberg_core
    core/src/bytes.cpp
    core/src/archive.cpp
    core/src/resources.cpp
)
target_include_directories(gutenberg_core PUBLIC core/include)
target_link_libraries(gutenberg_core PUBLIC ZLIB::ZLIB)
target_compile_options(gutenberg_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
if(GUTENBERG_SANITIZE)
    target_compile_options(gutenberg_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gutenberg_core PUBLIC -fsanitize=address,undefined)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    include(CTest)
    if(BUILD_TESTING)
        add_executable(core_tests tests/core_tests.cpp tests/resource_tests.cpp)
        target_link_libraries(core_tests PRIVATE gutenberg_core)
        add_test(NAME core_tests COMMAND core_tests)
    endif()
endif()
