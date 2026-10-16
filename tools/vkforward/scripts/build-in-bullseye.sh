#!/usr/bin/env bash
# Runs inside a Debian bullseye container, on an aarch64 runner.
#
# The proot runtime is a bullseye rootfs, so its glibc is 2.31, and a library
# built on a newer host quietly carries that host's symbol versions with it.
# Building in the image it has to load in is the only way to be sure.
#
# Bullseye's own libvulkan-dev is Vulkan 1.2, which predates the 1.3 entry
# points the ICD serves, so the headers come from upstream. Struct sizes do
# not depend on the header version - Vulkan structs are append-only - so this
# changes nothing about what crosses the socket.
set -euo pipefail

HEADER_VERSION=1.4.309

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential ca-certificates curl zip unzip binutils

curl -fsSL -o /tmp/vulkan-headers.tar.gz \
    "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v${HEADER_VERSION}.tar.gz"
tar xf /tmp/vulkan-headers.tar.gz -C /tmp
rm -f /tmp/vulkan-headers.tar.gz

export CFLAGS="-isystem /tmp/Vulkan-Headers-${HEADER_VERSION}/include"
bash tools/vkforward/scripts/build-runtime.sh
