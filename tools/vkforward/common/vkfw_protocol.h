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
