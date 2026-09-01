#!/usr/bin/env bash
# Build the Android APK (default: arm64-v8a). Needs Android SDK, NDK, cmake, javac.
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

SDL_VER="${SDL_VER:-2.32.8}"
SDL_DIR="$ROOT/third_party/SDL2"
if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
  mkdir -p "$ROOT/third_party"
  TGZ="$ROOT/third_party/SDL2-${SDL_VER}.tar.gz"
  if [ ! -f "$TGZ" ]; then
    echo "fetch SDL2 ${SDL_VER}"
    curl -fL --retry 3 -o "$TGZ" \
      "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz"
  fi
  rm -rf "$SDL_DIR" "$ROOT/third_party/SDL2-${SDL_VER}"
  tar -xzf "$TGZ" -C "$ROOT/third_party"
  mv "$ROOT/third_party/SDL2-${SDL_VER}" "$SDL_DIR"
fi

ICON_SRC="$REPO/exg-c.png"
mkdir -p "$ROOT/res/drawable"
if [ -f "$ICON_SRC" ]; then
  cp -f "$ICON_SRC" "$ROOT/res/drawable/ic_launcher.png"
fi

echo "== native arm64-v8a =="
BUILD_N="$ROOT/build/native"
rm -rf "$BUILD_N"
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
find "$BUILD_N" -name 'libSDL2.so' -o -name 'libmain.so' -o -name 'libc++_shared.so' \
  | while read -r so; do cp -f "$so" "$LIBDIR/"; done
[ -f "$LIBDIR/libmain.so" ] && [ -f "$LIBDIR/libSDL2.so" ] \
  || { echo "native libs missing in $LIBDIR"; ls -la "$LIBDIR"; exit 1; }

echo "== java =="
BUILD="$ROOT/build/apk"
rm -rf "$BUILD"
mkdir -p "$BUILD"/{gen,obj,java}
SDL_JAVA="$SDL_DIR/android-project/app/src/main/java"
[ -d "$SDL_JAVA/org/libsdl/app" ] || { echo "SDL java missing at $SDL_JAVA"; exit 1; }
cp -a "$SDL_JAVA/." "$BUILD/java/"
cp -a "$ROOT/src/." "$BUILD/java/"

"$BT/aapt" package -f -m -J "$BUILD/gen" -M "$ROOT/AndroidManifest.xml" -S "$ROOT/res" \
  -I "$PLATFORM/android.jar" -F "$BUILD/resources.ap_"
"$JAVAC" --release 17 -encoding UTF-8 \
  -cp "$PLATFORM/android.jar" \
  -d "$BUILD/obj" \
  $(find "$BUILD/gen" "$BUILD/java" -name '*.java')
(cd "$BUILD/obj" && "$JAR" cf "$BUILD/classes.jar" .)
"$BT/d8" --min-api 28 --output "$BUILD" "$BUILD/classes.jar"
cp "$BUILD/resources.ap_" "$BUILD/unsigned.apk"
(cd "$BUILD" && "$BT/aapt" add unsigned.apk classes.dex)
mkdir -p "$BUILD/lib/arm64-v8a"
cp -f "$LIBDIR/"*.so "$BUILD/lib/arm64-v8a/"
(cd "$BUILD" && "$BT/aapt" add unsigned.apk \
  lib/arm64-v8a/libSDL2.so \
  lib/arm64-v8a/libmain.so \
  $( [ -f lib/arm64-v8a/libc++_shared.so ] && echo lib/arm64-v8a/libc++_shared.so ))
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
