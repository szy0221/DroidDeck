/*
 * vkforward server (M1 skeleton — does not render yet)
 *
 * Runs inside the Android app process (loaded via JNI from the Kotlin
 * service). Owns the real Vulkan stack: dlopen("libvulkan.so") resolves to
 * the vendor driver (Mali/Immortalis on XRING O3). Receives serialized calls
 * from the ICD stub over a Unix socket and executes them on real handles.
 *
 * Handle model: clients see 64-bit ids; this side keeps id -> real handle.
 */

#include <stdint.h>
#include <stddef.h>

#define VKFW_MAGIC 0x564B4657u /* 'VKFW' */

struct vkfw_frame {
    uint32_t magic;
    uint16_t cmd;
    uint16_t flags;
    uint64_t seq;
    /* payload follows */
};

enum {
    VKFW_CMD_ENUMERATE_PHYSICAL_DEVICES = 1,
    VKFW_CMD_GET_DEVICE_PROPS,
    VKFW_CMD_CREATE_DEVICE,
    VKFW_CMD_CREATE_BUFFER,
    VKFW_CMD_ALLOCATE_MEMORY,
    VKFW_CMD_QUEUE_SUBMIT,
    VKFW_CMD_QUEUE_WAIT_IDLE,
    /* DXVK needs fences + timeline semaphores; add before M2 ships. */
};

/* System Vulkan (vendor Mali driver), resolved at server start. */
static void *g_libvulkan;
static void *(*g_get_instance_proc_addr)(void *instance, const char *name);

static int vkfw_load_driver(void)
{
    /* TODO(M1): dlopen("libvulkan.so", RTLD_NOW|RTLD_LOCAL),
       dlsym vkGetInstanceProcAddr. The app process is a normal Android
       process, so the vendor ICD is already in the linker namespace — no
       adrenotools-style hook needed. */
    g_libvulkan = 0;
    g_get_instance_proc_addr = 0;
    return -1;
}

/* One connection = one proot session. Dispatch loop: read frame, route by
   cmd, write reply with matching seq. */
static void vkfw_serve_connection(int fd)
{
    struct vkfw_frame f;
    (void)fd; (void)f;
    /* TODO(M1): framing read/write, dispatch table, id maps for
       instance/device/queue/buffer/memory/fence/semaphore. */
}

/* JNI entry: called from VkForwardServer.kt when a session starts. Returns 0
   on success. */
int vkfw_server_main(int listen_fd)
{
    if (vkfw_load_driver() != 0) return -1;
    (void)listen_fd;
    /* TODO(M1): accept loop, one thread per connection (proot spawns many
       short-lived processes; keep accept cheap, consider SO_PASSCRED). */
    return 0;
}
