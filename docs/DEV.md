# Working on exg-c

Read this before changing the C host. What the app does on screen is [APP.md](APP.md). The LAN frame is [API.md](API.md). Android build steps are [android/README.md](../android/README.md).

The program is one engine and two drawings of it. `include/np_host.h` is the only header a UI file should call. `src/np_ui.c` is the Linux window. Android is Java, and it reaches the same functions through `src/np_android_jni.c`. A behavior change that exists only in the JNI file or only in the SDL file will drift.

## Threads

The USB reader lives in `reader_thread` (`src/np_stream.c`). It reads bytes, locks a 21-byte or 57-byte frame, stamps it, cooks it for the plot, and wakes the API. On Android that thread asks for audio priority (`nice -19`) so a busy phone still drains 500 samples a second. Do not do UI work, file dialogs, or long locks on this thread.

`np_host_tick` runs on the UI thread, once per frame. It advances calibration, starts a late channel ladder, applies API commands that were queued, and steps the cube. It must not read the serial port.

Board commands go through `cmd_push`. `cmd_thread` writes them and then waits 1.25 seconds (`NP_CMD_GAP_US`). The UI thread only queues. The gap is there because the Knight parser drops a line that arrives while it is still handling the previous one. Do not shorten it in passing.

Upload runs on `flash_thread` (`src/np_flash.c`). It needs a quiet line before the one sync. A channel ladder that is still sending looks like "the board did not reset". The recovery is a second Upload once the line is quiet, still at 115200. There is no 57600 retry.

The API thread (`src/np_api.c`) wakes on a pipe and writes sockets. It does not cook. Cook stays on the reader. If a socket is slow, drop or finish the short write there. Do not stall the reader on a client.

## Where to edit

`src/np_state.c` holds `struct np_app g` and the small shared tables (scale steps, window lengths, default colors). There is one `g`. Fields are declared in `include/np_app.h`.

| If you are changing… | Open |
| --- | --- |
| Startup, the per-frame tick, connect, the status line | `src/np_session.c` |
| The serial session, the reader, the command queue, the channel ladder, `exgmode_` | `src/np_stream.c` |
| Notch, high-pass, low-pass, CAR, the live sample, a sample that arrived over the LAN | `src/np_cook.c` |
| Which channels are on, gain, color, NEG RAIL, electrode sites, bipolar traces | `src/np_montage.c` |
| Amplitude, time window, pause | `src/np_plot.c` |
| Desk plate, still plate, CSV, the strip FFT | `src/np_cal.c` |
| The one-second Record pose and the ID line | `src/np_learn.c` |
| Named takes | `src/np_take.c` |
| `exg-c.ini`, profiles, kits | `src/np_cfg.c` |
| What the Cube tab edits (algos, made cubes, spin, zoom) | `src/np_cube.c` |
| Lighting cube cells from the live sample | `src/np_desk.c` |
| The programmer and the lines on the Flash screen | `src/np_flash.c` |
| API on/off, LAN follow, Allow/No | `src/np_share.c` |
| Where files are stored | `src/np_paths.c` |
| Frame parse, sample rate snap, optiboot pages | `src/np_knight.c`, `src/np_rate.c`, `src/np_stk500.c` |
| CubalC | `src/np_algo.c` |
| The 8³ map and SoT pack | `src/np_smx.c`, `src/np_sot.c` |
| Takes on disk, RMS, cosine | `src/np_atom.c`, `nplearn/` |

Declarations the UI calls stay in `include/np_host.h`. The body moves with the data, not into a new file, unless the file is already doing two jobs.

## Headers

Engine `.c` files start with `src/np_local.h` (system includes and the `NP_ALOG` macro) and then `include/np_mods.h`.

`include/np_host.h` is the product API. JNI and the SDL window include it.

`include/np_app.h` is the shared state and the helpers the desktop window still calls directly (`cmd_push`, `do_connect`, `cfg_save`). New UI code should prefer `np_host_*`.

The other `include/np_*.h` files are how engine files call each other. They are not a second public API. A helper used in one `.c` file stays `static` and out of a header.

A comment sits on the line above the definition. It says the units, which thread may call it, and what a bad argument or an early return does. It does not repeat the function's name.

`g.mu` covers fields the reader and the UI both touch. Do not call back into the UI, and do not take another lock that the reader takes later, while holding it. `g.qmu` is only the command queue. `g.parse_mu` is the frame parser.

## The sample

A Knight frame is `0xA0` … `0xC0`, 21 bytes of EEG or 57 with IMU, 115200 8N1. The host locks the length after it has seen a stable run, then snaps the delivered rate to 125, 200, 250, or 500. 200 means the USB link is full. It is not a firmware mode.

The plot cook is whatever the band and the filter buttons say. ID cooks on its own: high-pass, notch, CAR, detrend, no envelope, no low-pass. The EXG1 frame carries the plot cook, not the ID cook.

Counts become microvolts as `4/32767/gain*1e6/79.57`. CLIP on the plot is 4 mV. An open rail is far above that, and CAR is not allowed to hide it.

## Channel ladder

Connect does not program the ADS1299 by itself. After the board has actually rebooted, `enable_thread` sends `chon_` and `rldadd_` / `rldremove_` with the same 1.25 second gap. The reboot is an `EXG-FW` line, or a reset this process requested (DTR, or the watchdog after `exgmode_`).

Opening the serial port does not reset the chip and does not print `EXG-FW` again. That path logs `channels: leave (no reboot)` and leaves the gains alone. Queuing `exgmode_` is not the reboot. The `EXG-FW` line after the watchdog is.

## Firmware

`firmware/` is a submodule. One image. EEPROM byte 0 is read once in `setup()`:

- 0 or blank — 125 SPS, IMU frames when the sensor answers
- 1 — 250 SPS, EEG only
- 2 — 500 SPS, EEG only

A full upload does not write that byte. A fresh chip boots mode 0 until Settings sends `exgmode_N`. The sketch stores the byte, prints `EXG-SWITCH N`, and resets itself with the watchdog. Do not open the bootloader for a mode change.

`poll_mode_line` has to see `exgmode_` before the NeuroPawn parser eats the line. It remembers a partial line and drops it on a later call, 20 ms on. It must not wait inside `HardwareSerial::available`. The link wrap calls `__real_…available`, never `Serial.available()`, and LTO stays off or the wrap disappears.

Build the hex from `firmware/` with PlatformIO before `android/build.sh`. The app build copies `firmware/.pio/build/knight/firmware.hex` into the APK. It does not compile the sketch. The hex is not stored in git.

500 SPS of 21-byte frames already uses about 91% of 115200. 500 SPS with 57-byte IMU frames does not fit. Do not raise the baud, shrink the frame, or turn on RTS/CTS in a drive-by change. RTS is part of the reset pulse.

## Build and test

Desktop:

```bash
make
make test
./np-exg
```

`make test` links the parsers, the cook helpers, and the mock API. It does not compile `np_session.c` and the other engine files. After a change in those files, `make np-exg` is the check that they still link. `make test-live` reads a real serial port and is not part of the mock suite.

Android:

```bash
./android/build.sh
adb install -r android/exg-c.apk
```

Needs the SDK, the NDK, cmake, and `javac` 17. Take the electrodes off before anyone taps Upload.

Desktop config is `~/.config/exg-c.ini`. Profiles are `~/.config/exg-c/profiles/`. On Android the same names live under the app files directory. See the Android README for the list.

## Names that do not mean what they sound like

**DC on** subtracts the still-plate mean. It is not a high-pass. **CLEAN** is a Wiener filter against the desk plate, and it stays idle until that plate exists and the window is at least 3 seconds.

**MATCH** names a Record pose (one second, `exg-c.learn`). **Take ID** names a take (`atoms/<name>.npat`). They are different files. Neither one prints a cosine as a percent.

**NEG RAIL** is eight private pairs. Bias is off, each channel has its own minus site, and the sample is already V(+) − V(−). Do not run CAR or the software pair view on top of that.

**200 SPS** on the status line is a full USB link. The firmware modes are 125, 250, and 500.

**set_gen** in the ini is a migration counter. Raising it forces one correction on old files (raw view, API off, default sites). It is not a user setting.
