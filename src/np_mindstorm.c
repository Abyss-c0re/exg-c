#include "np_mindstorm.h"

#include "ms_decim.h"
#include "ms_link.h"
#include "ms_util.h"
#include "ms_wind.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* Phone-side wind client. The cook thread holds live_mu and then this mutex.
 * JNI takes only this mutex, never live_mu. Do not invert that order. */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static ms_decim decim;
static ms_link link;
static int inited;
static int wind_on = 1;
static int rate_ok;
static int rate_bad;
static char st[48] = "disconnected";

/* Caller holds the MindStorm mutex. The first call clears the session. */
static void ensure(void)
{
    if (inited) {
        return;
    }
    ms_decim_init(&decim);
    ms_link_init(&link);
    snprintf(st, sizeof st, "disconnected");
    inited = 1;
}

/* Caller holds the MindStorm mutex. A refused rate hides auth. */
static void refresh_status(void)
{
    if (rate_bad) {
        snprintf(st, sizeof st, "rate not supported");
    } else if (ms_link_authed(&link)) {
        snprintf(st, sizeof st, "authed");
    } else {
        snprintf(st, sizeof st, "disconnected");
    }
}

/* Samples per second, rounded to an int. A non-finite, non-positive, or huge
 * value is 0. 200 stays 200 and does not snap. */
static int round_sps(float sps)
{
    if (!(sps > 0.f) || sps > 100000.f) {
        return 0;
    }
    return (int)(sps + 0.5f);
}

/* Cooked microvolts, one vector. Called from live_sync_u on the reader or the
 * UI thread, which already holds live_mu. A null v, or n outside 1..8, returns
 * without touching the ball. Any other rate than 125, 250, or 500, including
 * 200, sets "rate not supported" and does not decimate. A closed 10 ms window
 * is queued as WIND only when forwarding is on and the session is authed. */
void np_mindstorm_on_sample(const float *v, int n, float sps)
{
    int rate;
    int closed;
    int out_n = 0;
    int16_t out[MS_DECIM_CH];

    if (!v || n < 1 || n > MS_DECIM_CH) {
        return;
    }
    pthread_mutex_lock(&mu);
    ensure();
    rate = round_sps(sps);
    if (!ms_sps_ok(rate)) {
        rate_ok = 0;
        rate_bad = 1;
        refresh_status();
        pthread_mutex_unlock(&mu);
        return;
    }
    rate_ok = 1;
    rate_bad = 0;
    closed = ms_decim_push(&decim, v, n, rate, out, &out_n);
    if (closed == 1 && wind_on && out_n > 0 && ms_link_authed(&link)) {
        ms_link_wind(&link, rate, out, out_n);
    }
    refresh_status();
    pthread_mutex_unlock(&mu);
}

/* 16-byte id and 32-byte token, any thread. A null pointer or a wrong length
 * returns -1 and leaves the session. Success resets the link, stores the
 * identity, and turns wind forwarding on. The token is not logged. */
int np_mindstorm_set_identity(const uint8_t *id, int id_n,
                              const uint8_t *token, int token_n)
{
    if (!id || !token || id_n != 16 || token_n != 32) {
        return -1;
    }
    pthread_mutex_lock(&mu);
    ensure();
    ms_link_init(&link);
    ms_decim_init(&decim);
    ms_link_set_identity(&link, id, token);
    wind_on = 1;
    refresh_status();
    pthread_mutex_unlock(&mu);
    return 0;
}

/* Ball bytes, any thread. n < 0, or a null buffer with n > 0, returns.
 * A challenge for this id queues a proof. AUTH_OK sets the session authed. */
void np_mindstorm_rx(const uint8_t *b, int n)
{
    if (n < 0 || (n > 0 && !b)) {
        return;
    }
    pthread_mutex_lock(&mu);
    ensure();
    ms_link_rx(&link, b, n);
    refresh_status();
    pthread_mutex_unlock(&mu);
}

/* Next radio frame, any thread. Returns the byte count copied into dst, or 0
 * when the queue is empty, dst is null, or cap is shorter than the frame. */
int np_mindstorm_tx(uint8_t *dst, int cap)
{
    int n;

    pthread_mutex_lock(&mu);
    ensure();
    n = ms_link_tx(&link, dst, cap);
    pthread_mutex_unlock(&mu);
    return n;
}

/* Any thread. Drops auth and queued frames, keeps a stored identity, and
 * queues HELLO. Returns -1 when no identity was stored or the queue is full.
 * Does not expect the USB banner. Wind forwarding is left as it was. */
int np_mindstorm_hello(void)
{
    uint8_t id[16];
    uint8_t token[32];
    int have = 0;
    int rc;

    pthread_mutex_lock(&mu);
    ensure();
    if (link.have_identity) {
        memcpy(id, link.id, sizeof id);
        memcpy(token, link.token, sizeof token);
        have = 1;
    }
    ms_link_init(&link);
    ms_decim_init(&decim);
    if (!have) {
        refresh_status();
        pthread_mutex_unlock(&mu);
        return -1;
    }
    ms_link_set_identity(&link, id, token);
    ms_memzero(token, (int)sizeof token);
    ms_memzero(id, (int)sizeof id);
    rc = ms_link_hello(&link);
    refresh_status();
    pthread_mutex_unlock(&mu);
    return rc;
}

/* 1 after AUTH_OK until AUTH_NO, a HELLO, or a new identity. Any thread. */
int np_mindstorm_authed(void)
{
    int on;

    pthread_mutex_lock(&mu);
    ensure();
    on = ms_link_authed(&link);
    pthread_mutex_unlock(&mu);
    return on;
}

/* 1 when closed windows may be queued. Defaults on, including before a token
 * is stored. Any thread. Frames are still withheld until the session is authed. */
int np_mindstorm_wind_enabled(void)
{
    int on;

    pthread_mutex_lock(&mu);
    ensure();
    on = wind_on ? 1 : 0;
    pthread_mutex_unlock(&mu);
    return on;
}

/* Any thread. A non-zero on allows WIND after auth. Zero stops those frames.
 * The exg-c traces are not affected. */
void np_mindstorm_set_wind(int on)
{
    pthread_mutex_lock(&mu);
    ensure();
    wind_on = on ? 1 : 0;
    pthread_mutex_unlock(&mu);
}

/* 1 when the latest cooked sample rounded to 125, 250, or 500. 0 before any
 * sample and after a refused rate, including 200. Any thread. */
int np_mindstorm_rate_ok(void)
{
    int ok;

    pthread_mutex_lock(&mu);
    ensure();
    ok = rate_ok ? 1 : 0;
    pthread_mutex_unlock(&mu);
    return ok;
}

/* Copies the status into dst, any thread. A null dst or n < 1 returns without
 * writing. The text is "rate not supported", "authed", or "disconnected",
 * and it is NUL-terminated when n > 0. */
void np_mindstorm_status(char *dst, int n)
{
    if (!dst || n < 1) {
        return;
    }
    pthread_mutex_lock(&mu);
    ensure();
    snprintf(dst, (size_t)n, "%s", st);
    pthread_mutex_unlock(&mu);
}
