#ifndef NP_TYPES_H
#define NP_TYPES_H

#include <stdint.h>

#define NP_NCHAN 8
#define NP_START 0xA0
#define NP_END 0xC0
#define NP_FRAME_EEG 22
#define NP_FRAME_IMU 57
#define NP_FRAME_MAX 64
#define NP_IMU_BYTES 36
#define NP_BAUD 115200
#define NP_DEFAULT_SPS 125
#define NP_RING 2048
#define NP_FFT_N 256
#define NP_MAX_PORTS 32
#define NP_MAX_PATH 256

enum np_board {
    NP_BOARD_KNIGHT = 0,
    NP_BOARD_KNIGHT_IMU = 1
};

static const int NP_GAINS[] = {1, 2, 3, 4, 6, 8, 12};
#define NP_NGAINS 7

#endif
