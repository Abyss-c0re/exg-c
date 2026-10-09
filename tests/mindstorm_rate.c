#include "np_mindstorm.h"
#include "ms_frame.h"

#include <stdio.h>
#include <string.h>

static int fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        fail = 1;
    }
}

static void drain(void)
{
    uint8_t buf[256];
    int i;
    for (i = 0; i < 16; i++) {
        if (np_mindstorm_tx(buf, (int)sizeof buf) <= 0) {
            return;
        }
    }
}

static int count_wind(float sps, int samples)
{
    float v[8];
    uint8_t buf[256];
    int i;
    int w = 0;
    for (i = 0; i < 8; i++) {
        v[i] = (float)(i + 1);
    }
    for (i = 0; i < samples; i++) {
        np_mindstorm_on_sample(v, 8, sps);
    }
    for (i = 0; i < 16; i++) {
        int n = np_mindstorm_tx(buf, (int)sizeof buf);
        if (n <= 0) {
            break;
        }
        if (buf[0] == 0x40) {
            w++;
        }
    }
    return w;
}

static int await_wind(float sps, int samples, int expect_sps)
{
    float v[8];
    uint8_t buf[256];
    int i;
    for (i = 0; i < 8; i++) {
        v[i] = 10.f * (float)(i + 1);
    }
    for (i = 0; i < samples; i++) {
        int n;
        int got;
        np_mindstorm_on_sample(v, 8, sps);
        n = np_mindstorm_tx(buf, (int)sizeof buf);
        if (n <= 0) {
            continue;
        }
        if (buf[0] != 0x40 || buf[6] != 8) {
            fprintf(stderr, "FAIL wind shape type %u nch %u\n", buf[0], buf[6]);
            fail = 1;
            return 0;
        }
        got = (int)buf[4] | ((int)buf[5] << 8);
        if (got != expect_sps) {
            fprintf(stderr, "FAIL wind sps %d want %d\n", got, expect_sps);
            fail = 1;
            return 0;
        }
        return 1;
    }
    return 0;
}

static void auth(void)
{
    uint8_t id[16];
    uint8_t tok[32];
    uint8_t nonce[16];
    uint8_t frame[64];
    uint8_t buf[256];
    int n;

    memset(id, 0x11, sizeof id);
    memset(tok, 0x22, sizeof tok);
    memset(nonce, 0x5a, sizeof nonce);
    expect(np_mindstorm_hello() == -1, "hello before identity");
    expect(np_mindstorm_set_identity(0, 0, 0, 0) == -1, "null identity");
    expect(np_mindstorm_set_identity(id, 15, tok, 32) == -1, "short id");
    np_mindstorm_set_wind(0);
    expect(np_mindstorm_wind_enabled() == 0, "wind forced off");
    expect(np_mindstorm_set_identity(id, 16, tok, 32) == 0, "identity");
    expect(np_mindstorm_wind_enabled() == 1, "wind armed on");
    expect(np_mindstorm_hello() == 0, "hello");
    n = np_mindstorm_tx(buf, (int)sizeof buf);
    expect(n > 0 && buf[0] == 0x10, "hello frame");
    n = ms_frame_pack(frame, (int)sizeof frame, 0x11, 0, nonce, 16);
    expect(n > 0, "pack challenge");
    np_mindstorm_rx(frame, n);
    n = np_mindstorm_tx(buf, (int)sizeof buf);
    expect(n > 0 && buf[0] == 0x12, "proof frame");
    n = ms_frame_pack(frame, (int)sizeof frame, 0x13, 0, 0, 0);
    expect(n > 0, "pack auth ok");
    np_mindstorm_rx(frame, n);
    expect(np_mindstorm_authed() == 1, "authed");
}

int main(void)
{
    char st[64];

    auth();
    expect(count_wind(200.f, 400) == 0, "200 produced wind");
    np_mindstorm_status(st, (int)sizeof st);
    expect(strcmp(st, "rate not supported") == 0, st);
    expect(np_mindstorm_rate_ok() == 0, "200 rate ok");
    expect(np_mindstorm_authed() == 1, "200 cleared auth");
    expect(count_wind(200.4f, 20) == 0, "200.4 produced wind");
    expect(count_wind(199.6f, 20) == 0, "199.6 produced wind");

    expect(await_wind(125.f, 8, 125), "125 wind");
    np_mindstorm_status(st, (int)sizeof st);
    expect(strcmp(st, "authed") == 0, st);
    expect(np_mindstorm_rate_ok() == 1, "125 rate");
    drain();

    expect(await_wind(250.f, 16, 250), "250 wind");
    drain();
    expect(await_wind(500.f, 16, 500), "500 wind");
    drain();

    np_mindstorm_set_wind(0);
    expect(np_mindstorm_wind_enabled() == 0, "wind off");
    expect(count_wind(125.f, 40) == 0, "suppressed wind");
    np_mindstorm_status(st, (int)sizeof st);
    expect(strcmp(st, "authed") == 0, "suppressed status");
    np_mindstorm_set_wind(1);
    expect(await_wind(125.f, 8, 125), "wind resumed");

    expect(count_wind(0.f, 4) == 0, "zero rate wind");
    np_mindstorm_status(st, (int)sizeof st);
    expect(strcmp(st, "rate not supported") == 0, "zero status");

    if (fail) {
        fprintf(stderr, "np_mindstorm test failed\n");
        return 1;
    }
    printf("np_mindstorm test ok\n");
    return 0;
}
