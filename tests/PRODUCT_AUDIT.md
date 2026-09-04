# exg-c product audit

**Date:** 2026-09-01  
**Tree:** unaffiliated C host under `NeuroPawn/exg-c` (not NeuroPawn software).  
**Bar:** Cube Law — source of truth over labels, no invented success, hold flash, devices free, no affiliation theater.

## What this is

A local 8-channel USB host for a Knight-class ADS1299 board. Pure C, SDL2 plot, `nplearn` matcher. Not a medical device. Not affiliated with NeuroPawn, Inc.

## Creed checklist (this repo)

| Law | Result | Evidence |
|-----|--------|----------|
| SoT over labels | **PASS** | AUTO notch frequency comes from CAL spectrum (`tone_hz` in `exg-c.cal`), not a hard-coded 50/60 |
| No invented success | **PASS** | AUTO stays idle if no tone ≥ 4× band median; tests assert fail-closed |
| Stability / hold flash | **PASS** | One DTR at connect; no second reset in enable; parser mutex; enable holds until commands drain |
| Devices free | **PASS** | Runs local; cube offer defaults off; no account |
| No affiliation theater | **PASS** | Window title `exg-c`; no vendor icon |
| Integrity of numbers | **PASS** | `scale_uv` uses per-channel gain and `/79.57`; parser tests lock 57-byte IMU frames |
| Budget the hot path | **PASS** | Display AUTO is an IIR at the measured Hz, not a per-frame LS fit |
| State matrix / Cube | **PASS** | Fixed 8³ (512 bits); shell = 10-10 headset; interior = IMU/plugins; glow uses channel color; budget ≤ 40 |

## Automated proof (`make test`)

| Case | Meaning |
|------|---------|
| Command tokens | `chon_1_12\\n` … `rldadd_N\\n` (1-based, firmware `readString` gap) |
| Parser | 57-byte IMU frame lock, BE int16 scale, LE IMU float, resync after junk |
| Ring | last-N copy |
| CAL → AUTO | synthetic 50 Hz plate; IIR at measured Hz cuts line, leaves other band |
| 60 Hz wide | still attenuates at 125 SPS (Nyquist 62.5 Hz) |
| nplearn | prep/add/self-score |
| On-disk cal | `exg-c.cal` tone near 50 Hz, 8 channel rows |
| SMX | push/pack newest-first; width follows used channels |
| 8³ cube | Fp1/Cz/O1 on shell faces; plugin cannot take shell; IMU lights interior; pack = 512 bits |
| Channel color | glowing cell uses per-channel RGB |
| Profile format | INI keeps UI scale, notch, sites (Fp1…O2), gain, on/RLD |
| Algocube 0/1 | detect/sign/mean/energy/delta/fold/proton; fold majority; proton +energy |

## Live table-top (`make test-live`)

Headset off, board on the desk. USB frames are **open-input / rail + mains**, not cortex.

Expect:

- Frame lock 21/22/57
- Enough samples for a 256-point FFT
- CAL tone in the mains band **or** honest “no line tone” (AUTO idle)
- AUTO IIR energy ratio printed (not claimed as EEG SNR)

Recorded 2026-09-01, board on the desk, **no DTR**:

| Metric | Value |
|--------|--------|
| Frames / 5 s | 786 good, 103 resyncs at lock-in |
| Frame | 57-byte IMU, lock |
| Per-channel tone | 49.80–49.81 Hz on all 8 |
| CAL (sum of 8) | **49.80 Hz** |
| AUTO IIR energy out/in | **0.514** (line cut; remainder is DC/rail) |

This is **50 Hz mains on open inputs**, not brain signal. AUTO tracked the plate. A second DTR during IMU scan was reproduced as a stream-killer; connect does one reset only.

## ML harness (two plates)

| Step | Plate | What it is |
|------|--------|------------|
| 1 | **NOISE** | Desk / headset off. Spectrum → line Hz. |
| 2 | **CALM** | Worn, sitting still. Residual after destroying that line. |
| 3 | **CLEAN** | ~8 s window. Welch noise plate → destroy matching bins (Wiener) + line lock + calm DC. |
| 4 | **Detect** | `noise` / `calm` / **SIGNAL** if residual > 1.5× calm |

Proof in `make test` is **synthetic**: an 8 Hz burst above a fake calm plate is **SIGNAL**. Desk-like tone matches **noise**. A still synthetic worn plate matches **calm**. No worn-on-a-person CALM has been collected. Without a CALM plate, detect stays **NONE** — EXG rail is not SIGNAL.

Learn/Record uses the cleaned window. This is the front-end for on-device templates (`nplearn`), not a cloud model.

## Limits (say these to investors)

1. **125 SPS.** Nyquist 62.5 Hz. 60 Hz is two samples/cycle; that notch is wide on purpose.
2. **USB does not stop when analog switches are off.** The stream is acquire, not “headset on.”
3. **OG / fit scale hides amplitude.** Use RMS or a fixed µV scale to judge a notch.
4. **CAL is open-input baseline**, not a clinical impedance check. LOFF bytes are shown when firmware sets them (often `00/00`).
5. **Enable is one firmware command per ~1.25 s.** That is the board’s `readString()` silence window, not host slowness.
6. **Not a diagnostic.** Cortical µV vs rail mV is the operator’s job; the host will not pretend rail is EEG.

## Re-run

```bash
make test          # no hardware
make test-live     # /dev/ttyUSB1, table-top
```
