cmake_minimum_required(VERSION 3.18)
project(exg-c-android C)

set(EXG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(exg SHARED
    ${EXG_ROOT}/src/np_state.c
    ${EXG_ROOT}/src/np_paths.c
    ${EXG_ROOT}/src/np_cfg.c
    ${EXG_ROOT}/src/np_learn.c
    ${EXG_ROOT}/src/np_take.c
    ${EXG_ROOT}/src/np_cook.c
    ${EXG_ROOT}/src/np_stream.c
    ${EXG_ROOT}/src/np_cal.c
    ${EXG_ROOT}/src/np_desk.c
    ${EXG_ROOT}/src/np_cube.c
    ${EXG_ROOT}/src/np_flash.c
    ${EXG_ROOT}/src/np_montage.c
    ${EXG_ROOT}/src/np_plot.c
    ${EXG_ROOT}/src/np_session.c
    ${EXG_ROOT}/src/np_share.c
    ${EXG_ROOT}/src/np_serial_android.c
    ${EXG_ROOT}/src/np_android_jni.c
    ${EXG_ROOT}/src/sdl_stub.c
    ${EXG_ROOT}/src/np_knight.c
    ${EXG_ROOT}/src/np_rate.c
    ${EXG_ROOT}/src/np_stk500.c
    ${EXG_ROOT}/src/np_fw.c
    ${EXG_ROOT}/src/np_ring.c
    ${EXG_ROOT}/src/np_dsp.c
    ${EXG_ROOT}/src/np_font.c
    ${EXG_ROOT}/src/np_smx.c
    ${EXG_ROOT}/src/np_algo.c
    ${EXG_ROOT}/src/np_sot.c
    ${EXG_ROOT}/src/np_atom.c
    ${EXG_ROOT}/src/np_api.c
    ${EXG_ROOT}/src/np_link.c
    ${EXG_ROOT}/src/np_peer.c
    ${EXG_ROOT}/nplearn/src/nplearn.c
    ${EXG_ROOT}/nplearn/src/nplearn_filt.c
    ${EXG_ROOT}/nplearn/src/nplearn_posix.c
)

target_include_directories(exg PRIVATE
    ${EXG_ROOT}/include
    ${EXG_ROOT}/src
    ${EXG_ROOT}/nplearn/include
)

target_compile_definitions(exg PRIVATE
    _DEFAULT_SOURCE
    NPL_POSIX
    __ANDROID__
    NP_ANDROID_UI
)

target_compile_options(exg PRIVATE -std=c11 -O2 -Wall -Wextra -fPIC)

target_link_libraries(exg
    android
    log
    m
    dl
)
