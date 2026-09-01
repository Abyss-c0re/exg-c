#!/usr/bin/env bash
# Build the Android APK (native UI + C host). Needs Android SDK, NDK, cmake, javac.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ]; then
  NDK="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)"
fi
BT="$(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V | tail -1)"
PLATFORM="$(ls -d "$SDK"/platforms/android-* 2>/dev/null | sort -V | tail -1)"
CMAKE_BIN="$(ls -d "$SDK"/cmake/*/bin/cmake 2>/dev/null | sort -V | tail -1)"
NINJA_BIN="$(ls -d "$SDK"/cmake/*/bin/ninja 2>/dev/null | sort -V | tail -1)"
[ -n "$NDK" ] && [ -n "$BT" ] && [ -n "$PLATFORM" ] && [ -n "$CMAKE_BIN" ] \
  || { echo "need Android SDK+NDK+cmake under $SDK"; exit 1; }
export PATH="${HOME}/.local/jdk-17/bin:${HOME}/.local/jdk/bin:/usr/lib/jvm/java-17-openjdk/bin:${PATH}"
export LANG="${LANG:-C.UTF-8}" LC_ALL="${LC_ALL:-C.UTF-8}"
JAVAC="$(command -v javac)"
JAR="$(command -v jar)"
[ -n "$JAVAC" ] || { echo "javac missing"; exit 1; }

ICON_SRC="$REPO/exg-c.png"
mkdir -p "$ROOT/res/drawable"
if [ -f "$ICON_SRC" ]; then
  cp -f "$ICON_SRC" "$ROOT/res/drawable/ic_launcher.png"
fi

echo "== native arm64-v8a =="
BUILD_N="$ROOT/build/native"
mkdir -p "$BUILD_N"
"$CMAKE_BIN" -S "$ROOT" -B "$BUILD_N" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM="$NINJA_BIN"
"$CMAKE_BIN" --build "$BUILD_N" --parallel

LIBDIR="$ROOT/build/lib/arm64-v8a"
mkdir -p "$LIBDIR"
find "$BUILD_N" -name 'libexg.so' -o -name 'libc++_shared.so' \
  | while read -r so; do cp -f "$so" "$LIBDIR/"; done
[ -f "$LIBDIR/libexg.so" ] || { echo "libexg.so missing"; ls -la "$LIBDIR"; exit 1; }
# c++_shared from NDK if cmake did not copy it
if [ ! -f "$LIBDIR/libc++_shared.so" ]; then
  find "$NDK" -path '*aarch64-linux-android/libc++_shared.so' 2>/dev/null | sort -V | tail -1 \
    | while read -r s; do cp -f "$s" "$LIBDIR/"; done
fi

echo "== java =="
BUILD="$ROOT/build/apk"
rm -rf "$BUILD"
mkdir -p "$BUILD"/{gen,obj}
"$BT/aapt" package -f -m -J "$BUILD/gen" -M "$ROOT/AndroidManifest.xml" -S "$ROOT/res" \
  -I "$PLATFORM/android.jar" -F "$BUILD/resources.ap_"
"$JAVAC" --release 17 -encoding UTF-8 \
  -cp "$PLATFORM/android.jar" \
  -d "$BUILD/obj" \
  $(find "$BUILD/gen" "$ROOT/src" -name '*.java')
(cd "$BUILD/obj" && "$JAR" cf "$BUILD/classes.jar" .)
"$BT/d8" --min-api 28 --output "$BUILD" "$BUILD/classes.jar"
cp "$BUILD/resources.ap_" "$BUILD/unsigned.apk"
(cd "$BUILD" && "$BT/aapt" add unsigned.apk classes.dex)
mkdir -p "$BUILD/lib/arm64-v8a"
cp -f "$LIBDIR/"*.so "$BUILD/lib/arm64-v8a/"
(cd "$BUILD" && "$BT/aapt" add unsigned.apk lib/arm64-v8a/libexg.so)
if [ -f "$BUILD/lib/arm64-v8a/libc++_shared.so" ]; then
  (cd "$BUILD" && "$BT/aapt" add unsigned.apk lib/arm64-v8a/libc++_shared.so)
fi
"$BT/zipalign" -f -p 4 "$BUILD/unsigned.apk" "$BUILD/aligned.apk"
KS="$ROOT/debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore "$KS" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=exg-c,O=Abyss-c0re,C=EU"
fi
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out "$ROOT/exg-c.apk" "$BUILD/aligned.apk"
echo "OK $ROOT/exg-c.apk ($(stat -c%s "$ROOT/exg-c.apk") bytes)"
