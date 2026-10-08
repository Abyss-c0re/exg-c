# Android / Quest

Same C host as `./np-exg`. Serial is USB Host. UI is Java. Native library is `libexg.so` from `src/np_core.c` — not the desktop SDL file.

App: **3.03**, package `com.abysscore.exgc`, min SDK 28, ABI `arm64-v8a`.
Quest 3: `com.oculus.intent.category.2D` so it runs as a 2D panel.

How the app behaves: [../docs/APP.md](../docs/APP.md).  
LAN API: [../docs/API.md](../docs/API.md).

## Build

Needs Android SDK + NDK + cmake + build-tools, and `javac` 17+.
`$ANDROID_HOME` or `~/Android/Sdk` is enough for `build.sh`.

```bash
./android/build.sh
adb install -r android/exg-c.apk
```

Or `make android`. Output is `android/exg-c.apk` (debug-signed).

## First run

**Phone:** Type-C in USB **host / OTG**. Gadget / MTP will not see the board. The layout pads inside the status and navigation bars. Buttons are one compact row (scroll if the panel is narrow) so the traces keep the square screen. Plot text follows the phone density.

**Quest 3:** the Knight is USB-host on the headset, not on a PC.

1. Plug the Knight (FTDI `0403:6001`) or CH340 / CP210x / CDC ACM.
2. Grant USB. Open **exg-c**. Tap **Connect**.
3. Warming is under 64% of the locked rate (80 sps at 125). ID / Record stay idle until then. **Flash** writes the one Knight image. DTR and RTS reset the board. Sync waits until the USB line is quiet, waits out the bootloader LED, then sends one command. A sketch that was already streaming is not a failed reset. If it never goes quiet, Upload resets once more. On the Titan that image was 22578 bytes and 177 pages. A ladder that is still sending can keep the line busy, so the first reset misses and the next Upload is the one that finishes. **Debug** shows boot text. Take the electrodes off before Upload. A finished upload does not ask again unless the board prints an older `EXG-FW`. Settings then sends `exgmode_N`. The button reads `125 + IMU`, `250 EEG`, or `500 EEG` from `EXG-MODE` and from the live rate.
4. **Calibrate**: 5 s to set the kit down, desk plate, wear, sit still.
5. Cut button: teal **DC on** is the still-plate offset. **CLEAN on** only if the window is ≥ 3 s and a noise plate exists.
6. **ID** should say `still Nx`. Blink / clench change the class. That is EXG vs baseline, not a take.
7. **Take rest**, then an action. ID names only a unique winner. **Record** poses are listed separately; they are not take chips.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano. CH340 opens at 115200 8N1 with DTR and RTS high. CP210x uses its own enable and modem bits. FTDI status bytes are removed on every USB packet.

**API** is **off** until Settings → **API on**. A persistent notification stays up while the stream is on so Quest can close the 2D panel. The service is started with `startService` from a visible activity (`dataSync`). If API is off, the service stops.

## Controls that used to lie

| Chrome | Machine |
|--------|---------|
| **DC on / DC off** | still-plate mean. Teal = on. |
| **bias ON / bias off** | RLD. Connect applies add and remove. |
| **NEG RAIL** | Bias off. Each channel's bias button becomes that channel's − site. |
| **ID on** | take ID when takes exist |
| **MATCH on** | names a unique Record pose, no cosine `%` |
| FFT red mark | effective notch only (none if AUTO and no plate) |
| Pause | freezes any channel that already has samples |

Off channels are hidden on traces, map, and the channel row.

## Profiles

Tap a name to switch band/filters. Long-press to rename or delete. Electrode map stays. Plates and takes **recook** from raw.

**Export… / Import…** use the system document picker. No storage permission.

**Settings → View** bars: amplitude 20 µV–8 mV, window 1–8 s, UI 0.8×–2.2×. The plot corner draws the µV and time those lengths are. Cube tab has a zoom bar (0.7×–2.8×) plus +/− and pinch. **board** `8-ch EXG` or `8-ch + IMU` — disconnect first.

**band:** `raw` / `line-kill` (hp 2, CAR, ±1000) / `EEG` (hp 2, lp 40, ±200) / `EMG` (hp 20, envelope, CAR, ±2000).

## Storage (`getFilesDir()`)

- `exg-c.ini` — last session
- `exg-c.cal` — NOISE + CALM plates
- `exg-c.learn` — Record poses
- `exg-c/profiles/<name>.ini`
- `exg-c/atoms/<name>.npat` — takes
- `exg-c/raw/` — raw plates / takes for recook
- `live-snap.txt` — last EXG snapshot (debug)
- **CSV** — system save dialog. Writes where you pick (Downloads, Drive, …). Stop CSV closes the file.

The package is not debuggable. `run-as` cannot read these files.

## USB IDs

`res/xml/usb_device_filter.xml`: FTDI `0403:*` (Knight `0403:6001`), CH340 `1a86:*`, CP210x `10c4:*`, CDC ACM class 2.

## Layout

| Path | Role |
|------|------|
| `CMakeLists.txt` | NDK: `np_core.c` + cook + `np_serial_android.c` + JNI. No `np_ui.c`. |
| `src/com/abysscore/exgc/ExgActivity.java` | 2D UI |
| `TraceView.java` / `FftView.java` / `CubeView.java` | plot / FFT / cube |
| `StreamService.java` | FGS while API is on |
| `ExgNative.java` / `../src/np_android_jni.c` | JNI → `np_host.h` |
| `UsbSerial.java` | USB Host serial |
