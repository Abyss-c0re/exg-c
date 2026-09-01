# Android

Native Android UI on the same C host as `./np-exg`: 8-channel traces,
cube, NOISE / CALM / CLEAN, nplearn, profiles. Serial is USB Host.
Cal plates and profiles write into the **app files directory** — no
storage permission is required.

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

All of this is under `getFilesDir()` (no extra Android permission):

- `exg-c.cal` — NOISE + CALM plates
- `exg-c.ini` — last session
- `exg-c.learn` — learn templates
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
| `src/com/abysscore/exgc/ExgActivity.java` | Native Android UI |
| `src/com/abysscore/exgc/TraceView.java` | 8-channel plot |
| `src/com/abysscore/exgc/CubeView.java` | 8³ cube |
| `src/com/abysscore/exgc/ExgNative.java` | JNI to the C host |
| `src/com/abysscore/exgc/UsbSerial.java` | USB Host serial |
| `../src/np_android_jni.c` / `np_host.h` | Host API |
