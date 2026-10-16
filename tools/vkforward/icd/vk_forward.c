/*
 * vkforward ICD stub (M1 skeleton — does not render yet)
 *
 * Lives inside the proot Linux runtime. Implements the Vulkan loader ICD
 * interface, serializes a small command set, and talks to the app-side
 * server over a Unix socket. Build with the runtime toolchain (glibc,
 * aarch64). Installs through DroidDeck's "import Linux Vulkan driver" flow.
 *
 * Wire format: [u32 'VKFW'][u16 cmd][u16 flags][u64 seq][payload...]
 * See docs/development/mali-vulkan-forwarding.md for the protocol.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef void *PFN_vkVoidFunction;
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef uint32_t VkResult;
typedef uint64_t VkDeviceSize;

#define VK_SUCCESS 0
#define VK_ERROR_INITIALIZATION_FAILED ((VkResult)-3)
#define VK_ERROR_EXTENSION_NOT_PRESENT ((VkResult)-7)

/* ---- transport ------------------------------------------------------- */

/* Default socket the app binds and the session script bind-mounts in. */
#define VKFW_SOCKET_PATH "/tmp/vkforward.sock"

struct vkfw_conn {
    int fd;          /* connected Unix socket, -1 when offline */
    uint64_t next_seq;
};

static struct vkfw_conn g_conn = { .fd = -1, .next_seq = 1 };

/* M1: connect lazily on first call; fail cleanly when the server is down. */
static int vkfw_ensure_connected(void)
{
    if (g_conn.fd >= 0) return 0;
    /* TODO(M1): connect(AF_UNIX, VKFW_SOCKET_PATH), retry with backoff,
       remember failure so games see VK_ERROR_INITIALIZATION_FAILED instead
       of blocking forever. */
    return -1;
}

/* Send one frame, read one reply. Returns reply payload length or < 0. */
static long vkfw_call(uint16_t cmd, const void *payload, size_t payload_len,
                      void *reply, size_t reply_cap)
{
    if (vkfw_ensure_connected() != 0) return -1;
    /* TODO(M1): serialize header + payload, write(), blocking read() of the
       reply header then payload, match by seq, handle EINTR. */
    (void)cmd; (void)payload; (void)payload_len; (void)reply; (void)reply_cap;
    return -1;
}

/* ---- dispatch table ----------------------------------------------------
 * Every dispatchable handle created by this ICD starts with this struct so
 * the loader's trampoline can reach us. The server-side handle lives in the
 * second slot and is the id that crosses the wire.
 */
struct vkfw_object {
    void *loader_dispatch;   /* VK_LOADER_DATA magic, required by the loader */
    uint64_t server_handle;
};

/* ---- entry points (M1: enumeration only) ------------------------------ */

static VkResult vkfw_EnumerateInstanceExtensionProperties(
        const char *layer, uint32_t *count, void *props)
{
    (void)layer; (void)props;
    /* M1: advertise nothing; the extension set comes from the server. */
    *count = 0;
    return VK_SUCCESS;
}

static VkResult vkfw_CreateInstance(const void *info, const void *alloc,
                                    VkInstance *out)
{
    (void)info; (void)alloc;
    if (vkfw_ensure_connected() != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    /* TODO(M1): CREATE_INSTANCE cmd; server returns api_version + handle id. */
    *out = NULL;
    return VK_ERROR_INITIALIZATION_FAILED;
}

static PFN_vkVoidFunction vkfw_GetInstanceProcAddr(VkInstance instance,
                                                   const char *name)
{
    (void)instance;
    if (!strcmp(name, "vkEnumerateInstanceExtensionProperties"))
        return (PFN_vkVoidFunction)vkfw_EnumerateInstanceExtensionProperties;
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction)vkfw_CreateInstance;
    /* TODO(M2+): the full DXVK surface — see the design doc's command list. */
    return NULL;
}

/* ---- loader-facing exports --------------------------------------------- */

__attribute__((visibility("default")))
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name)
{
    return vkfw_GetInstanceProcAddr(instance, name);
}

__attribute__((visibility("default")))
VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version)
{
    /* Loader interface v5 is enough for everything we forward. */
    if (*version > 5) *version = 5;
    return VK_SUCCESS;
}
