# vkforward — Vulkan forwarding for non-Adreno GPUs

Skeleton for M1 of `docs/development/mali-vulkan-forwarding.md`. Nothing here
renders yet; the two halves define the wire protocol and the build seams.

```
icd/     libvulkan_vkforward.so — glibc aarch64 ICD for the proot runtime.
         Build with the runtime toolchain (see tools/linuxfs), then zip it as
         a Linux Vulkan driver and import it in DroidDeck's driver UI.
server/  App-side half. VkForwardServer.kt binds a Unix socket per session;
         vk_forward_server.c loads the system libvulkan.so (vendor Mali
         driver) and executes forwarded calls on real handles.
```

M1 exit criteria: `vulkaninfo` inside the runtime lists the Mali device with
real properties, limits and features.
