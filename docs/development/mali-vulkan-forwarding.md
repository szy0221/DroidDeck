# Vulkan forwarding for non-Adreno GPUs (Mali / Immortalis / Xclipse)

Status: M1 done and verified in CI. Goal: make Windows games through
Proton/DXVK run with hardware acceleration on devices whose GPU is not an
Adreno (e.g. Xiaomi XRING O3 tablets with an Arm Mali/Immortalis GPU).

M1 — plumbing — is complete: `vulkaninfo --summary` through the ICD reports the
device behind the real driver (CI runs it against lavapipe on aarch64 and
compares it against the same tool on the same driver untouched). What is left
is M2, the rendering commands, and M3, gamescope.

## Measured device facts (Xiaomi XRING O3 tablet)

Collected with adb on the target device — this is what the forwarding layer
must target:

```
ro.hardware.vulkan = mali
ro.hardware        = xring_o3_asic
ro.soc.model       = O3
/vendor/lib64/hw/  → vulkan.mali.so
/dev/mali0         → crw-rw-rw- system:graphics  10, 307
/sys/class/misc/mali0 → ../../devices/platform/soc/d2000000.gpu/misc/mali0
```

Reading:

- The Vulkan HAL is `vulkan.mali.so`, i.e. Arm's Mali driver stack, confirmed
  by `ro.hardware.vulkan=mali`. On the Android side `dlopen("libvulkan.so")`
  resolves through `ro.hardware.vulkan` to that HAL, so no vendor-specific
  `dlopen` path is needed — but the server may also open
  `/vendor/lib64/hw/vulkan.mali.so` directly and `dlsym` its
  `vkGetInstanceProcAddr` as a fallback.
- `/dev/mali0` with major 10 (misc) and a `sys/class/misc` entry means
  **`mali_kbase`, the Arm proprietary kernel driver** — not DRM/panthor. There
  is no `/dev/dri/renderD*` node for the GPU, so no Mesa driver can attach.
- The node is world-readable/writable (`crw-rw-rw-`), so an ordinary app could
  open it, but the ioctl ABI is undocumented and version-locked to Arm's
  user-side library. Reimplementing it is not an option — forwarding is.
- SoC base `d2000000.gpu` is useful only for logging/identification.

## Why Turnip does not work here

The Linux runtime renders games on `libvulkan_freedreno.so` (Turnip), which
talks to the GPU through the KGSL user-space interface
(`/dev/kgsl-3d0`). KGSL is Qualcomm-only.

Mali devices ship Arm's proprietary `mali_kbase` kernel driver. Mesa's Vulkan
driver for Mali (PanVK) only supports the open-source `panfrost`/`panthor`
kernel drivers, which phone/tablet vendors do not ship. So inside the proot
runtime there is no Vulkan ICD that can drive the GPU at all.

The Android compositor is not affected: `waylandcomp` already falls back to
the system `libvulkan.so` when no AdrenoTools driver is loaded, so the system
Mali driver presents frames fine.

## Design: forward Vulkan from the Linux runtime to the Android process

```
  proot Linux runtime                    Android app process
  ┌─────────────────────────┐            ┌──────────────────────────┐
  │ game → DXVK → libvulkan │            │ forwarding server        │
  │         ↓               │  socket    │  (runs in app process)   │
  │ libvulkan_vkforward.so  ├───────────▶│  dlopen system libvulkan │
  │ (ICD stub, translates   │            │  executes on Mali driver │
  │  calls into messages)   │◀───────────┤                          │
  └─────────────────────────┘            └──────────────────────────┘
```

This is the same idea as Venus (virtio-gpu Vulkan) or virglrenderer's Vulkan
mode, but scoped to a much simpler transport: one local socket between the
proot session and the app, one protocol, one physical device.

### Components

1. **`tools/vkforward/icd/`** — a glibc/aarch64 Vulkan ICD
   (`libvulkan_vkforward.so`) built with the same toolchain as the runtime.
   It implements `vk_icdGetInstanceProcAddr`, serializes calls, and talks to
   the server over a Unix socket bind-mounted into the session. It installs
   through the existing "import Linux Vulkan driver" flow (the importer now
   accepts any `libvulkan_*.so`).

2. **`tools/vkforward/server/`** — an Android-side native server living in the
   app process. It loads the system `libvulkan.so` (Mali vendor driver),
   receives serialized calls, executes them, and sends results back. Handles
   only exist on the server side; the stub maps client handles to server
   handles by value (dispatchable handles are wrapped with a client-side
   dispatch table, same as the loader's `terminator` layer does).

### Protocol (v1)

Binary, little-endian, length-prefixed frames over a Unix socket:

```
[u32 magic 'VKFW'] [u16 cmd] [u16 flags] [u64 seq] [payload...]
```

Commands cover a deliberately small subset first:

- `ENUMERATE_PHYSICAL_DEVICES`, `GET_DEVICE_PROPS`, `CREATE_DEVICE`,
  `GET_QUEUE`, `ALLOCATE_MEMORY`, `CREATE_BUFFER`, `CREATE_IMAGE`,
  `BIND_MEMORY`, `CREATE_COMMAND_POOL/BUFFER`, `CMD_*` (record), `QUEUE_SUBMIT`,
  `QUEUE_WAIT_IDLE`, plus the sync objects needed by DXVK 2.x
  (fences, timeline semaphores).

Handles cross the wire as 64-bit ids. The server keeps a map from client id to
real Mali handle. Object creation returns a fresh id; destroy frees it.

### Hard parts and mitigations

1. **Memory.** `vkAllocateMemory` results must be shared between the two
   environments for zero-copy presentation. The compositor already receives
   frames as dmabuf via `wl_dmabuf` from gamescope, so presentation does not
   go through Vulkan at all. Only the GPU-allocated buffers that DXVK touches
   matter, and those never cross the boundary as raw memory — the server
   allocates on behalf of the client. `HOST_VISIBLE` memory is implemented as
   a server-side staging buffer plus an explicit upload/download channel
   (shared memfd + the same socket for command traffic).

2. **Synchronization.** Fences and (timeline) semaphores are executed on the
   server; `vkGetFenceStatus` / wait calls are answered by the server after
   polling the real object. No cross-process fence FDs in v1 — add
   `VK_KHR_external_fence_fd` / sync-file import-export in v2 to cut latency.

3. **Extension negotiation.** The stub must report exactly the intersection of
   what DXVK requires and what the Mali driver exposes. First milestone:
   Vulkan 1.3 core + `VK_KHR_swapchain`-less offscreen rendering (gamescope
   owns the window), `VK_EXT_image_drm_format_modifier`,
   `VK_KHR_external_memory_fd` (opaque fd), dynamic rendering, synchronization2.

4. **Performance.** Round-trips dominate. Batching: the stub accumulates
   command-buffer records and flushes on `vkQueueSubmit`; object queries are
   cached. Expect a large but unknown overhead — measure with `vkcube` and one
   DXVK game before optimizing.

## Milestones

1. **M1 — plumbing** (done): stub + server, `vulkaninfo` inside the runtime
   reports the Mali device (props/limits/features forwarded). En route it also
   needed images, because the tool creates one before it prints anything.
2. **M2 — render**: `vkcube` (offscreen) through the stub; memory and sync
   working. This is where the anonymous-shared-fd and sync-object questions in
   "Known hard parts" get answered, and it is the milestone the trace output
   tells us what to build: every unimplemented core command it names is one
   DXVK will reach for.
3. **M3 — gamescope**: gamescope creates its Vulkan device through the stub;
   a native ARM64 Linux game renders.
4. **M4 — DXVK**: Proton + DXVK game renders; measure FPS vs. software.
5. **M5 — upstream**: decide whether this belongs in DroidDeck or as a
   standalone layer.

## Non-goals

- No support for multiple GPUs or device groups.
- No window-system integration in the stub (gamescope owns presentation).
- No Vulkan layers in v1.
