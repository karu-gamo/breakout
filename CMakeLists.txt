cmake_minimum_required(VERSION 3.24)
project(breakout VERSION 0.1.0 LANGUAGES C)
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)
# Embed the original PNG so releases can still ship a single executable.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/ball.png")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/ball.png" BALL_PNG_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," BALL_PNG_BYTES "${BALL_PNG_HEX}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/ball_png.h"
    "static const unsigned char ball_png[] = {${BALL_PNG_BYTES}};\n")
add_executable(breakout WIN32 src/main.c)
target_compile_features(breakout PRIVATE c_std_11)
target_include_directories(breakout PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(breakout PRIVATE SDL3::SDL3)
if(NOT MSVC)
    target_compile_options(breakout PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(breakout PRIVATE m)
endif()
set_target_properties(breakout PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")

option(BREAKBOUT_BUILD_TESTS "Build headless gameplay regression tests" OFF)
if(BREAKBOUT_BUILD_TESTS)
    enable_testing()
    add_executable(breakbout_tests tests/gameplay_test.c)
    target_compile_features(breakbout_tests PRIVATE c_std_11)
    target_include_directories(breakbout_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(breakbout_tests PRIVATE SDL3::SDL3)
    if(NOT MSVC)
        target_compile_options(breakbout_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(breakbout_tests PRIVATE m)
    endif()
    add_test(NAME gameplay COMMAND breakbout_tests)
    set_tests_properties(gameplay PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()
