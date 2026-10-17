# vkforward — Vulkan forwarding for non-Adreno GPUs

`M1 status: done. CI runs vulkaninfo through the ICD against lavapipe on
aarch64, and against lavapipe untouched, and the two agree on the device.
Images cross the socket too, which the tool needs before it prints anything.
What is left is M2, the rendering commands.`

Makes the Mali/Immortalis GPU usable from inside DroidDeck's proot Linux
runtime by forwarding Vulkan calls to the vendor driver that already lives in
the Android app process. See
`docs/development/mali-vulkan-forwarding.md` for the design.

```
  proot Linux runtime                    Android app process
  ┌─────────────────────────┐            ┌──────────────────────────┐
  │ game → DXVK → libvulkan │            │ vkforward server         │
  │         ↓               │  socket    │  dlopen libvulkan.so     │
  │ libvulkan_vkforward.so  ├───────────▶│  executes on Mali driver │
  │ (ICD stub)              │◀───────────┤                          │
  └─────────────────────────┘            └──────────────────────────┘
```

## Layout

```
common/   shared transport and codec: framing, buffer helpers, pNext chains,
          generated struct size table.
          vkfw_protocol.h  frame layout, command ids, limits
          vkfw_transport.c endpoint policy (env, file socket, abstract socket)
          vkfw_codec.c     buffer/cursor helpers and pNext chain marshalling
          vkfw_pnext_sizes.c  generated sType -> sizeof table, see scripts/
icd/      libvulkan_vkforward.so for the glibc runtime (M1: queries and device
          creation, no rendering yet).
server/   the app-side native half plus its JNI bridge.
scripts/  build helpers and the pNext table generator.
tests/    vkfw_loopback.c — server and ICD in one process, over a real socket.

The Kotlin half lives where it has to, in the app tree:
app/src/main/java/com/droiddeck/launcher/gpu/VkForwardServer.kt. The app build
compiles these C files through its own CMakeLists, so the server ships inside
the APK and there is no second copy of the transport to keep in step.
```

## Same-ABI assumption

Both halves are compiled against Vulkan headers of the same version, so Vulkan
POD structs travel as blobs instead of being marshalled field by field. This is
what keeps the code small; it also means a header bump must happen on both
sides at once. Struct *sizes* do not come from `sizeof` at runtime but from the
generated table in `common/vkfw_pnext_sizes.c`, so the two ends agree even when
toolchains differ; every rebuilt pNext node still gets a 128-byte zero tail so a
driver built against newer headers cannot read past a node we own.

Regenerate the table when the Vulkan headers move. It needs a 64-bit C
compiler to measure the sizes, and a Vulkan-Headers tree rather than an NDK
sysroot (bionic's headers will not compile anywhere but under the NDK):

```bash
python3 scripts/gen_pnext_table.py <Vulkan-Headers>/include/vulkan/vulkan_core.h common cc
```

The committed table stores the sizes as numbers, not as `sizeof(...)`
expressions, so it compiles under any Vulkan headers at all: an NDK or
distro header bump renames and drops structs freely, and none of that has
any say over this file. Vulkan structs are append-only, which is why the
version they were measured at does not have to match the one the ICD is
built against — measuring 1.3.275 and 1.4.309 gives identical sizes for
every struct they share.

## Wire format

```
[u32 magic 'VKFW'][u16 cmd][u16 flags][u32 payload_len][u64 seq][payload]
```

Every request gets exactly one reply whose payload starts with a `VkResult`;
commands declared in `vkfw_protocol.h` but not implemented yet reply
`VK_ERROR_FEATURE_NOT_PRESENT` rather than mislead the caller.

## Endpoint policy

The ICD tries `$VKFW_SOCKET`, then `/tmp/vkforward.sock`, then the abstract
socket. The app server binds the **abstract** socket `vkforward` by default:
abstract sockets carry no filesystem path, so nothing has to be bind-mounted
into the proot session and the app exposes no file. A rooted or adb-driven
session can use a file socket instead by passing the endpoint as `argv[1]` to
`vkfw_server`.

## Build

```bash
# glibc side (the runtime): ICD, standalone server, loopback test
tools/vkforward/scripts/build-linux.sh

# bionic side (the tablet, for adb verification before the app integration)
ANDROID_HOME=/path/to/sdk tools/vkforward/scripts/build-android.sh
```

### Verify on the tablet (no app involved yet)

```bash
adb shell 'mkdir -p /data/local/tmp/vkfw'
adb push tools/vkforward/build-android/vkfw_loopback /data/local/tmp/vkfw/
adb shell 'chmod +x /data/local/tmp/vkfw/vkfw_loopback'
adb shell 'cd /data/local/tmp/vkfw && VKFW_DRIVER=/vendor/lib64/hw/vulkan.mali.so ./vkfw_loopback /data/local/tmp/vkfw/sock'
```

Pass an absolute path as the endpoint: Android shells have no `$TMPDIR`. The
server side then `dlopen`s the vendor HAL, which is exactly what the app-side
server will do.

### In the app

`App.onCreate` calls `VkForwardServer.startOnMali()`, which gates on the same
Mali detection the A-line work landed, so the listener only exists on hardware
route B targets. Once up, the runtime's ICD reaches it on the abstract socket
`vkforward` without anything being mounted.

### In the runtime

`vkforward-linux-aarch64.zip` is what the tablet gets. It holds
`libvulkan_vkforward.so` and a `meta.json`, which is exactly the shape the
app's "Linux Vulkan driver" import takes: it installs both, writes the ICD
manifest itself and points the session at it. Nothing has to be rebuilt in
the runtime and nothing has to be copied there by hand.

Then, in DroidDeck, pick it as the Linux driver. The ICD on one side and the
server on the other find each other over the abstract socket `vkforward`,
which needs no mount, so a session that starts will use it.

For a hand-rolled check the standalone `vkfw_server` binary and the ICD
manifest work too:

```bash
# on the tablet, one shell for the server
adb shell 'cd /data/local/tmp && VKFW_DRIVER=/vendor/lib64/hw/vulkan.mali.so ./vkfw_server'
# and another for the client, inside the runtime
VK_ICD_FILENAMES=/path/to/vkforward_icd.aarch64.json vulkaninfo --summary
```

Use an absolute path for the socket: Android shells have no `$TMPDIR`.
`VKFW_TRACE=1` prints every exchange plus every entry point the ICD cannot
serve, which on a tablet is the only debugger there is. It is verbose: the
loader asks for several hundred names up front.

## M1 exit criterion

`vulkaninfo` inside the runtime lists the Mali device with properties, limits
and features that came from the real driver. `vkfw_loopback` is the same check
without needing the runtime, and CI runs it plus `vulkaninfo` against lavapipe
on every change.

## Known gaps (to close in M2/M3)

- **A resource can now be made and backed by memory.** Buffers, images,
  device memory, binding either of them to it, fences and binary and timeline
  semaphores all cross the socket. Buffer create info forwards its pNext chain,
  because `VkExternalMemoryBufferCreateInfo` lives there and it is the only way
  a caller says the allocation behind a buffer has to be exportable.
  Not forwarded yet: image views, framebuffers, render passes, pipelines,
  descriptors, command buffers and queue submission.
- A command the driver does not have — one from a Vulkan newer than the device
  was built for, say — is reported as `VK_ERROR_FEATURE_NOT_PRESENT`, not as a
  failure. "This device cannot" and "the layer is broken" are different
  answers and only one of them is the caller's problem.
- Core commands that are not implemented answer with a stub that returns that
  error rather than with NULL. The loader builds a trampoline for every core
  command whatever the ICD behind it can do, and the trampoline dispatches
  through a table it filled from the ICD, so a NULL there is a call through
  address zero. Extension commands still answer NULL, because that NULL is
  what an app checks before using the extension.
- `vkCreateInstance` forwards app/engine info and enabled names only; the
  loader's own pNext nodes do not cross the socket.
- **Device memory is a dma-buf, not an opaque id.** A pointer the driver hands
  out in the app process means nothing in the runtime process, so
  `vkMapMemory` is the one thing that cannot be forwarded — there is no value
  to send back. Instead the server creates the backing store as a memfd,
  imports it into the driver as a dma-buf, and sends the descriptor with the
  reply. The ICD maps its own copy (`vkMapMemory`) and dups it
  (`vkGetMemoryFdKHR`), so the caller can write into the memory and a
  compositor can import the image. Drivers that will not import a dma-buf get
  a plain allocation plus a warning: that memory cannot be mapped or exported,
  and the caller learns that from `vkMapMemory`, not from `vkAllocateMemory`.
- **Window system integration is the ICD's own, not the driver's.** A Wayland
  compositor asks for `VK_KHR_surface`, `VK_KHR_swapchain`,
  `VK_KHR_wayland_surface` and `VK_EXT_surface_maintenance1`, and an Android
  driver has never provided any of them: it answers in `VK_KHR_android_surface`,
  and a `wl_display *` from another process means nothing here. So the ICD
  serves those four itself, and the server strips them before anything reaches
  the driver — otherwise the driver simply refuses to create the instance. The
  list lives once, in `vkfw_protocol.h`, and both halves use it.
  Surfaces, support queries, formats and modes are complete; **presenting is
  not**: `vkCreateSwapchainKHR` and friends answer `VK_ERROR_FEATURE_NOT_PRESENT`
  and say so on stderr, because real swapchain images need the command buffers
  and queue submission that do not exist yet.
- No Vulkan layers in this ICD.
- The loader resolves promoted `...2KHR` aliases only while their extension is
  enabled; without it the ICD sees the core spellings, which is what it serves.
