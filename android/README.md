# Android

Same C host as `./np-exg` on Linux: 8-channel Knight EXG, CLEAN / NOISE /
CALM, nplearn, 8³ cube, profiles. The UI is the desktop layout with
larger buttons. Serial is USB Host, not `/dev/ttyUSB*`.

## What you need

- Android SDK (`$ANDROID_HOME` or `~/Android/Sdk`)
- NDK, cmake, and build-tools inside that SDK
- `javac` 17+
- `adb` to install

Package: `com.abysscore.exgc`. Min SDK 28. Default ABI `arm64-v8a`.

## Build

```
./android/build.sh
adb install -r android/exg-c.apk
```

Or from the repo root: `make android`.

The script fetches SDL2 2.32.x into `android/third_party/` on the first
run (that directory is gitignored). Output: `android/exg-c.apk`, signed
with a local debug keystore.

## Phone

1. Type-C in **USB host / OTG** mode (gadget / MTP will not see the board).
2. Plug the Knight (FTDI `0403:6001`). Grant the USB permission dialog.
3. Open **exg-c**. It lists `usb:0403:6001` and connects.
4. First session is the same as Linux: **NOISE** (desk) → **OK** → wear
   headset → **CALM** → leave **CLN** on.

Do not hammer Disconnect / Connect. Each DTR pulse resets the Nano; wait
for frames instead.

If the plot is empty but the port is listed: unplug/replug once, grant
USB again, tap **Connect** once.

## Storage

Linux `~/.config/exg-c*` becomes the app files directory:

- `exg-c.ini`
- `exg-c.learn`
- `exg-c/profiles/<name>.ini`
- CSV recordings (`knight-YYYYMMDD-HHMMSS.csv`)

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
| `src/com/abysscore/exgc/ExgActivity.java` | SDL activity |
| `src/com/abysscore/exgc/UsbSerial.java` | USB Host serial (FTDI / CDC / CH340) |
| `../src/np_serial_android.c` | JNI `np_serial_*` for the C host |
| `../src/main.c` | Shared UI (`#ifdef __ANDROID__` for touch + paths) |
