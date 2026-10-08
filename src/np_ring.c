#include "np_ring.h"
#include "np_knight.h"

#include <string.h>

/* Locked ring of newest EEG µV plus last IMU and lead-off.
 * push adds the sample drop count. Nothing here increments bad. */

/* Zero the ring and init its mutex. A second call on the same ring leaks the first mutex. */
void np_ring_init(struct np_ring *r)
{
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->mu, NULL);
}

/* Store uv (µV) and the acc triple at wr, then count the sample as good.
 * gyr, mag, and imu_ok update only when s->imu is set. drops is added. bad stays as it was. */
void np_ring_push(struct np_ring *r, const struct np_sample *s)
{
    uint32_t i, w;
    pthread_mutex_lock(&r->mu);
    w = r->wr % NP_RING;
    for (i = 0; i < NP_NCHAN; i++) {
        r->ch[i][w] = s->uv[i];
    }
    r->acc[0][w] = s->acc[0];
    r->acc[1][w] = s->acc[1];
    r->acc[2][w] = s->acc[2];
    if (s->imu) {
        int k;
        for (k = 0; k < 3; k++) {
            r->imu_acc[k] = s->acc[k];
            r->imu_gyr[k] = s->gyr[k];
            r->imu_mag[k] = s->mag[k];
        }
        r->imu_ok = 1;
    }
    r->loff_p = s->loff_p;
    r->loff_n = s->loff_n;
    r->wr++;
    r->total++;
    r->good++;
    r->drops += s->drops;
    pthread_mutex_unlock(&r->mu);
}

/* Copy the newest n samples of one channel, oldest first, in µV.
 * n is clipped to what is stored, at most NP_RING. Returns 0 when ch is outside 0..7. */
uint32_t np_ring_copy(struct np_ring *r, int ch, float *dst, uint32_t n)
{
    uint32_t have, i, start;
    if (ch < 0 || ch >= NP_NCHAN) {
        return 0;
    }
    pthread_mutex_lock(&r->mu);
    have = r->wr < NP_RING ? r->wr : NP_RING;
    if (n > have) {
        n = have;
    }
    start = (r->wr + NP_RING - n) % NP_RING;
    for (i = 0; i < n; i++) {
        dst[i] = r->ch[ch][(start + i) % NP_RING];
    }
    pthread_mutex_unlock(&r->mu);
    return n;
}

/* Snapshot total, good, and bad under the lock. Any pointer may be null.
 * bad stays 0 unless some other caller writes it. */
void np_ring_stats(struct np_ring *r, uint64_t *total, uint32_t *good, uint32_t *bad)
{
    pthread_mutex_lock(&r->mu);
    if (total) {
        *total = r->total;
    }
    if (good) {
        *good = r->good;
    }
    if (bad) {
        *bad = r->bad;
    }
    pthread_mutex_unlock(&r->mu);
}

/* Cumulative sample-number gaps added by push. */
uint32_t np_ring_drops(struct np_ring *r)
{
    uint32_t n;
    pthread_mutex_lock(&r->mu);
    n = r->drops;
    pthread_mutex_unlock(&r->mu);
    return n;
}

/* Last lead-off bytes. Bit 0 is channel 1. Either pointer may be null. */
void np_ring_loff(struct np_ring *r, uint8_t *p, uint8_t *n)
{
    pthread_mutex_lock(&r->mu);
    if (p) {
        *p = r->loff_p;
    }
    if (n) {
        *n = r->loff_n;
    }
    pthread_mutex_unlock(&r->mu);
}

/* Last IMU that arrived with imu set: acc in m/s², gyr in rad/s, mag in µT.
 * ok stays 0 until that first frame. Any array pointer may be null. */
void np_ring_imu(struct np_ring *r, float acc[3], float gyr[3], float mag[3], int *ok)
{
    int k;
    pthread_mutex_lock(&r->mu);
    if (ok) {
        *ok = r->imu_ok;
    }
    for (k = 0; k < 3; k++) {
        if (acc) {
            acc[k] = r->imu_acc[k];
        }
        if (gyr) {
            gyr[k] = r->imu_gyr[k];
        }
        if (mag) {
            mag[k] = r->imu_mag[k];
        }
    }
    pthread_mutex_unlock(&r->mu);
}
