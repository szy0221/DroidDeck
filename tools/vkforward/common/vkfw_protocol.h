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

enum vkfw_cmd {
    VKFW_CMD_PING = 0,

    /* ---- instance level ---------------------------------------------- */
    VKFW_CMD_ENUMERATE_INSTANCE_VERSION = 10,
    VKFW_CMD_ENUMERATE_INSTANCE_LAYERS,
    VKFW_CMD_ENUMERATE_INSTANCE_EXTENSIONS,
    VKFW_CMD_CREATE_INSTANCE,
    VKFW_CMD_DESTROY_INSTANCE,

    /* ---- physical device (M1 core) ----------------------------------- */
    VKFW_CMD_ENUMERATE_PHYSICAL_DEVICES = 30,
    VKFW_CMD_ENUMERATE_PHYSICAL_DEVICE_GROUPS,
    VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES2,
    VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES2,
    VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES2,
    VKFW_CMD_GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES2,
    VKFW_CMD_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES,
    VKFW_CMD_ENUMERATE_DEVICE_EXTENSIONS,
    VKFW_CMD_ENUMERATE_DEVICE_LAYERS,
    VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_FENCE_PROPERTIES,
    VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_PROPERTIES,

    /* ---- device level ------------------------------------------------- */
    VKFW_CMD_CREATE_DEVICE = 60,
    VKFW_CMD_DESTROY_DEVICE,
    VKFW_CMD_GET_DEVICE_QUEUE,
    VKFW_CMD_DEVICE_WAIT_IDLE,

    /* ---- memory / buffers / images (M2) -------------------------------- */
    VKFW_CMD_ALLOCATE_MEMORY = 80,
    VKFW_CMD_FREE_MEMORY,
    VKFW_CMD_MAP_MEMORY,
    VKFW_CMD_UNMAP_MEMORY,
    VKFW_CMD_CREATE_BUFFER,
    VKFW_CMD_DESTROY_BUFFER,
    VKFW_CMD_GET_BUFFER_MEMORY_REQUIREMENTS,
    VKFW_CMD_BIND_BUFFER_MEMORY,
    VKFW_CMD_CREATE_IMAGE,
    VKFW_CMD_DESTROY_IMAGE,
    VKFW_CMD_GET_IMAGE_MEMORY_REQUIREMENTS,
    VKFW_CMD_BIND_IMAGE_MEMORY,

    /* ---- synchronisation (M2) ----------------------------------------- */
    VKFW_CMD_CREATE_FENCE = 100,
    VKFW_CMD_DESTROY_FENCE,
    VKFW_CMD_RESET_FENCES,
    VKFW_CMD_GET_FENCE_STATUS,
    VKFW_CMD_WAIT_FOR_FENCES,
    VKFW_CMD_CREATE_SEMAPHORE,
    VKFW_CMD_DESTROY_SEMAPHORE,
    VKFW_CMD_GET_SEMAPHORE_COUNTER_VALUE,
    VKFW_CMD_SIGNAL_SEMAPHORE,
    VKFW_CMD_WAIT_SEMAPHORES,

    /* ---- command buffers and submission (M2) --------------------------- */
    VKFW_CMD_CREATE_COMMAND_POOL = 120,
    VKFW_CMD_DESTROY_COMMAND_POOL,
    VKFW_CMD_RESET_COMMAND_POOL,
    VKFW_CMD_ALLOCATE_COMMAND_BUFFERS,
    VKFW_CMD_FREE_COMMAND_BUFFERS,
    VKFW_CMD_BEGIN_COMMAND_BUFFER,
    VKFW_CMD_END_COMMAND_BUFFER,
    VKFW_CMD_RESET_COMMAND_BUFFER,
    VKFW_CMD_QUEUE_SUBMIT,
    VKFW_CMD_QUEUE_WAIT_IDLE,
};

/* Transport-level failures the ICD turns into a Vulkan error instead of
 * hanging the caller. Deliberately outside the VkResult range. */
#define VKFW_ERR_TRANSPORT (-1000)
#define VKFW_ERR_PROTOCOL (-1001)

#endif /* VKFW_PROTOCOL_H */
