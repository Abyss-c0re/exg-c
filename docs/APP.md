# App (2.57)

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

## Defaults (first load, then `set_gen=4`)

| Setting | Value | Why |
|---------|--------|-----|
| band | line-kill | EXG after common mode |
| notch | AUTO (−1) | idle until a desk plate locks a line |
| hp | 2 Hz | drops DC/slow rail |
| lp | off | does not squash EXG |
| CAR | on | worn raw is lockstep millivolt |
| envelope | off | envelope hid EXG |
| detrend | on | plot is EXG, not the floor |
| DC cut (`cal_cut`) | on | still-plate mean |
| scale | ±1000 µV | EXG hundreds of µV, clench ~mV |
| window | 2 s | 250 samples — **below** Wiener’s 256 |
| API | **off** | turn on in Settings if you want LAN |

After `set_gen=4`, saved ini wins. API is not forced on at boot. `set_gen=3` remounts the pair belt once. `set_gen=4` applies pair colors (white / yellow / cyan / red).

Worn raw on this head is typically **200–300 µV**. Off-head / open inputs spike toward **1 mV**. That is contact vs antenna, not a stronger brain signal. After CAR+hp+notch, rest EXG is tens–hundreds of µV; a jaw clench is several times that.

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

Viz is the crimson 8³ lattice. One mapped 10-10 cell per channel tracks EXG µV — same color and millivolt as the traces. Pair EXG `A−B` draws a crimson link between the two sites. **float on** levitates; **float off** is manual drag-spin and +/− zoom. Map still assigns 10-10 sites.

## Quest / phone

- Package `com.abysscore.exgc`. Quest uses `com.oculus.intent.category.2D`.
- Stream service is `startService` from a visible activity, then `startForeground` (`dataSync`). Not `startForegroundService`.
- Service stays up while the API **server** is on **or** the app is connected (USB or API client).
- **USB / LAN / Bluetooth** next to Connect. USB is the Knight here. Bluetooth picks a nearby name. The share shows **Allow / No** on the app (and a notification). After Allow, EXG rides that link. LAN is EXG on wifi after a pair that saw wifi. Share EXG is how this device offers the board out.
- **CSV** asks where to save (system picker), then records cooked samples until **Stop CSV**.
- **Send mine / Take theirs / Both ways** copies map, colors, filters, and named profiles over EXG. Saved here so you can unplug and plug the board on the other device.
- Files live under the app directory (see android README). `run-as` cannot read them unless the package is debuggable.
