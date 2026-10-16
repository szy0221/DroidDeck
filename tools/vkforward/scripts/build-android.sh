#!/usr/bin/env bash
# Cross-build the forwarding layer with the Android NDK.
#
# Everything is compiled for Android here so it can be pushed to the tablet and
# driven over adb before the app integration exists. The same sources also build
# with a plain glibc toolchain (see build-linux.sh) for the proot runtime.
set -euo pipefail

cd "$(dirname "$0")/../../.."
ROOT="$PWD"
NDK="${ANDROID_NDK_HOME:-${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}/ndk/27.3.13750724}"
if [[ ! -d "$NDK" ]]; then
    echo "NDK not found at $NDK; set ANDROID_NDK_HOME" >&2
    exit 2
fi
case "$(uname -s)" in
     MINGW*|MSYS*|CYGWIN*) HOST_TAG="windows-x86_64" ;;
     Darwin*)              HOST_TAG="darwin-x86_64" ;;
     *)                    HOST_TAG="linux-x86_64" ;;
 esac
CLANG="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/aarch64-linux-android31-clang"
OUT="$ROOT/tools/vkforward/build-android"

rm -rf "$OUT"
mkdir -p "$OUT"

COMMON="$ROOT/tools/vkforward/common"
SERVER="$ROOT/tools/vkforward/server"
ICD="$ROOT/tools/vkforward/icd"

# Vulkan structs are always initialised from their sType, so the "missing
# initializer" noise is not actionable here.
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"

echo "== building the Android server library =="
"$CLANG" -O2 -fPIC -shared $WARN \
    -I"$COMMON" -I"$SERVER" \
    -o "$OUT/libvkforward_server.so" \
    "$SERVER/vkfw_server_core.c" "$SERVER/vk_forward_server.c" \
    "$COMMON/vkfw_codec.c" "$COMMON/vkfw_transport.c" "$COMMON/vkfw_pnext_sizes.c" \
    -ldl -llog

echo "== building the loopback test binary =="
"$CLANG" -O2 $WARN \
    -I"$COMMON" -I"$SERVER" -I"$ICD" \
    -o "$OUT/vkfw_loopback" \
    "$ROOT/tools/vkforward/tests/vkfw_loopback.c" \
    "$ICD/vk_forward.c" \
    "$SERVER/vkfw_server_core.c" \
    "$COMMON/vkfw_codec.c" "$COMMON/vkfw_transport.c" "$COMMON/vkfw_pnext_sizes.c" \
    -ldl -llog

echo "artifacts in $OUT:"
ls -1 "$OUT"
