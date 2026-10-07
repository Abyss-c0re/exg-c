#ifndef NP_TYPES_H
#define NP_TYPES_H

#include <stdint.h>

#define NP_NCHAN 8
#define NP_START 0xA0
#define NP_END 0xC0
#define NP_FRAME_EEG 21 /* NP_DEFAULT: 20-byte EEG block + 0xC0 */
#define NP_FRAME_IMU 57 /* NP_IMU: EEG block + 9×f32le + 0xC0 */
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
    NP_BOARD_KNIGHT_IMU = 1,
    NP_BOARD_AUTO = 2
};

static const int NP_GAINS[] = {1, 2, 3, 4, 6, 8, 12};
#define NP_NGAINS 7

/* Firmware applies only these. Any other value leaves the ADS1299 gain unchanged. */
static inline int np_gain_ok(int g)
{
    int i;
    for (i = 0; i < NP_NGAINS; i++) {
        if (NP_GAINS[i] == g) {
            return 1;
        }
    }
    return 0;
}

#endif
