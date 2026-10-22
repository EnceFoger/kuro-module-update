#!/usr/bin/env bash
# Usage: ANDROID_NDK=/path/to/ndk ./build.sh
set -euo pipefail
: "${ANDROID_NDK:?set ANDROID_NDK to your NDK root (r25+ recommended)}"
cd "$(dirname "$0")"

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc 2>/dev/null || echo 4)"

# Magisk module layout: META-INF + module.prop + zygisk/arm64-v8a.so
rm -rf dist
mkdir -p dist/zygisk dist/META-INF/com/google/android
cp module.prop dist/
cp build/libkuro_companion.so dist/zygisk/arm64-v8a.so
"$ANDROID_NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip --strip-unneeded dist/zygisk/arm64-v8a.so || true

cat > dist/META-INF/com/google/android/update-binary <<'EOS'
#!/sbin/sh
umask 022
ui_print() { echo "$1"; }
require_new_magisk() {
  ui_print "*******************************"
  ui_print " Please install Magisk v20.4+! "
  ui_print "*******************************"
  exit 1
}
OUTFD=$2
ZIPFILE=$3
mount /data 2>/dev/null
[ -f /data/adb/magisk/util_functions.sh ] || require_new_magisk
. /data/adb/magisk/util_functions.sh
[ $MAGISK_VER_CODE -lt 20400 ] && require_new_magisk
install_module
exit 0
EOS
echo "#MAGISK" > dist/META-INF/com/google/android/updater-script

rm -f kuro_companion.zip
( cd dist && zip -r ../kuro_companion.zip META-INF module.prop zygisk >/dev/null )
unzip -l kuro_companion.zip
echo "Built: $(pwd)/kuro_companion.zip"
