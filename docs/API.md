# API (2.60)

C only (`include/np_api.h`, `src/np_api.c`). No Python in this tree.

Default: **off**. Settings → **API on**. Then:

| | |
|--|--|
| bind | **lan** `0.0.0.0` or **local** `127.0.0.1` |
| HTTP | **8765** (0 = off) |
| UDP | **8766** (0 = off) — **live path** |
| TCP | **8767** (0 = off) |
| rate | **1–125 Hz** (default 125) |
| push dest | empty until you type `host:port` |
| token | optional. Loopback GET needs none. LAN writes/streams need `X-EXG-Token` if set. |

Cook runs on the **USB reader** thread. The API thread wakes on a pipe and sends. Sockets do not block the cook. There is no fake sample delay.

`/` and `/health` advertise `"v":"2.60"`. GET `/status` is the same version. GET/POST `/kit` is map+settings text (no bind secrets). Live stream is **EXG1** binary. There is no `/stream.json`. GET/POST `/pair` is open (no token): first LAN connect. POST starts Allow/No on the share; GET polls `state` (`1` wait, `2` grant, `3` no).

`GET /cfg` is the settings mirror: API bind plus EXG filters, 8 colors, 8 10-10 names, active mask, and the ID line. Token value is never returned (only `true`/`false`). Dest and token are typed on the client. Loopback GET is open. LAN `/status` `/sample` `/cfg` need the lock word **or** a pair grant — `token:false` does not mean the LAN is open once a grant table exists.

## EXG1 frame (68 bytes, little-endian)

| Offset | Type | Field |
|--------|------|--------|
| 0 | `E X G 1` | magic |
| 4 | u32 | `seq` |
| 8 | u64 | `t_us` (`CLOCK_REALTIME`) |
| 16 | u32 | host frame count |
| 20 | u8 | `nch` |
| 21 | u8 | channel mask |
| 22 | u8 | clip mask |
| 23 | u8 | flags: bit0 connected, bit1 paused, bit2 id |
| 24 | 8 × f32 | cooked µV (ch 1..8) |
| 56 | f32 | measured sps |
| 60 | f32 | take-ID score (0 if no unique winner) |
| 64 | i8 | take-ID best index, or −1 |
| 65–67 | 0 | pad |

Cooked = the **display** cook (notch / hp / lp / CAR / envelope as set). Event ID cooks EXG separately and is not this frame.

`t_us` is wall-clock. Two machines’ clocks are not a latency number. UDP **PING/PONG** is the honest RTT.

## HTTP (control)

`GET /` and `GET /health` — no token.

| Method | Path | Body |
|--------|------|------|
| GET | `/` | index: bind, ports, hz, whether a token is set, get/post lists |
| GET | `/health` | `on`, bind, ports, client counts |
| GET | `/status` | host JSON (sps, id line, connected) |
| GET | `/sample` | last frame as JSON (pull, not 125 Hz) |
| GET | `/cfg` | `on`, bind, ports, hz, token flag, push dest |
| GET | `/stream` | `application/octet-stream`, `X-EXG-Format: EXG1`, one frame per sample |
| GET | `/pair` | current ask: `state`, `grant` if allowed. Open. |
| POST | `/pair` | body `{"name":"…"}`. Starts Allow on the share. Open. |
| POST | `/connect` `/disconnect` `/pause` | queued; applied on the host tick |
| POST | `/cfg` | JSON ints: `on`, `bind`/`lan`, `http`, `udp`, `tcp`, `hz` |

```bash
curl -s http://127.0.0.1:8765/health
curl -s http://127.0.0.1:8765/sample
curl -s -H 'X-EXG-Token: secret' http://127.0.0.1:8765/status
```

Use loopback in examples. Do not bake a LAN unicast or a lab hostname into a dest.

## UDP (live)

Subscribe with `SUB1` plus the pair grant (4-byte prefix, then the grant). PING does not subscribe. Tests with no grant table still take any non-PING datagram. The host sends EXG1 to subscribers and to an optional typed push dest.

**PING** (12 bytes): `PING` + 8-byte little-endian client stamp.  
**PONG** (20 bytes): `PONG` + same stamp + 8-byte server `CLOCK_REALTIME` µs.  
PING does **not** subscribe.

```bash
make recv
./tools/exg-recv --port 8766 --seconds 8
./tools/exg-recv --ping 127.0.0.1:8766
```

`age_us` in the recv tool is wall-clock minus `t_us`. If the clocks disagree it can be negative. RTT from PING/PONG is the net number.

## TCP

Connect to `:8767` and read 68-byte EXG1 frames. Same cook as UDP.

## C

```c
#include "np_api.h"

struct np_api_cfg c;
np_api_cfg_default(&c);   /* on = 0 */
c.on = 1;
c.lan = 0;                /* 127.0.0.1 */
c.http = 8765;
c.udp = 8766;
np_api_apply(&c);

struct np_api_sample s;
unsigned char raw[NP_API_FRAME];
np_api_pack(raw, sizeof(raw), &s);
np_api_unpack(raw, NP_API_FRAME, &s);
```

`np_api_push` queues a frame and kicks the pipe. Host code must not `send` on the cook thread.

## What this is not

- Not a JSON 125 Hz stream.
- Not NTP sync. VR match is `seq` + RTT.
- Not on unless you turn it on.
- Push dest is empty until typed. A saved dest that starts with `192.` is stripped on boot (old leak bandage).
