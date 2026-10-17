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

# ------------------------------------------- 1b. actually getting a file fd --
# The r3 log settled where the export really dies: 11 export attempts, all with
# fd=-1, and the sealing warning never printed once. So os_create_anonymous_file()
# itself returned -1 and the allocation never reached fcntl(F_ADD_SEALS).
#
# Its two paths are memfd_create() and a mkostemp() file; the second needs a
# writable XDG_RUNTIME_DIR/TMPDIR, and the first can be refused outright by the
# proot ptrace layer. Neither is guaranteed here, so try them again explicitly
# and then fall through to POSIX shared memory and a plain temp file.
#
# Any descriptor that can be mmap'd MAP_SHARED and handed to another process is
# good enough: lavapipe and gamescope are both software, so this buffer never
# reaches a GPU and the kernel does not have to back it with a dma-buf. The only
# thing that has to work is the mmap() below and os_import_memory_fd() on the far
# side, both of which are indifferent to how the descriptor was made.
s = ensure_includes(p, '#include <string.h>',
                    ['#include <limits.h>', '#include <stdlib.h>', '#include <sys/stat.h>'])
with open(p, 'w') as fh:
    fh.write(s)

old = """   mem_fd = os_create_anonymous_file(alloc_size, fd_name);

   if(mem_fd < 0)
      return NULL;"""
new = """/* DroidDeck: these live in <sys/mman.h> only with _GNU_SOURCE and a recent
 * enough glibc; define them so the fallbacks below build anywhere. */
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
#define MFD_ALLOW_SEALING 0x0002U
#endif

   mem_fd = os_create_anonymous_file(alloc_size, fd_name);

   if (mem_fd < 0) {
      /* DroidDeck: without a usable descriptor every Vulkan external-memory
       * allocation fails, and gamescope reports it as
       * "vkAllocateMemory failed (-2)" while never presenting a frame. Walk
       * the ways of getting one, from the most to the least private, and say
       * which of them worked so the next exported log can be read directly. */
      static unsigned long fallback_seq;
      int saved_errno = errno;
      const char *how = "none";

#ifdef HAVE_MEMFD_CREATE
      if (mem_fd < 0) {
         mem_fd = memfd_create(fd_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
         if (mem_fd >= 0) how = "memfd(sealing)";
      }
      if (mem_fd < 0) {
         mem_fd = memfd_create(fd_name, MFD_CLOEXEC);
         if (mem_fd >= 0) how = "memfd";
      }
#endif
      if (mem_fd < 0) {
         char shm_name[256];
         snprintf(shm_name, sizeof(shm_name), "/mesa-lvp-%ld-%lu",
                  (long) getpid(), fallback_seq++);
         mem_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_EXCL, 0600);
         if (mem_fd >= 0) {
            shm_unlink(shm_name);
            how = "shm";
         }
      }
      if (mem_fd < 0) {
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
         const char *dirs[] = { getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), "/tmp", "/var/tmp" };
         for (unsigned i = 0; i < ARRAY_SIZE(dirs) && mem_fd < 0; i++) {
            char tmpl[PATH_MAX];
            if (!dirs[i] || !*dirs[i])
               continue;
            if (snprintf(tmpl, sizeof(tmpl), "%s/mesa-lvp-XXXXXX", dirs[i]) >= (int) sizeof(tmpl))
               continue;
            mem_fd = mkstemp(tmpl);
            if (mem_fd >= 0) {
               int flags = fcntl(mem_fd, F_GETFD);
               if (flags >= 0)
                  fcntl(mem_fd, F_SETFD, flags | FD_CLOEXEC);
               unlink(tmpl);
               how = "tmpfile";
            }
         }
      }
      if (mem_fd >= 0 && ftruncate(mem_fd, alloc_size) != 0) {
         close(mem_fd);
         mem_fd = -1;
         how = "ftruncate failed";
      }
      fprintf(stderr, "[mesa-fd] os_create_anonymous_file(%llu) failed errno=%d, "
                      "fallback %s -> fd=%d\\n",
              (unsigned long long) alloc_size, saved_errno, how, mem_fd);
   }

   if(mem_fd < 0)
      return NULL;"""
patch(p, old, new, 'fallback ways to get an exportable fd')

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
