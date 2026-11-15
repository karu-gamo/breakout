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
add_executable(breakout WIN32 src/main.c)
target_compile_features(breakout PRIVATE c_std_11)
target_link_libraries(breakout PRIVATE SDL3::SDL3)
if(NOT MSVC)
    target_compile_options(breakout PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(breakout PRIVATE m)
endif()
set_target_properties(breakout PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
