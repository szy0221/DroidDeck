#!/usr/bin/env bash
# Build the forwarding layer for the glibc runtime (the proot side).
#
# Produces:
#   libvulkan_vkforward.so   the ICD that goes into the Linux runtime
#   vkforward_icd.<arch>.json  its loader manifest (paths baked at build time)
#   vkfw_server              standalone server process (also useful on the
#                            tablet: adb shell runs it and the ICD connects)
#   vkfw_loopback            in-process end-to-end M1 check
set -euo pipefail

cd "$(dirname "$0")/../../.."
ROOT="$PWD"
VK="${ROOT}/tools/vkforward"
OUT="${VK}/build-linux"
CC_BIN="${CC:-cc}"

ARCH="$(uname -m)"
rm -rf "$OUT"
mkdir -p "$OUT"

SRC_COMMON="$VK/common"
SRC_SERVER="$VK/server"
SRC_ICD="$VK/icd"

COMMON_SRCS=("$SRC_COMMON/vkfw_codec.c" "$SRC_COMMON/vkfw_transport.c" "$SRC_COMMON/vkfw_pnext_sizes.c")
WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"

echo "== ICD (glibc $ARCH) =="
"$CC_BIN" -O2 -fPIC -shared $WARN \
    -I"$SRC_COMMON" -I"$SRC_ICD" \
    -o "$OUT/libvulkan_vkforward.so" \
    "$SRC_ICD/vk_forward.c" "${COMMON_SRCS[@]}" \
    -lpthread

cat > "$OUT/vkforward_icd.$ARCH.json" <<EOF
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "$OUT/libvulkan_vkforward.so",
    "api_version": "1.1",
    "is_portability_driver": false
  }
}
EOF

echo "== server core + standalone server =="
"$CC_BIN" -O2 $WARN \
    -I"$SRC_COMMON" -I"$SRC_SERVER" \
    -o "$OUT/vkfw_server" \
    "$SRC_SERVER/vkfw_server_core.c" "$SRC_SERVER/vkfw_server_main.c" "${COMMON_SRCS[@]}" \
    -ldl -lpthread

echo "== loopback test =="
"$CC_BIN" -O2 $WARN \
    -I"$SRC_COMMON" -I"$SRC_SERVER" -I"$SRC_ICD" \
    -o "$OUT/vkfw_loopback" \
    "$VK/tests/vkfw_loopback.c" "$SRC_ICD/vk_forward.c" "$SRC_SERVER/vkfw_server_core.c" "${COMMON_SRCS[@]}" \
    -ldl -lpthread

echo "artifacts in $OUT:"
ls -1 "$OUT"
