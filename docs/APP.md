# App (2.80)

What the host does. The LAN wire is [API.md](API.md).

## Stack

```
Knight USB (FTDI 0403:6001, 8ch, 125 SPS)
        │
        ▼
src/np_core.c     cook on the USB reader thread
  np_host_*       product calls for any UI
  np_api.c        optional LAN (off until you turn it on)
        │
        ├─ src/np_ui.c          Linux SDL
        └─ android Java + JNI   Quest / phone, libexg.so
```

Android does **not** compile `np_ui.c`. Java talks to `include/np_host.h` via `src/np_android_jni.c`.

## Defaults (first load, then `set_gen=5`)

| Setting | Value | Why |
|---------|--------|-----|
| band | raw | same as official Knight plot |
| notch | off | official does not cook |
| hp | off | off-head is a DC rail — keep it |
| lp | off | |
| CAR | off | CAR hides the common off-head rail |
| envelope | off | |
| detrend | off | |
| DC cut (`cal_cut`) | off | |
| scale | ±1000 µV | worn is hundreds of µV; off-head rails the plot |
| window | 2 s | 250 samples |
| API | **off** | turn on in Settings if you want LAN |

After `set_gen=5`, saved ini wins. `set_gen=5` forces raw once so an old line-kill ini cannot hide the board. Line-kill / EEG / EMG stay as bands.

Scale is `4/(2^15-1)/gain*1e6/79.57` (~±4.2 mV at gain 12). That is the official Knight conversion (fixed analog front-end 79.57). CLIP, plot, plates, and ID use that unit. Worn raw is tens to hundreds of µV. Off-head approaches the 4 mV rail. **line-kill** (hp 2, CAR, detrend) is a cook. It is not the board.

## Knight wire

Follows the published [firmware](https://docs.neuropawn.tech/knight-board/firmware/), [data format](https://docs.neuropawn.tech/knight-board/data-format/), and [command set](https://docs.neuropawn.tech/knight-board/command-set/).

| Item | Host |
|------|------|
| Serial | 115200 8N1 |
| Frames | `0xA0` … `0xC0`. EEG-only **21** bytes (`NP_DEFAULT`). IMU **57** bytes (9× little-endian f32). Hunt still accepts a leftover 22-byte lock. |
| Scale | `4/(2^15-1)/79.57/gain*1e6` µV. Gains `1 2 3 4 6 8 12`. |
| Commands | `chon_{ch}_{gain}`, `choff_{ch}`, `rldadd_{ch}`, `rldremove_{ch}` |
| Connect | Wait for the binary stream, **2 s** settle, then per active channel `chon_` then `rldadd_`/`rldremove_` with ≥1 s between commands. Channels start off on the board. |

Do not write other text on the USB serial link. Do not shell CubalC at 125 Hz.

## DC vs CLEAN

| Chrome | Machine |
|--------|---------|
| **DC on** (teal) | Subtract still-plate mean (`np_sub_dc`). Default 2 s window. |
| **DC off** (dark) | No DC subtract, no Wiener. |
| **CLEAN on** (teal) | Wiener vs desk noise plate. Needs a noise plate **and** window ≥ 3 s. |

DC is **direct current** — the standing offset, not a rhythm. CLEAN is not advertised unless Wiener can run.

## Calibrate

1. Phase 5 — **5 s** to put the headset on the desk/floor.
2. Phase 1 — **8 s** desk plate (NOISE). Line tone + per-channel PSD.
3. Tap when worn.
4. Phase 3 — **8 s** sit still (CALM). Stores DC + residual RMS.
5. Done.

Do not treat the countdown as “tap to cancel.” A second tap during 1/3/5 is a no-op.

## ID (live event)

Cooked independently of the plot: hp 2 + notch + CAR + detrend. **No envelope, no LP.**

Last ~0.5 s EXG vs a rolling quiet baseline (`id_base` EMA while still).

| Label | Rule |
|-------|------|
| `ID warming` | connected but &lt; 80 sps |
| `ID need CALM` | no baseline yet |
| `ID still Nx` | quiet vs baseline |
| `ID blink Nx` | Fp pair hot, rest quiet |
| `ID clench Nx` | ≥4 channels ≥1.8× |
| `ID burst Nx` | one-hot burst |
| `ID rail` / `ID CLIP` | open rail / ≥6 channels window-clip |

`Nx` is a **ratio**, not a percent. Hamming and RMS cosine are not ID.

## Takes and MATCH

- **Take** — bookmark a named stretch (`atoms/<name>.npat`, raw next to it).
- Live take ID: **last 1 s** vs each take’s **pattern** of seconds unlike rest/CALM. Fail-closed: unique winner ≥ **70%** and ≥ **8 pt** gap, or no percent.
- Compare two takes: `same head — not distinct`, `distinct`, or `no RMS`. No whole-file percent.
- **Record** — 1 s pose in `exg-c.learn`. MATCH **names** a unique pose. Cosine is not printed as `%`.
- Chips on Android are **takes**. Record poses are listed separately and can be deleted.

## Bias (RLD)

Per-channel **bias ON** (teal) / **bias off** (dark). Connect sends `RLDADD` or `RLDRM` for every active pin. A saved off stays off.

## Plot

- Off channels are hidden on traces, map, and channel row.
- **Pause** freezes any channel that already has samples (not only ch 0).
- FFT marks the **effective** notch only. AUTO with no plate → no 50 Hz sticker.
- CLIP on the plot is `|v| > 4000 µV` after the view cook. CAR only skips **rails** (±250000 µV), not 4 mV.

## Bands

| Band | hp | lp | CAR | envelope | scale |
|------|----|----|-----|----------|-------|
| raw | 0 | 0 | off | off | (unchanged) |
| line-kill | 2 | 0 | on | off | 1000 |
| EEG | 2 | 40 | on | off | 200 |
| EMG | 20 | 0 | on | on | 2000 |

Profile load keeps the electrode map and **recooks** plates/takes from raw. It does not re-record.

## Cube

The Cube tab **creates cubes**. One cube is a 2×2×2 of 8 bits, one cell per channel. 8 jacks → 1 cube; 16 → 2. Bit buttons 1–8 pick the channel. The voxel label is `N·chM`. Color tints the whole lattice.

Tap the **cube name** (`cube 1 · sign`) to set the algo for the whole cube. Tap a **channel** to override that cell — first choice is **same as cube**. Hold a channel to pick its jack. Buttons stay still. Only lattice cells light. CubalC uses **ON** / **OFF**. A program can light a named channel: `if ch2 < ch5 then ch3 ON`.

## Algos

A separate tab. Defaults are CubalC you can read (and reset). Add your own. **use on cube** puts the selected program on all 8 bits. **help** opens the syntax. **save CubalC** compiles first — a broken program is not saved.

| Name | CubalC |
|------|--------|
| detect | `if signal == 1 then 1` |
| sign | `if ch > 0 then 1` |
| mean | `if abs(ch) > 0.85 * mean then 1` |
| energy | `if rms > 1.05 * mean then 1` |
| delta | `if abs(ch - prev) > 1.10 * dxmean then 1` |
| fold | `if above > 0.5 then 1` |
| proton | `if pos > 0.5 then 1` |
| compare | `if ch2 < ch5 then ch3 ON` |

A block after `THEN` can set several channels. `ELSE IF` works. Compares are 0/1, so `(ch1>0)+(ch2>0)>1` means two high. Logic: `AND` `OR` `NOT` (`&&` `||` `!`). Math: `+ - * / % abs() min() max() sqrt() pow(a, b)`. `LET name = expr`, `LET name, expr`, or CubalC’s `LET name expr`. `FOR <seconds>` keeps the body on for that many seconds even if the outer `IF` later misses — not official CubalC `FOR i = a TO b` (count loop). `UNTIL <cond>` keeps the body on until the cond is true (`UNTIL ch4 < ch2`) — one pass per tick, not CubalC’s busy `UNTIL` loop. Names: `ch`/`last`/`mean`/`rms`/`prev`/`dxmean`/`above`/`pos`/`signal`, and `ch1`…`ch8`. The cube is ON/OFF bits from this dialect. It does not import CubalC’s EEG feature-pack (50 µV polarity/energy bits).

**float on** levitates; **float off** is manual drag-spin and +/− zoom. Sites live in Settings.

## Quest / handheld

- Package `com.abysscore.exgc`. Quest uses `com.oculus.intent.category.2D`.
- Stream service is `startService` from a visible activity, then `startForeground` (`dataSync`). Not `startForegroundService`.
- Service stays up while the API **server** is on **or** the app is connected (USB or LAN client).
- **USB / LAN** next to Connect. USB is the Knight here. LAN types a dest (`host` or `host:8765`). First connect POSTs `/pair`. The share shows **Allow / No** on the app **and** in a notification. After Allow, EXG rides UDP. Share EXG is how this device offers the board out. No Bluetooth. No nearby radio list.
- **CSV** asks where to save (system picker), then records cooked samples until **Stop CSV**.
- **Send mine / Take theirs / Both ways** copies map, colors, filters, and named profiles over EXG. Saved here so you can unplug and plug the board on the other device.
- Files live under the app directory (see android README). `run-as` cannot read them unless the package is debuggable.
