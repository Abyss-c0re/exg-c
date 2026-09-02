# exg-c

A host app for a **[Knight](https://www.neuropawn.tech/)** ADS1299 board — or any USB-serial dongle that speaks the same 8-channel stream (FTDI, CH340, CP210x, CDC ACM).

Plug the board in, watch eight traces, clean line noise, map sites on a cube, save a profile. One C host. Linux window or Android APK.

![Android host on a Knight FTDI board (`usb:0403:6001`)](docs/android.png)

Not a medical device. Not affiliated with NeuroPawn.

## What you get

- Live 8-channel plot at 125 SPS
- **NOISE** / **CALM** / **CLEAN** so the leftover is usable
- **Cube** — **viz** is the crimson N=8 lattice (Cube Experience / levitate). **map** assigns 10-10 sites. **algo** picks the 0/1 fold (`detect` … `proton`).
- Named profiles (export / import as files on Android)
- Settings: time window, µV scale, **UI 1 / 1.5 / 2×**, notch, high-pass, **CAR**, **low-pass**, **detrend**, **envelope**
- IMU acc / gyr / mag on 57-byte Knight boards (`8-ch + IMU`)
- Band presets: `raw` / `line-kill` / `EEG` / `EMG`
- CLIP gate — Record will not save a saturated window
- Learn: **Record** a named pose on Main (gated at 80 sps). **Poses** lists wave % vs cube Jaccard. MATCH hit is wave ≥ 55%.
- **CSV** dump, **Pause**, site names + RMS on the plot, FFT strip with a 50/60 Hz marker
- **Take** a named stretch. **ID** (MATCH) names the live stream from that list (RMS + CubalC bits). Fail-closed if two takes look the same.

Default montage: Fp1 Fp2 C3 C4 P3 P4 O1 O2.

## Linux

```bash
sudo apt install build-essential libsdl2-dev pkg-config
sudo ./install-usb-permissions.sh   # udev + dialout for Knight FTDI 0403:6001
make
./np-exg
```

Headless smoke test:

```bash
./np-exg --cli --port /dev/ttyUSB0 --seconds 8
```

`make test` is the mock suite. `make test-live` reads 5 s from `/dev/ttyUSB1` (desk, not cortex).

Config lives in `~/.config/exg-c.ini`, named profiles in `~/.config/exg-c/profiles/`.

## Android

Needs the Android SDK + NDK + cmake + build-tools, and `javac` 17+.
`$ANDROID_HOME` (or `~/Android/Sdk`) is enough for `build.sh` to find the rest.

```bash
./android/build.sh
adb install -r android/exg-c.apk
```

Same thing as `make android`. Output is `android/exg-c.apk` (debug-signed).
Package `com.abysscore.exgc`, min SDK 28, ABI `arm64-v8a`.

On the phone:

1. Type-C in **USB host / OTG** mode (gadget / MTP will not see the board).
2. Plug the Knight. Grant the USB permission dialog.
3. Open **exg-c**. It lists `usb:0403:6001` — tap **Connect**.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano; wait for frames.

Cal plates, profiles, and recordings stay in the **app files directory**. No storage permission. Use **Export…** / **Import…** in Settings to share a profile through the system document picker.

More phone notes: [android/README.md](android/README.md).

## First session

1. **Connect** (Linux also tries the first USB port on start).
2. Headset **off** the head. **NOISE**, then **OK**. That is the desk / open-input plate, not EEG.
3. Wear the headset. Sit still. **CALM**.
4. Leave **CLN** on. The plot is leftover after that plate + calm DC.
5. The **ID** line should say `still`. Hard **blink** → `blink`. Jaw **clench** → `clench`.
   That is the proof the stream is on a head, not rail. Do not use Record until ID flips.
6. Type `blink` or `clench` → **Record** → do that gesture. It snaps 1 s at the burst.
7. **Cube** → **viz** for the live 8³ sample, **map** to assign 10-10 sites.
8. **Settings** — type a name (`motor`) and **Save**. On Android, **Export…** writes that profile as a file you can keep.

## Compatible boards

| USB ID | Typical hardware |
|--------|------------------|
| `0403:6001` | Knight FTDI FT232R |
| `0403:*` | other FTDI UART |
| `1a86:*` | CH340 |
| `10c4:*` | CP210x |
| class 2 | CDC ACM (`/dev/ttyACM*`) |

The firmware has to emit Knight frames. A bare UART with no ADS1299 stream will connect and sit empty.

## Keys (Linux)

`c` connect · `d` disconnect · `r` record CSV · space pause · `1`–`8` channel ·
`m` Main · `s` Settings · `b` Cube · `v` viz/map · arrows step 10-10 ·
`+` `-` zoom · Enter assign · `q` quit

On Android those are on-screen buttons. Drag the cube to spin it.

## Embedded

`nplearn/` is the no-heap matcher the hosts already link. A Knight firmware image only needs two files:

```bash
cc -c nplearn/src/nplearn.c nplearn/src/nplearn_filt.c -Inplearn/include
```

Skip `nplearn_posix.c` on the MCU. See [nplearn/README](nplearn/README).
