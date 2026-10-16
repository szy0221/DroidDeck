#!/usr/bin/env bash
# Build the ICD for the Linux runtime and package it for the app to import.
#
# The app's "Linux driver" import takes a zip holding a libvulkan_*.so and a
# meta.json, installs both under its own files, writes the ICD manifest itself
# and points the session at it. That is the whole delivery path: no runtime
# image has to be rebuilt, and the ICD lands somewhere the proot session can
# already read.
#
# This runs inside tools/vkforward/scripts/build-in-bullseye.sh, because the
# runtime is a Debian bullseye rootfs and a library built against a newer glibc
# will not load in it.
#
# Produces:
#   vkforward-linux-aarch64.zip   what the app imports
#   vkforward_icd.<arch>.json     manifest for a hand-rolled check
set -euo pipefail

cd "$(dirname "$0")/../../.."
ROOT="$PWD"
VK="${ROOT}/tools/vkforward"
OUT="${VK}/build-runtime"
CC_BIN="${CC:-cc}"

ARCH="$(uname -m)"
# The runtime image is Debian bullseye; see tools/linuxfs/debian-image.env.
RUNTIME_GLIBC_MINOR=31
HEADER_VERSION=1.4.309

rm -rf "$OUT"
mkdir -p "$OUT/pkg"

WARN="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"
COMMON_SRCS=("$VK/common/vkfw_codec.c" "$VK/common/vkfw_transport.c" "$VK/common/vkfw_pnext_sizes.c")

echo "== ICD (glibc $ARCH) =="
# Symbols on: this is a debugging layer, and a stack with names in it is worth
# more inside a session log than the kilobytes that stripping would save.
"$CC_BIN" -O2 -g -fPIC -shared $WARN \
    -I"$VK/common" -I"$VK/icd" ${CFLAGS:-} \
    -o "$OUT/pkg/libvulkan_vkforward.so" \
    "$VK/icd/vk_forward.c" "${COMMON_SRCS[@]}" \
    -lpthread

# A library that needs a newer glibc than the runtime has is one that fails to
# load there, with an error that names a symbol nobody asked about. Refuse it
# here, where the message can say why.
newest=$(objdump -T "$OUT/pkg/libvulkan_vkforward.so" | grep -o 'GLIBC_2\.[0-9]*' |
    sort -u -t. -k2,2n | tail -1)
if [ -n "$newest" ] && [ "${newest#GLIBC_2.}" -gt "$RUNTIME_GLIBC_MINOR" ]; then
    echo "the ICD needs $newest but the runtime is glibc 2.$RUNTIME_GLIBC_MINOR" >&2
    echo "build it in the container this script ships with, not on this host" >&2
    exit 1
fi
echo "== ICD glibc requirement: ${newest:-none} (runtime allows 2.$RUNTIME_GLIBC_MINOR) =="

cat > "$OUT/vkforward_icd.$ARCH.json" <<EOF
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "$OUT/pkg/libvulkan_vkforward.so",
    "api_version": "1.1",
    "is_portability_driver": false
  }
}
EOF

cat > "$OUT/pkg/meta.json" <<EOF
{
  "schemaVersion": 1,
  "kind": "linux-vulkan-icd",
  "name": "vkforward",
  "driverVersion": "m1",
  "libc": "glibc",
  "minGlibc": "2.$RUNTIME_GLIBC_MINOR",
  "sourceLibraryName": "libvulkan_vkforward.so",
  "vulkanHeaderVersion": "$HEADER_VERSION",
  "note": "Forwards Vulkan to the Mali driver in the app process. Needs the matching vkforward server running in DroidDeck."
}
EOF

echo "== packaging =="
(cd "$OUT/pkg" && zip -9 -q -r "../vkforward-linux-$ARCH.zip" .)
unzip -l "$OUT/vkforward-linux-$ARCH.zip"
ls -lh "$OUT/vkforward-linux-$ARCH.zip"
