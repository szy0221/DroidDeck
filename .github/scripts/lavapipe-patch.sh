#!/usr/bin/env bash
# DroidDeck: make lavapipe work on a Mali tablet that has no DRM render node.
#
# Steam's gamescope is a Vulkan compositor and there is no hardware Vulkan for
# Mali inside the Linux runtime, so lavapipe (Mesa's software rasteriser) is
# what has to carry the session. It gets as far as enumerating the device and
# picking it, then dies in vulkan_create_flat_texture():
#
#     [gamescope][Error] vulkan: vkAllocateMemory failed (VkResult: -2)
#     gamescope: src/rendervulkan.cpp:3178: vulkan_create_flat_texture():
#         Assertion `bRes' failed.
#
# -2 is VK_ERROR_OUT_OF_DEVICE_MEMORY, and it is not really out of memory: 3.7 MB
# on an 11 GB device. The path is
#
#     gamescope flippable image
#       -> VkExportMemoryAllocateInfo(DMA_BUF)
#       -> lvp_AllocateMemory's export_handle_types branch
#       -> os_malloc_aligned_fd() -> os_create_anonymous_file() -> fcntl(F_ADD_SEALS)
#
# and every failure anywhere along it lands on the same `goto fail`, which
# returns VK_ERROR_OUT_OF_DEVICE_MEMORY. Two things are worth changing:
#
#   1. sealing the memfd is hardening, not correctness. Under a sandbox it can
#      legitimately fail; refusing the allocation because of it is what turns a
#      cosmetic failure into a dead session.
#   2. if the dma-buf path fails anyway (no DRM node), plain memory is fine for
#      every consumer that only renders into or samples from the image, which is
#      exactly what gamescope's placeholder textures do.
#
# Both paths print to stderr, which gamescope records in session.log, so the
# next exported log says precisely which step failed.

set -euo pipefail

cd "${1:-mesa}"

python3 - <<'PYEOF'
def ensure_includes(path, anchor, extra):
    """Add headers only if they are not already reachable."""
    with open(path) as fh:
        s = fh.read()
    missing = [inc for inc in extra if inc not in s]
    if not missing:
        return s
    if anchor not in s:
        raise SystemExit(f"cannot anchor includes in {path}")
    return s.replace(anchor, "".join(inc + "\n" for inc in missing) + anchor, 1)

def patch(path, old, new, label):
    with open(path) as fh:
        src = fh.read()
    if old not in src:
        raise SystemExit(f"patch target not found: {label} in {path}")
    with open(path, 'w') as fh:
        fh.write(src.replace(old, new, 1))
    print(f"patched {label}: {path}")

# ---------------------------------------------------------------- 1. sealing --
p = 'src/util/os_memory_fd.c'
s = ensure_includes(p, '#include <string.h>', ['#include <errno.h>', '#include <stdio.h>'])
with open(p, 'w') as fh:
    fh.write(s)

old = """#if defined(HAVE_MEMFD_CREATE) || DETECT_OS_ANDROID
   // Seal fd, so no one can grow or shrink the memory.
   if (fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
      goto fail;
#endif"""
new = """#if defined(HAVE_MEMFD_CREATE) || DETECT_OS_ANDROID
   // DroidDeck: sealing is hardening, not correctness. Inside a sandbox (proot
   // and friends) F_ADD_SEALS can legitimately fail; failing the allocation over
   // it turns a cosmetic problem into VK_ERROR_OUT_OF_DEVICE_MEMORY for callers
   // that have no idea why their memory vanished.
   if (fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
      fprintf(stderr, "[mesa-fd] F_ADD_SEALS failed (errno=%d), continuing unsealed\\n", errno);
#endif"""
patch(p, old, new, 'sealing is not fatal')

# ------------------------------------------------------- 2. lavapipe fallback --
p = 'src/gallium/frontends/lavapipe/lvp_device.c'
s = ensure_includes(p, '#include "lvp_private.h"', ['#include <stdio.h>'])
with open(p, 'w') as fh:
    fh.write(s)

old = """      mem->pmem = device->pscreen->allocate_memory_fd(device->pscreen, pAllocateInfo->allocationSize, &mem->backed_fd, dmabuf);
      if (!mem->pmem || mem->backed_fd < 0) {
          goto fail;
      }

      mem->map = device->pscreen->map_memory(device->pscreen, mem->pmem);
      mem->memory_type = memory_type;"""
new = """      mem->pmem = device->pscreen->allocate_memory_fd(device->pscreen, pAllocateInfo->allocationSize, &mem->backed_fd, dmabuf);
      bool plain_fallback = false;
      if (!mem->pmem || mem->backed_fd < 0) {
          /* DroidDeck: a device with no DRM render node cannot produce a
           * dma-buf. Most callers only render into or sample from the image -
           * gamescope's placeholder textures, for one - so plain memory keeps
           * them working instead of failing the whole allocation. */
          fprintf(stderr, "[lvp] export alloc of %llu bytes failed (dmabuf=%d fd=%d), retrying as plain memory\\n",
                  (unsigned long long) pAllocateInfo->allocationSize, (int) dmabuf, (int) mem->backed_fd);
          mem->pmem = device->pscreen->allocate_memory(device->pscreen, pAllocateInfo->allocationSize);
          if (!mem->pmem)
              goto fail;
          mem->backed_fd = -1;
          plain_fallback = true;
          fprintf(stderr, "[lvp] plain memory allocation succeeded\\n");
      }

      mem->map = device->pscreen->map_memory(device->pscreen, mem->pmem);
      mem->memory_type = plain_fallback ? LVP_DEVICE_MEMORY_TYPE_DEFAULT : memory_type;"""
patch(p, old, new, 'export falls back to plain memory')
PYEOF

grep -n "plain_fallback" src/gallium/frontends/lavapipe/lvp_device.c | head -5
grep -n "continuing unsealed" src/util/os_memory_fd.c | head -2
echo "--- lavapipe patch applied ---"
