#!/usr/bin/env bash
# Runs inside a Debian bullseye container, on an aarch64 runner.
#
# The proot runtime is a bullseye rootfs, so its glibc is 2.31, and a library
# built on a newer host quietly carries that host's symbol versions with it.
# Building in the image it has to load in is the only way to be sure. This is
# the same container the x86 preloads are built in, down to the archive
# sources: bullseye's packages have left the live mirror.
#
# Bullseye's own libvulkan-dev is Vulkan 1.2, which predates the 1.3 entry
# points the ICD serves, so the headers come from upstream. Struct sizes do
# not depend on the header version - Vulkan structs are append-only - so this
# changes nothing about what crosses the socket.

set -euo pipefail

HEADER_VERSION=1.4.309

printf 'deb http://archive.debian.org/debian bullseye main\ndeb http://archive.debian.org/debian-security bullseye-security main\n' \
    > /etc/apt/sources.list
apt-get -o Acquire::Check-Valid-Until=false update -qq
# libc6-dev is named even though gcc pulls it, because a build that cannot
# find stdint.h is a far more confusing failure than one that says which
# package was missing.
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc libc6-dev binutils curl ca-certificates zip unzip
ls /usr/include/stdint.h /usr/include/pthread.h

for attempt in 1 2 3 4 5 6; do
    curl -fsSL -o /tmp/vulkan-headers.tar.gz \
        "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v${HEADER_VERSION}.tar.gz" && break
    [ "$attempt" -lt 6 ] || { echo "::error::could not fetch Vulkan-Headers ${HEADER_VERSION}"; exit 1; }
    echo "pull attempt $attempt failed; retrying in $((attempt * 10)) s"
    sleep $((attempt * 10))
done
tar xf /tmp/vulkan-headers.tar.gz -C /tmp
rm -f /tmp/vulkan-headers.tar.gz

export CFLAGS="-isystem /tmp/Vulkan-Headers-${HEADER_VERSION}/include"
bash tools/vkforward/scripts/build-runtime.sh
