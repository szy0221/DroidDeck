# vkforward — Vulkan forwarding for non-Adreno GPUs

`M1 status: implemented, verified by CI against lavapipe and ready for the
device loopback (see "Verify on the tablet").`

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
```

## Same-ABI assumption

Both halves are compiled against Vulkan headers of the same version, so Vulkan
POD structs travel as blobs instead of being marshalled field by field. This is
what keeps the code small; it also means a header bump must happen on both
sides at once. Struct *sizes* do not come from `sizeof` at runtime but from the
generated table in `common/vkfw_pnext_sizes.c`, so the two ends agree even when
toolchains differ; every rebuilt pNext node still gets a 128-byte zero tail so a
driver built against newer headers cannot read past a node we own.

Regenerate the table when the Vulkan headers move:

```bash
python3 scripts/gen_pnext_table.py \
    "$ANDROID_HOME/ndk/27.3.13750724/toolchains/llvm/prebuilt/*/sysroot/usr/include/vulkan/vulkan_core.h" \
    common
```

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

## M1 exit criterion

`vulkaninfo` inside the runtime lists the Mali device with properties, limits
and features that came from the real driver. `vkfw_loopback` is the same check
without needing the runtime, and CI runs it plus `vulkaninfo` against lavapipe
on every change.

## Known gaps (to close in M2/M3)

- No rendering commands yet: memory, buffers, images, command buffers and queue
  submission reply `VK_ERROR_FEATURE_NOT_PRESENT`.
- `vkCreateInstance` forwards app/engine info and enabled names only; the
  loader's own pNext nodes do not cross the socket.
- Window system integration is not forwarded — gamescope owns presentation.
- `vkGetPhysicalDeviceQueueFamilyProperties2` / `...MemoryProperties2` are not
  exported yet; their chains would need per-family/per-heap round trips.
- No Vulkan layers in this ICD.
