cmake_minimum_required(VERSION 3.18)
project(exg-c-android C)

set(EXG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2")

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_DIR}" "${CMAKE_BINARY_DIR}/sdl2")

add_library(main SHARED
    ${EXG_ROOT}/src/main.c
    ${EXG_ROOT}/src/np_serial_android.c
    ${EXG_ROOT}/src/np_knight.c
    ${EXG_ROOT}/src/np_ring.c
    ${EXG_ROOT}/src/np_dsp.c
    ${EXG_ROOT}/src/np_font.c
    ${EXG_ROOT}/src/np_smx.c
    ${EXG_ROOT}/src/np_algo.c
    ${EXG_ROOT}/nplearn/src/nplearn.c
    ${EXG_ROOT}/nplearn/src/nplearn_filt.c
    ${EXG_ROOT}/nplearn/src/nplearn_posix.c
)

target_include_directories(main PRIVATE
    ${EXG_ROOT}/include
    ${EXG_ROOT}/src
    ${EXG_ROOT}/nplearn/include
)

target_compile_definitions(main PRIVATE
    _DEFAULT_SOURCE
    NPL_POSIX
    __ANDROID__
)

target_compile_options(main PRIVATE -std=c11 -O2 -Wall -Wextra -fPIC)

if(TARGET SDL2main)
    target_link_libraries(main SDL2main)
endif()
target_link_libraries(main
    SDL2
    android
    log
    m
    dl
)
target_link_options(main PRIVATE -Wl,-u,SDL_main)
