# Android

Native Android UI on the same C host as `./np-exg`. Serial is USB Host.
Cal plates and profiles write into the **app files directory** — no
storage permission is required.

## What you need

- Android SDK (`$ANDROID_HOME` or `~/Android/Sdk`)
- NDK, cmake, and build-tools inside that SDK
- `javac` 17+
- `adb` to install

Package: `com.abysscore.exgc`. Min SDK 28. Default ABI `arm64-v8a`.

## Build

From the repo root:

```bash
./android/build.sh
adb install -r android/exg-c.apk
```

Or `make android`. Output is `android/exg-c.apk`, signed with a local
debug keystore. The native library is `libexg.so` (no SDL2).

## Phone

1. Type-C in **USB host / OTG** mode (gadget / MTP will not see the board).
2. Plug the Knight (FTDI `0403:6001`) — or CH340 / CP210x / CDC ACM.
3. Grant the USB permission dialog.
4. Open **exg-c**. It lists the device. Tap **Connect**.
5. First session: **NOISE** (desk) → **OK** → wear headset → **CALM** → leave **CLN** on.
6. Wait until the strip shows ~125 sps. **ID** / **Record** / **MATCH** stay
   off while the board is still enabling (below 80 sps).
7. **ID** (CALM-relative event) should read `still`. Hard blink → `blink`.
   Jaw clench → `clench`. That is an event label, not a learned take.
   **Record** snaps 1 s. MATCH prints a percent only if one pose wins.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano; wait
for frames instead.

If the plot is empty but the port is listed: unplug/replug once, grant
USB again, tap **Connect** once.

**CSV** writes `knight-YYYYMMDD-HHMMSS.csv` into the app files directory
(debug only). **ATOM** folds each second into an 8-byte CubalC atom
(same 8×8 feature bits as `cubalc_eeg_pack_matrix`). Tap **name (tap)** and type in the dialog — the in-plot keyboard is a
black overlay on the 1440² face. **SaveA** writes
`exg-c/atoms/<name>.npat`. Tap two takes to compare. Near-identical
files say `same head — not distinct`. Hamming unity is not a score.
**Pause** freezes the plot (FROZEN). The strip under the traces is a
128-pt FFT with a marker at 50/60 Hz.

## Profiles

**Profiles** (Settings): tap a name to switch filters/band. Long-press to
rename or delete. **Save current as…** snapshots what you have now.
Electrode map does not change. NOISE / CALM / takes recook from stored
raw — you do not re-record. **Share file** / **Open file** for a copy.

**Export…** / **Import…** use the system document picker so you can put a
profile on Downloads, Drive, or a USB stick — still no storage permission.

**win** picks the plot window (1 / 2 / 4 / 8 s). **UI** is 1.0 / 1.5 / 2.0×
text. **board** is `8-ch + IMU` (57-byte frames, acc/gyr/mag on the strip)
or `8-ch EXG`. Disconnect before switching board.

**band** picks `raw` / `line-kill` (notch+CAR+hp1) / `EEG` (+lp 40) /
`EMG` (hp 20 + envelope). **CAR** subtracts the mean of non-clip
channels. **envelope** plots 150 ms RMS. **detrend** hides DC.
A take that is loud is saved with a warning, not refused.

Tap the site name (Fp1…) in Settings for an RGB color picker. Other
Settings buttons open a list — they do not cycle on each tap.

**algo** (Cube tab and Settings) picks how each headset cell becomes 0 or 1:
`detect` `sign` `mean` `energy` `delta` `fold` `proton`. Same as Linux.

**MATCH** / **ID** name a unique winner only (gap ≥ 8 points). A split
is `now —`, not two high percents. Cube Jaccard is not printed.

**Take** on Main starts a timed fold. **Stop** asks for a name. **Takes**
lists them — tap two to compare. Delete to drop one.

Names: letters, digits, `-`, `_`.

## API

Settings → **API**. Default is on, **lan**, HTTP `8765`, UDP `8766`, TCP `8767`, 125 Hz.
Type the port numbers. A persistent notification stays up while the stream is on so Quest can close the panel.

`GET /health` `/status` `/sample` `/stream` `/cfg`. Live stream is EXG1 binary, not JSON.
`POST /connect` `/disconnect` `/pause` `/cfg`.
UDP: send any packet to `:8766` to subscribe. TCP: connect `:8767` and read EXG1 frames.
Token (optional) is required for LAN, not for `127.0.0.1`. Push dest is `host:port` for a fixed UDP sink.

## Storage

All of this is under `getFilesDir()`:

- `exg-c.cal` — NOISE + CALM plates
- `exg-c.ini` — last session
- `exg-c.learn` — learn templates
- `exg-c/profiles/<name>.ini`
- CSV recordings (`knight-YYYYMMDD-HHMMSS.csv`) from the **CSV** button
- `exg-c/atoms/<name>.npat` — CubalC atom chains (8 bytes / second)

## USB IDs

`res/xml/usb_device_filter.xml` matches:

- FTDI `0403:*` (Knight FT232R `0403:6001`)
- CH340 `1a86:*`
- CP210x `10c4:*`
- CDC ACM (class 2)

## Layout

| Path | Role |
|------|------|
| `CMakeLists.txt` | NDK: same `src/*.c` + `nplearn` + `np_serial_android.c` |
| `src/com/abysscore/exgc/ExgActivity.java` | Native Android UI |
| `src/com/abysscore/exgc/TraceView.java` | 8-channel plot (site names + RMS) |
| `src/com/abysscore/exgc/FftView.java` | 128-pt strip FFT |
| `src/com/abysscore/exgc/CubeView.java` | 8³ cube (viz + map) |
| `src/com/abysscore/exgc/ExgNative.java` | JNI to the C host |
| `src/com/abysscore/exgc/UsbSerial.java` | USB Host serial |
| `../src/np_android_jni.c` / `np_host.h` | Host API |
