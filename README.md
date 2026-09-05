# exg-c

A C host for a **[Knight](https://www.neuropawn.tech/)** ADS1299 board — or any USB-serial dongle that speaks the same 8-channel stream (FTDI, CH340, CP210x, CDC ACM).

Not a medical device. Not affiliated with NeuroPawn.

Shipped app: **2.58** (`com.abysscore.exgc`, versionCode 67). One framework, two skins:

| Piece | Role |
|-------|------|
| `src/np_core.c` + `include/np_host.h` | USB, cook, plates, ID, takes, API |
| `src/np_ui.c` | Linux SDL window. `main` only calls `np_host_start`. |
| Android Java + JNI | Same host. CMake builds **core only** (`libexg.so`, no SDL). |
| `include/np_api.h` / `src/np_api.c` | Optional LAN/HTTP API. **Off** until you turn it on. |

How the app actually behaves: [docs/APP.md](docs/APP.md).  
Wire format and endpoints: [docs/API.md](docs/API.md).  
Quest / phone notes: [android/README.md](android/README.md).

![Android host on a Knight FTDI board (`usb:0403:6001`)](docs/android.png)

## What it does

- 8 channels at **125 SPS** (Knight ADS1299).
- Default view is **line-kill EXG**: notch AUTO, hp 2 Hz, CAR on, detrend on, envelope off, ±1000 µV, 2 s window.
- **Calibrate** — 5 s to put the headset down, 8 s desk plate, tap when worn, 8 s still plate.
- **DC on / DC off** — subtracts the still-plate mean. That is not Wiener CLEAN.
- **CLEAN on** — Wiener vs the desk noise plate. Only if a noise plate exists **and** the window is ≥ 3 s (256 samples). Default 2 s cannot run it.
- **ID** — event label + ratio (`still 1.1x`, `clench 5.9x`). Not a percent. Needs a worn still plate.
- **Take** — named stretch. ID names a take only if one unique winner (≥70% and 8 pt gap) on the **last 1 s vs that take’s pattern**.
- **Record** — 1 s named pose. MATCH **names** a unique pose. It does **not** print a cosine percent.
- **bias ON / bias off** per channel (RLD). Connect applies add **and** remove.
- **Cube viz** — crimson 8³ lattice. Mapped electrode cells track EXG µV (same color and millivolt as the traces). **float on/off**, drag to spin, +/− or pinch to zoom. **map** assigns 10-10 sites.
- **USB / LAN / Bluetooth** — three ways the board reaches the app. USB is the Knight on this device. Bluetooth pairs nearby (Allow/No) and EXG rides that link. LAN uses wifi EXG after a pair that saw wifi.
- **API server** off by default. When on: HTTP 8765, UDP 8766, TCP 8767, bind lan, 125 Hz. Live path is **EXG1** binary, not JSON. `/cfg` carries colors, map, and filters so a client matches.

Default montage: **FCz–CPz**, **CP4–FC3**, **FC4–CP3**, **C3–C4**. Eight EXG sites; four bipolar EXG traces. Map can still assign any 10-10 name.

## Linux

```bash
sudo apt install build-essential libsdl2-dev pkg-config
sudo ./install-usb-permissions.sh   # udev + dialout for Knight FTDI 0403:6001
make
./np-exg
```

Headless smoke:

```bash
./np-exg --cli --port /dev/ttyUSB0 --seconds 8
```

`make test` is the mock suite. `make test-live` reads 5 s from `/dev/ttyUSB1` (desk, not cortex).

Config: `~/.config/exg-c.ini`. Profiles: `~/.config/exg-c/profiles/`.

## Android / Quest

```bash
./android/build.sh
adb install -r android/exg-c.apk
```

Package `com.abysscore.exgc`, min SDK 28, ABI `arm64-v8a`, Quest 2D panel. Cube is the build SoT in this lab.

On a phone the Knight is USB-host on the phone. On Quest 3 the Knight is USB-host on the headset.

## First session

1. **Connect**. Wait until ~125 sps (below 80 is warming).
2. **Calibrate**. Put the kit down for 5 s, leave it for the desk plate, wear it, sit still.
3. Default cut is **DC on** (still-plate offset). Teal means the cut is on.
4. **ID** should read `still Nx`. Hard blink → `blink`. Jaw clench → `clench`. That is EXG vs a quiet baseline, not a learned take.
5. **Take rest**, then **Take** an action. ID names only a unique winner.
6. Settings → **API on** only if you want the LAN stream.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano.

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

Android / Quest: on-screen buttons. Drag the cube to spin it.

## Embedded

`nplearn/` is the no-heap matcher the hosts already link. A Knight firmware image only needs two files:

```bash
cc -c nplearn/src/nplearn.c nplearn/src/nplearn_filt.c -Inplearn/include
```

Skip `nplearn_posix.c` on the MCU. See [nplearn/README](nplearn/README).
