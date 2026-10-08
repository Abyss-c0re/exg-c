#ifndef NP_RING_H
#define NP_RING_H

#include "np_types.h"
#include <pthread.h>

struct np_sample;

struct np_ring {
    pthread_mutex_t mu;
    float ch[NP_NCHAN][NP_RING];
    float acc[3][NP_RING];
    uint32_t wr;
    uint64_t total;
    uint32_t good;
    uint32_t bad;
    uint32_t drops; /* sample-number gaps, wrap 256 */
    uint8_t loff_p;
    uint8_t loff_n;
    float imu_acc[3];
    float imu_gyr[3];
    float imu_mag[3];
    int imu_ok;
};

/* Zero the ring and init its mutex. A second call on the same ring leaks the first mutex. */
void np_ring_init(struct np_ring *r);
/* Store uv (µV) and the acc triple at wr, then count the sample as good. gyr, mag, and imu_ok update only when s->imu is set. drops is added. bad stays as it was. */
void np_ring_push(struct np_ring *r, const struct np_sample *s);
/* Copy the newest n samples of one channel, oldest first, in µV. n is clipped to what is stored, at most NP_RING. Returns 0 when ch is outside 0..7. */
uint32_t np_ring_copy(struct np_ring *r, int ch, float *dst, uint32_t n);
/* Snapshot total, good, and bad under the lock. Any pointer may be null. bad stays 0 unless some other caller writes it. */
void np_ring_stats(struct np_ring *r, uint64_t *total, uint32_t *good, uint32_t *bad);
/* Cumulative sample-number gaps added by push. */
uint32_t np_ring_drops(struct np_ring *r);
/* Last lead-off bytes. Bit 0 is channel 1. Either pointer may be null. */
void np_ring_loff(struct np_ring *r, uint8_t *p, uint8_t *n);
/* Last IMU that arrived with imu set: acc in m/s², gyr in rad/s, mag in µT. ok stays 0 until that first frame. Any array pointer may be null. */
void np_ring_imu(struct np_ring *r, float acc[3], float gyr[3], float mag[3], int *ok);

#endif
