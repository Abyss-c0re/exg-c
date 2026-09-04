# Android / Quest

Same C host as `./np-exg`. Serial is USB Host. UI is Java. Native library is `libexg.so` from `src/np_core.c` — not the desktop SDL file.

App: **2.40**, package `com.abysscore.exgc`, min SDK 28, ABI `arm64-v8a`.
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

**Phone:** Type-C in USB **host / OTG**. Gadget / MTP will not see the board.

**Quest 3:** the Knight is USB-host on the headset, not on a PC.

1. Plug the Knight (FTDI `0403:6001`) or CH340 / CP210x / CDC ACM.
2. Grant USB. Open **exg-c**. Tap **Connect**.
3. Wait for ~125 sps. Below 80 is warming — ID / Record stay idle.
4. **Calibrate**: 5 s to set the kit down, desk plate, wear, sit still.
5. Cut button: teal **DC on** is the still-plate offset. **CLEAN on** only if the window is ≥ 3 s and a noise plate exists.
6. **ID** should say `still Nx`. Blink / clench change the class. That is leftover vs baseline, not a take.
7. **Take rest**, then an action. ID names only a unique winner. **Record** poses are listed separately; they are not take chips.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano.

**API** is **off** until Settings → **API on**. A persistent notification stays up while the stream is on so Quest can close the 2D panel. The service is started with `startService` from a visible activity (`dataSync`). If API is off, the service stops.

## Controls that used to lie

| Chrome | Machine |
|--------|---------|
| **DC on / DC off** | still-plate mean. Teal = on. |
| **bias ON / bias off** | RLD. Connect applies add and remove. |
| **ID on** | take ID when takes exist |
| **MATCH on** | names a unique Record pose, no cosine `%` |
| FFT red mark | effective notch only (none if AUTO and no plate) |
| Pause | freezes any channel that already has samples |

Off channels are hidden on traces, map, and the channel row.

## Profiles

Tap a name to switch band/filters. Long-press to rename or delete. Electrode map stays. Plates and takes **recook** from raw.

**Export… / Import…** use the system document picker. No storage permission.

**win** 1 / 2 / 4 / 8 s. **UI** 1.0 / 1.5 / 2.0× (including the cube). Cube **float on/off**, drag to spin, +/− or pinch to zoom. **board** `8-ch EXG` or `8-ch + IMU` — disconnect first.

**band:** `raw` / `line-kill` (hp 2, CAR, ±1000) / `EEG` (hp 2, lp 40, ±200) / `EMG` (hp 20, envelope, CAR, ±2000).

## Storage (`getFilesDir()`)

- `exg-c.ini` — last session
- `exg-c.cal` — NOISE + CALM plates
- `exg-c.learn` — Record poses
- `exg-c/profiles/<name>.ini`
- `exg-c/atoms/<name>.npat` — takes
- `exg-c/raw/` — raw plates / takes for recook
- `live-snap.txt` — last leftover snapshot (debug)
- CSV from the **CSV** button

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
