/*
 * vkforward wire protocol — shared by the Linux-side ICD stub and the
 * Android-side server.
 *
 * Frame layout (little-endian, both halves must run the same ABI so that
 * native Vulkan POD structs can travel as blobs — see ../../README.md):
 *
 *   [u32 magic 'VKFW'][u16 cmd][u16 flags][u32 payload_len][u64 seq][payload]
 *
 * Every request gets exactly one reply; the first 4 bytes of any reply payload
 * hold a VkResult. A reply is sent even when the call failed on the server,
 * so the client never blocks.
 */
#ifndef VKFW_PROTOCOL_H
#define VKFW_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define VKFW_MAGIC 0x564B4657u /* 'VKFW' */
#define VKFW_HEADER_SIZE 20u
#define VKFW_MAX_PAYLOAD (8u * 1024u * 1024u)
#define VKFW_MAX_CHAIN_NODES 64u
#define VKFW_MAX_CHAIN_NODE_BYTES (64u * 1024u)

/* Transport endpoints. The Android server binds the abstract name first; the
 * ICD tries VKFW_SOCKET, then the file socket, then the abstract name. */
#define VKFW_SOCKET_ENV "VKFW_SOCKET"
#define VKFW_FS_SOCKET "/tmp/vkforward.sock"
#define VKFW_ABSTRACT_NAME "vkforward"

/* ---- window system integration ------------------------------------------
 *
 * The ICD owns WSI, the driver does not take part. Android's Mali driver only
 * offers VK_KHR_android_surface: it has never heard of a Wayland compositor,
 * and it cannot be handed one anyway, because the wl_surface being wrapped
 * lives in the runtime's process and its address means nothing on this side.
 *
 * So these four names are claimed by the ICD and filtered out of everything
 * the driver reports, in both directions. Enumerating them asks the driver for
 * its list and appends these; creating an instance asks the driver without
 * them. Keeping the list in one place is what keeps the two halves agreeing:
 * a name claimed but not filtered is an instance that never gets created.
 */
#define VKFW_WSI_EXTENSIONS(X)                                                                 \
    X("VK_KHR_surface")                                                                        \
    X("VK_KHR_swapchain")                                                                      \
    X("VK_KHR_wayland_surface")                                                                \
    X("VK_EXT_surface_maintenance1")

/* This layer is the Linux runtime's window system integration, which means it
 * has to accept Wayland objects. Neither of its builds does: the NDK has no
 * wayland-client.h, and vk_forward.c is compiled there too, for the loopback
 * test that drives both halves in one process. Rather than make every build
 * depend on Wayland headers, declare the struct here, for both halves.
 *
 * Plain types only, deliberately: vkfw_protocol.h is included by transport and
 * codec, which otherwise need no Vulkan headers at all, and pulling vulkan.h in
 * here would give every translation unit that dependency. The layout matches
 * the C99 rules Vulkan structs follow, which is the assumption this whole
 * layer already rests on. Nothing dereferences those pointers - they are
 * opaque handles the caller keeps and hands back at destroy time.
 */
#ifndef VK_USE_PLATFORM_WAYLAND_KHR
typedef struct VkWaylandSurfaceCreateInfoKHR {
    int32_t sType;
    const void *pNext;
    uint32_t flags;
    void *display; /* wl_display * */
    void *surface; /* wl_surface * */
} VkWaylandSurfaceCreateInfoKHR;

typedef int32_t (*PFN_vkCreateWaylandSurfaceKHR)(void *instance,
                                                 const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                                                 const void *pAllocator,
                                                 void *pSurface);

typedef uint32_t (*PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)(void *physicalDevice,
                                                                        uint32_t queueFamilyIndex,
                                                                        void *display);
#endif

/* True when the ICD serves this extension itself, so the driver must not be
 * told about it. */
static inline int vkfw_wsi_owned(const char *name)
{
    static const char *const owned[] = {
#define X(n) n,
        VKFW_WSI_EXTENSIONS(X)
#undef X
        NULL
    };
    for (const char *const *p = owned; *p; p++) {
        const char *a = *p, *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return 1;
    }
    return 0;
}

struct vkfw_header {
    uint32_t magic;
    uint16_t cmd;
    uint16_t flags;
    uint32_t payload_len;
    uint64_t seq;
};

/*
 * The commands are written down once and consumed twice: once as the enum the
 * wire uses, once as the names VKFW_TRACE prints. A new command therefore
 * cannot end up on the wire without a name to report it by, which matters
 * because a trace is the only thing this layer can leave behind on a device
 * nobody can attach a debugger to.
 *
 * The second argument is the enumerator's assignment, or nothing at all for
 * the members that follow on from the one above them.
 */
#define VKFW_COMMANDS(X)                                                       \
    X(PING, = 0)                                                               \
    X(ENUMERATE_INSTANCE_VERSION, = 10)                                        \
    X(ENUMERATE_INSTANCE_LAYERS, )                                             \
    X(ENUMERATE_INSTANCE_EXTENSIONS, )                                         \
    X(CREATE_INSTANCE, )                                                       \
    X(DESTROY_INSTANCE, )                                                      \
    X(ENUMERATE_PHYSICAL_DEVICES, = 30)                                        \
    X(ENUMERATE_PHYSICAL_DEVICE_GROUPS, )                                      \
    X(GET_PHYSICAL_DEVICE_PROPERTIES, )                                        \
    X(GET_PHYSICAL_DEVICE_PROPERTIES2, )                                       \
    X(GET_PHYSICAL_DEVICE_FEATURES, )                                          \
    X(GET_PHYSICAL_DEVICE_FEATURES2, )                                         \
    X(GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES, )                           \
    X(GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES2, )                          \
    X(GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES, )                    \
    X(GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES, )                                 \
    X(GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES2, )                                \
    X(GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES, )                                 \
    X(GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES, )                           \
    X(ENUMERATE_DEVICE_EXTENSIONS, )                                           \
    X(ENUMERATE_DEVICE_LAYERS, )                                               \
    X(GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES, )                        \
    X(GET_PHYSICAL_DEVICE_EXTERNAL_FENCE_PROPERTIES, )                         \
    X(GET_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_PROPERTIES, )                     \
    X(GET_PHYSICAL_DEVICE_TOOL_PROPERTIES, )                                   \
    X(CREATE_DEVICE, = 60)                                                     \
    X(DESTROY_DEVICE, )                                                        \
    X(GET_DEVICE_QUEUE, )                                                      \
    X(DEVICE_WAIT_IDLE, )                                                      \
    X(ALLOCATE_MEMORY, = 80)                                                   \
    X(FREE_MEMORY, )                                                           \
    X(MAP_MEMORY, )                                                            \
    X(UNMAP_MEMORY, )                                                          \
    X(CREATE_BUFFER, )                                                         \
    X(DESTROY_BUFFER, )                                                        \
    X(GET_BUFFER_MEMORY_REQUIREMENTS, )                                        \
    X(BIND_BUFFER_MEMORY, )                                                    \
    X(CREATE_IMAGE, )                                                          \
    X(DESTROY_IMAGE, )                                                         \
    X(GET_IMAGE_MEMORY_REQUIREMENTS, )                                         \
    X(GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS, )                                  \
    X(BIND_IMAGE_MEMORY, )                                                     \
    X(CREATE_FENCE, = 100)                                                     \
    X(DESTROY_FENCE, )                                                         \
    X(RESET_FENCES, )                                                          \
    X(GET_FENCE_STATUS, )                                                      \
    X(WAIT_FOR_FENCES, )                                                       \
    X(CREATE_SEMAPHORE, )                                                      \
    X(DESTROY_SEMAPHORE, )                                                     \
    X(GET_SEMAPHORE_COUNTER_VALUE, )                                           \
    X(SIGNAL_SEMAPHORE, )                                                      \
    X(WAIT_SEMAPHORES, )                                                       \
    X(CREATE_COMMAND_POOL, = 120)                                              \
    X(DESTROY_COMMAND_POOL, )                                                  \
    X(RESET_COMMAND_POOL, )                                                    \
    X(ALLOCATE_COMMAND_BUFFERS, )                                              \
    X(FREE_COMMAND_BUFFERS, )                                                  \
    X(BEGIN_COMMAND_BUFFER, )                                                  \
    X(END_COMMAND_BUFFER, )                                                    \
    X(RESET_COMMAND_BUFFER, )                                                  \
    X(QUEUE_SUBMIT, )                                                          \
    X(QUEUE_WAIT_IDLE, )

#define VKFW_CMD_ENUM(name, assign) VKFW_CMD_##name assign,

enum vkfw_cmd { VKFW_COMMANDS(VKFW_CMD_ENUM) };

#undef VKFW_CMD_ENUM

const char *vkfw_cmd_name(uint16_t cmd);

/* Transport-level failures the ICD turns into a Vulkan error instead of
 * hanging the caller. Deliberately outside the VkResult range. */
#define VKFW_ERR_TRANSPORT (-1000)
#define VKFW_ERR_PROTOCOL (-1001)

#endif /* VKFW_PROTOCOL_H */
