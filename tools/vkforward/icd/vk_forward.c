/*
 * Linux-runtime side of the Mali Vulkan forwarding layer.
 *
 * Build result: libvulkan_vkforward.so, a Vulkan ICD that serialises a subset
 * of the API onto the socket the app-side server listens on. See
 * ../../../docs/development/mali-vulkan-forwarding.md for the design and
 * ../../README.md for the build/verification commands.
 *
 * Handle model: every dispatchable handle we hand to the loader starts with
 * loader magic followed by the server-side id. We keep the objects alive so
 * repeated enumeration returns the same pointers, as the spec requires.
 */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#include "vkfw_codec.h"
#include "vkfw_protocol.h"
#include "vkfw_transport.h"

/* ---- client-side objects ------------------------------------------------ */

struct vkfw_object {
    void *loader_magic; /* must stay first for the loader's terminator tricks */
    uint64_t id;        /* server-side handle id */
    uint32_t type;
};

enum {
    VKFW_TYPE_INSTANCE = 1,
    VKFW_TYPE_PHYSICAL_DEVICE,
    VKFW_TYPE_DEVICE,
    VKFW_TYPE_QUEUE,
};

/* Cached children so repeated queries return identical handles. */
struct vkfw_child {
    struct vkfw_child *next;
    uint64_t id;
    uint32_t key_a; /* queue family or enumeration index */
    uint32_t key_b;
    void *object;
};

struct vkfw_instance {
    struct vkfw_object base;
    struct vkfw_child *children; /* physical devices */
};

struct vkfw_device {
    struct vkfw_object base;
    VkPhysicalDevice phys_dev;
    struct vkfw_child *children; /* queues */
};

static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

static void vkfw_obj_init(struct vkfw_object *o, uint32_t type, uint64_t id)
{
    set_loader_magic_value(o);
    o->id = id;
    o->type = type;
}

static void *vkfw_cache_lookup(struct vkfw_child *head, uint64_t id, uint32_t a, uint32_t b)
{
    pthread_mutex_lock(&g_cache_lock);
    void *found = NULL;
    for (struct vkfw_child *n = head; n; n = n->next) {
        if (n->id == id && n->key_a == a && n->key_b == b) { found = n->object; break; }
    }
    pthread_mutex_unlock(&g_cache_lock);
    return found;
}

static void *vkfw_cache_insert(struct vkfw_child **head, uint64_t id, uint32_t a, uint32_t b, void *object)
{
    struct vkfw_child *n = calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->id = id;
    n->key_a = a;
    n->key_b = b;
    n->object = object;
    pthread_mutex_lock(&g_cache_lock);
    n->next = *head;
    *head = n;
    pthread_mutex_unlock(&g_cache_lock);
    return object;
}

static void vkfw_cache_free(struct vkfw_child **head)
{
    pthread_mutex_lock(&g_cache_lock);
    struct vkfw_child *n = *head;
    *head = NULL;
    pthread_mutex_unlock(&g_cache_lock);
    while (n) {
        struct vkfw_child *next = n->next;
        free(n->object);
        free(n);
        n = next;
    }
}

static struct vkfw_instance *vkfw_as_instance(VkInstance i)
{
    return i ? (struct vkfw_instance *)i : NULL;
}

static struct vkfw_device *vkfw_as_device(VkDevice d)
{
    return d ? (struct vkfw_device *)d : NULL;
}

/* ---- transport plumbing ------------------------------------------------- */

static VkResult vkfw_map_error(int32_t result, VkResult on_create)
{
    if (result >= 0) return (VkResult)result;
    if (result == VKFW_ERR_PROTOCOL) return VK_ERROR_INITIALIZATION_FAILED;
    return on_create;
}

/* Runs one command; *reply is owned by the caller and must be freed. */
static int vkfw_invoke(uint16_t cmd, vkfw_buf *payload, vkfw_buf *reply)
{
    if (payload->len > VKFW_MAX_PAYLOAD) return VKFW_ERR_PROTOCOL;
    return vkfw_transact(cmd, payload->data, payload->len, reply);
}

static int32_t vkfw_invoke_status(uint16_t cmd, vkfw_buf *payload, vkfw_buf *reply, vkfw_cur *body)
{
    vkfw_buf_init(reply);
    int rc = vkfw_invoke(cmd, payload, reply);
    if (rc != 0) {
        vkfw_buf_free(reply);
        return rc;
    }
    if (!vkfw_reply_body(reply, body)) return VKFW_ERR_PROTOCOL;
    return vkfw_reply_result(reply);
}

/* ---- entry points ------------------------------------------------------- */

static VkResult vkfw_vkEnumerateInstanceVersion(uint32_t *apiVersion)
{
    if (!apiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_INSTANCE_VERSION, &req, &reply, &body);
    uint32_t version = VK_MAKE_VERSION(1, 0, 0);
    if (rc == VK_SUCCESS) vkfw_cur_u32(&body, &version);
    vkfw_buf_free(&reply);
    vkfw_buf_free(&req);
    *apiVersion = version;
    if (rc != VK_SUCCESS) return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    return VK_SUCCESS;
}

static VkResult vkfw_vkEnumerateInstanceExtensionProperties(const char *layerName, uint32_t *pPropertyCount,
                                                          VkExtensionProperties *pProperties)
{
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_str(&req, layerName);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_INSTANCE_EXTENSIONS, &req, &reply, &body);
    vkfw_buf_free(&req);

    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pPropertyCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        vkfw_buf_free(&reply);
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t wrote = 0;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        char *name = vkfw_cur_str(&body);
        uint32_t version = 0;
        if (!vkfw_cur_u32(&body, &version) || !name) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            free(name);
            break;
        }
        if (i < *pPropertyCount) {
            VkExtensionProperties *dst = &pProperties[wrote];
            memset(dst, 0, sizeof(*dst));
            size_t n = strlen(name);
            if (n >= sizeof(dst->extensionName)) n = sizeof(dst->extensionName) - 1;
            memcpy(dst->extensionName, name, n);
            dst->extensionName[n] = '\0';
            dst->specVersion = version;
            wrote++;
        }
        free(name);
    }
    vkfw_buf_free(&reply);

    if (count > *pPropertyCount) {
        *pPropertyCount = wrote;
        return VK_INCOMPLETE;
    }
    *pPropertyCount = wrote;
    return result;
}

static VkResult vkfw_vkEnumerateInstanceLayerProperties(uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_INSTANCE_LAYERS, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pPropertyCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        vkfw_buf_free(&reply);
        *pPropertyCount = count;
        return VK_SUCCESS;
    }

    uint32_t wrote = 0;
    for (uint32_t i = 0; i < count; i++) {
        char *name = vkfw_cur_str(&body);
        uint32_t spec = 0, impl = 0;
        char *desc = NULL;
        if (!vkfw_cur_u32(&body, &spec) || !vkfw_cur_u32(&body, &impl)) {
            free(name);
            break;
        }
        desc = vkfw_cur_str(&body);
        if (wrote < *pPropertyCount) {
            VkLayerProperties *dst = &pProperties[wrote];
            memset(dst, 0, sizeof(*dst));
            if (name) strncpy(dst->layerName, name, sizeof(dst->layerName) - 1);
            if (desc) strncpy(dst->description, desc, sizeof(dst->description) - 1);
            dst->specVersion = spec;
            dst->implementationVersion = impl;
            wrote++;
        }
        free(name);
        free(desc);
    }
    vkfw_buf_free(&reply);
    if (count > *pPropertyCount) {
        *pPropertyCount = wrote;
        return VK_INCOMPLETE;
    }
    *pPropertyCount = wrote;
    return VK_SUCCESS;
}

/* The loader hands us its own pNext chain (debug utils, layer settings...).
 * Those nodes reference app-side function pointers that cannot cross the
 * socket, so only app/engine info and the name lists travel. */
static VkResult vkfw_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    (void)pAllocator;
    if (!pCreateInfo || !pInstance) return VK_ERROR_INITIALIZATION_FAILED;
    *pInstance = VK_NULL_HANDLE;

    vkfw_buf req;
    vkfw_buf_init(&req);
    const VkApplicationInfo *app = pCreateInfo->pApplicationInfo;
    vkfw_buf_put_u32(&req, app ? app->apiVersion : 0);
    vkfw_buf_put_str(&req, app ? app->pApplicationName : NULL);
    vkfw_buf_put_u32(&req, app ? app->applicationVersion : 0);
    vkfw_buf_put_str(&req, app ? app->pEngineName : NULL);
    vkfw_buf_put_u32(&req, app ? app->engineVersion : 0);
    vkfw_buf_put_u32(&req, pCreateInfo->enabledLayerCount);
    for (uint32_t i = 0; i < pCreateInfo->enabledLayerCount; i++)
        vkfw_buf_put_str(&req, pCreateInfo->ppEnabledLayerNames[i]);
    vkfw_buf_put_u32(&req, pCreateInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++)
        vkfw_buf_put_str(&req, pCreateInfo->ppEnabledExtensionNames[i]);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_CREATE_INSTANCE, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint64_t id = 0;
    if (!vkfw_cur_u64(&body, &id) || id == 0) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vkfw_buf_free(&reply);

    struct vkfw_instance *inst = calloc(1, sizeof(*inst));
    if (!inst) return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkfw_obj_init(&inst->base, VKFW_TYPE_INSTANCE, id);
    *pInstance = (VkInstance)inst;
    return VK_SUCCESS;
}

static void vkfw_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;
    struct vkfw_instance *inst = vkfw_as_instance(instance);
    if (!inst) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, inst->base.id);
    vkfw_buf reply;
    vkfw_buf_init(&reply);
    vkfw_invoke(VKFW_CMD_DESTROY_INSTANCE, &req, &reply);
    vkfw_buf_free(&req);
    vkfw_buf_free(&reply);

    vkfw_cache_free(&inst->children);
    free(inst);
}

static VkResult vkfw_vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                                              VkPhysicalDevice *pPhysicalDevices)
{
    struct vkfw_instance *inst = vkfw_as_instance(instance);
    if (!pPhysicalDeviceCount || !inst) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, inst->base.id);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_PHYSICAL_DEVICES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceCount = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pPhysicalDevices) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceCount = count;
        return VK_SUCCESS;
    }

    uint32_t wrote = 0;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t id = 0;
        if (!vkfw_cur_u64(&body, &id)) { result = VK_ERROR_INITIALIZATION_FAILED; break; }
        if (i >= *pPhysicalDeviceCount) continue;

        void *cached = vkfw_cache_lookup(inst->children, id, i, 0);
        struct vkfw_object *obj = cached;
        if (!obj) {
            obj = calloc(1, sizeof(*obj));
            if (!obj) { result = VK_ERROR_OUT_OF_HOST_MEMORY; break; }
            vkfw_obj_init(obj, VKFW_TYPE_PHYSICAL_DEVICE, id);
            cached = vkfw_cache_insert(&inst->children, id, i, 0, obj);
            if (!cached) { free(obj); result = VK_ERROR_OUT_OF_HOST_MEMORY; break; }
            obj = cached;
        }
        pPhysicalDevices[wrote++] = (VkPhysicalDevice)obj;
    }
    vkfw_buf_free(&reply);

    if (count > *pPhysicalDeviceCount) {
        *pPhysicalDeviceCount = wrote;
        return VK_INCOMPLETE;
    }
    *pPhysicalDeviceCount = wrote;
    return result;
}

static VkResult vkfw_vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
                                                     VkPhysicalDeviceGroupProperties *pProperties)
{
    struct vkfw_instance *inst = vkfw_as_instance(instance);
    if (!pPhysicalDeviceGroupCount || !inst) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, inst->base.id);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_PHYSICAL_DEVICE_GROUPS, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceGroupCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceGroupCount = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProperties) {
        vkfw_buf_free(&reply);
        *pPhysicalDeviceGroupCount = count;
        return VK_SUCCESS;
    }

    uint32_t wrote = 0;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t device_count = 0, subset = 0;
        if (!vkfw_cur_u32(&body, &device_count) || !vkfw_cur_u32(&body, &subset)) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            break;
        }
        if (i >= *pPhysicalDeviceGroupCount) {
            for (uint32_t d = 0; d < device_count; d++) {
                uint64_t id = 0;
                vkfw_cur_u64(&body, &id);
            }
            continue;
        }
        VkPhysicalDeviceGroupProperties *dst = &pProperties[wrote++];
        memset(dst, 0, sizeof(*dst));
        dst->subsetAllocation = subset;
        for (uint32_t d = 0; d < device_count; d++) {
            uint64_t id = 0;
            /* Past the array the ids are still in the reply: leave them there
             * and every group after this one reads the previous group's tail.
             */
            if (!vkfw_cur_u64(&body, &id)) { result = VK_ERROR_INITIALIZATION_FAILED; break; }
            if (d >= VK_MAX_DEVICE_GROUP_SIZE) continue;
            void *cached = vkfw_cache_lookup(inst->children, id, d, 0);
            struct vkfw_object *obj = cached;
            if (!obj) {
                obj = calloc(1, sizeof(*obj));
                if (!obj) { result = VK_ERROR_OUT_OF_HOST_MEMORY; break; }
                vkfw_obj_init(obj, VKFW_TYPE_PHYSICAL_DEVICE, id);
                void *inserted = vkfw_cache_insert(&inst->children, id, d, 0, obj);
                if (!inserted) {
                    free(obj);
                    result = VK_ERROR_OUT_OF_HOST_MEMORY;
                    break;
                }
                obj = inserted;
            }
            dst->physicalDevices[d] = (VkPhysicalDevice)obj;
            dst->physicalDeviceCount++;
        }
        if (result != VK_SUCCESS) break;
    }
    vkfw_buf_free(&reply);

    if (count > *pPhysicalDeviceGroupCount) {
        *pPhysicalDeviceGroupCount = wrote;
        return VK_INCOMPLETE;
    }
    *pPhysicalDeviceGroupCount = wrote;
    return result;
}

static uint64_t vkfw_phys_id(VkPhysicalDevice pd)
{
    struct vkfw_object *o = (struct vkfw_object *)pd;
    return o ? o->id : 0;
}

static VkResult vkfw_get_phys_props(VkPhysicalDevice pd, uint16_t cmd, void *pNext,
                                    VkPhysicalDeviceProperties *out)
{
    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    if (cmd == VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES2) {
        vkfw_chain chain;
        if (!vkfw_chain_collect(pNext, &chain)) {
            vkfw_buf_free(&req);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        vkfw_chain_put(&req, &chain);
        vkfw_chain_free(&chain);
    }

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(cmd, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }
    if (!vkfw_cur_bytes(&body, out, sizeof(*out))) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (cmd == VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES2 && !vkfw_chain_apply(&body, pNext)) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vkfw_buf_free(&reply);
    return VK_SUCCESS;
}

static void vkfw_vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties *pProperties)
{
    if (!pProperties) return;
    memset(pProperties, 0, sizeof(*pProperties));
    if (vkfw_get_phys_props(pd, VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES, NULL, pProperties) != VK_SUCCESS) {
        pProperties->apiVersion = VK_MAKE_VERSION(1, 0, 0);
    }
}

static void vkfw_vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *pProperties)
{
    if (!pProperties) return;
    memset(&pProperties->properties, 0, sizeof(pProperties->properties));
    if (vkfw_get_phys_props(pd, VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES2, pProperties->pNext,
                            &pProperties->properties) != VK_SUCCESS) {
        pProperties->properties.apiVersion = VK_MAKE_VERSION(1, 0, 0);
    }
}

static VkResult vkfw_get_phys_features(VkPhysicalDevice pd, uint16_t cmd, void *pNext,
                                       VkPhysicalDeviceFeatures *out)
{
    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    if (cmd == VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES2) {
        vkfw_chain chain;
        if (!vkfw_chain_collect(pNext, &chain)) {
            vkfw_buf_free(&req);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        vkfw_chain_put(&req, &chain);
        vkfw_chain_free(&chain);
    }

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(cmd, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }
    if (!vkfw_cur_bytes(&body, out, sizeof(*out))) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (cmd == VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES2 && !vkfw_chain_apply(&body, pNext)) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vkfw_buf_free(&reply);
    return VK_SUCCESS;
}

static void vkfw_vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures *pFeatures)
{
    if (!pFeatures) return;
    memset(pFeatures, 0, sizeof(*pFeatures));
    vkfw_get_phys_features(pd, VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES, NULL, pFeatures);
}

static void vkfw_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *pFeatures)
{
    if (!pFeatures) return;
    memset(&pFeatures->features, 0, sizeof(pFeatures->features));
    vkfw_get_phys_features(pd, VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES2, pFeatures->pNext, &pFeatures->features);
}

static void vkfw_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t *pCount,
                                                        VkQueueFamilyProperties *pProps)
{
    if (!pCount) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return;
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return;
    }
    if (!pProps) {
        vkfw_buf_free(&reply);
        *pCount = count;
        return;
    }
    uint32_t n = count < *pCount ? count : *pCount;
    for (uint32_t i = 0; i < n; i++) {
        if (!vkfw_cur_bytes(&body, &pProps[i], sizeof(pProps[i]))) break;
    }
    vkfw_buf_free(&reply);
    *pCount = n;
}

static void vkfw_vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice pd, uint32_t *pCount,
                                                           VkQueueFamilyProperties2 *pProps)
{
    if (!pCount) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, pProps ? *pCount : 0u);
    if (pProps) {
        for (uint32_t i = 0; i < *pCount; i++) {
            vkfw_chain chain;
            if (!vkfw_chain_collect(pProps[i].pNext, &chain)) {
                vkfw_buf_free(&req);
                *pCount = 0;
                return;
            }
            vkfw_chain_put(&req, &chain);
            vkfw_chain_free(&chain);
        }
    }

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES2, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return;
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return;
    }
    if (!pProps) {
        vkfw_buf_free(&reply);
        *pCount = count;
        return;
    }
    uint32_t n = count < *pCount ? count : *pCount;
    for (uint32_t i = 0; i < n; i++) vkfw_cur_bytes(&body, &pProps[i].queueFamilyProperties,
                                                    sizeof(pProps[i].queueFamilyProperties));
    for (uint32_t i = 0; i < n; i++) vkfw_chain_apply(&body, pProps[i].pNext);
    vkfw_buf_free(&reply);
    *pCount = n;
}

static void vkfw_vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd,
                                                      VkPhysicalDeviceMemoryProperties2 *pProps)
{
    if (!pProps) return;
    memset(&pProps->memoryProperties, 0, sizeof(pProps->memoryProperties));

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_chain chain;
    if (!vkfw_chain_collect(pProps->pNext, &chain)) {
        vkfw_buf_free(&req);
        return;
    }
    vkfw_chain_put(&req, &chain);
    vkfw_chain_free(&chain);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES2, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return;
    }
    vkfw_cur_bytes(&body, &pProps->memoryProperties, sizeof(pProps->memoryProperties));
    vkfw_chain_apply(&body, pProps->pNext);
    vkfw_buf_free(&reply);
}

static void vkfw_vkGetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice pd, VkFormat format,
                                                                VkImageType type, VkSampleCountFlagBits samples,
                                                                VkImageUsageFlags usage, VkImageTiling tiling,
                                                                uint32_t *pCount,
                                                                VkSparseImageFormatProperties *pProps)
{
    if (!pCount) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)format);
    vkfw_buf_put_u32(&req, (uint32_t)type);
    vkfw_buf_put_u32(&req, (uint32_t)samples);
    vkfw_buf_put_u32(&req, (uint32_t)usage);
    vkfw_buf_put_u32(&req, (uint32_t)tiling);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return;
    }

    uint32_t count = 0;
    vkfw_cur_u32(&body, &count);
    if (!pProps) {
        vkfw_buf_free(&reply);
        *pCount = count;
        return;
    }
    uint32_t n = count < *pCount ? count : *pCount;
    for (uint32_t i = 0; i < n; i++) vkfw_cur_bytes(&body, &pProps[i], sizeof(pProps[i]));
    vkfw_buf_free(&reply);
    *pCount = n;
}

static VkResult vkfw_vkGetPhysicalDeviceToolProperties(VkPhysicalDevice pd, uint32_t *pToolCount,
                                                       VkPhysicalDeviceToolProperties *pTools)
{
    if (!pToolCount) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_TOOL_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pToolCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pToolCount = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pTools) {
        vkfw_buf_free(&reply);
        *pToolCount = count;
        return VK_SUCCESS;
    }
    uint32_t wrote = 0;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceToolProperties props;
        memset(&props, 0, sizeof(props));
        if (!vkfw_cur_bytes(&body, &props, sizeof(props))) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            break;
        }
        if (i < *pToolCount) pTools[wrote++] = props;
    }
    vkfw_buf_free(&reply);

    if (count > *pToolCount) {
        *pToolCount = wrote;
        return VK_INCOMPLETE;
    }
    *pToolCount = wrote;
    return result;
}

static void vkfw_vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice pd, const VkPhysicalDeviceSparseImageFormatInfo2 *info, uint32_t *pCount,
    VkSparseImageFormatProperties2 *pProps)
{
    if (!info || !pCount) return;
    /* Only core fields of the info struct are forwarded (see the 1.0 variant),
     * and the properties are collected into a flat array first: the caller's
     * elements are VkSparseImageFormatProperties2, which are wider than the
     * payload items and must not be written through contiguously. */
    uint32_t count = 0;
    vkfw_vkGetPhysicalDeviceSparseImageFormatProperties(pd, info->format, info->type, info->samples,
                                                        info->usage, info->tiling, &count, NULL);
    if (!pProps) {
        *pCount = count;
        return;
    }
    VkSparseImageFormatProperties *flat = NULL;
    if (count > 0) {
        flat = calloc(count, sizeof(*flat));
        if (!flat) {
            *pCount = 0;
            return;
        }
    }
    uint32_t got = count;
    vkfw_vkGetPhysicalDeviceSparseImageFormatProperties(pd, info->format, info->type, info->samples,
                                                        info->usage, info->tiling, &got, flat);
    for (uint32_t i = 0; i < got; i++) pProps[i].properties = flat[i];
    free(flat);
    *pCount = got;
}

static void vkfw_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd,
                                                   VkPhysicalDeviceMemoryProperties *pProps)
{
    if (!pProps) return;
    memset(pProps, 0, sizeof(*pProps));

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return;
    }
    vkfw_cur_bytes(&body, pProps, sizeof(*pProps));
    vkfw_buf_free(&reply);
}

static void vkfw_vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, VkFormat format,
                                                   VkFormatProperties *pProps)
{
    if (!pProps) return;
    memset(pProps, 0, sizeof(*pProps));

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)format);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return;
    }
    vkfw_cur_bytes(&body, pProps, sizeof(*pProps));
    vkfw_buf_free(&reply);
}

static void vkfw_vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice pd, VkFormat format,
                                                      VkFormatProperties2 *pProps)
{
    if (!pProps) return;
    vkfw_vkGetPhysicalDeviceFormatProperties(pd, format, &pProps->formatProperties);
}


static VkResult vkfw_vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pd, VkFormat format,
                                                            VkImageType type, VkImageTiling tiling,
                                                            VkImageUsageFlags usage, VkImageCreateFlags flags,
                                                            VkImageFormatProperties *pProps)
{
    if (!pProps) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)format);
    vkfw_buf_put_u32(&req, (uint32_t)type);
    vkfw_buf_put_u32(&req, (uint32_t)tiling);
    vkfw_buf_put_u32(&req, (uint32_t)usage);
    vkfw_buf_put_u32(&req, (uint32_t)flags);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }
    VkResult result = (VkResult)rc;
    vkfw_cur_bytes(&body, pProps, sizeof(*pProps));
    vkfw_buf_free(&reply);
    return result;
}

static VkResult vkfw_vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice pd, const VkPhysicalDeviceImageFormatInfo2 *info, VkImageFormatProperties2 *props)
{
    if (!info || !props) return VK_ERROR_INITIALIZATION_FAILED;
    /* Same caveat as the sparse variant: pNext nodes of the info struct may
     * carry pointers, so only core fields are forwarded. */
    return vkfw_vkGetPhysicalDeviceImageFormatProperties(pd, info->format, info->type, info->tiling,
                                                         info->usage, info->flags, &props->imageFormatProperties);
}

static VkResult vkfw_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *pLayerName,
                                                        uint32_t *pCount, VkExtensionProperties *pProps)
{
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_str(&req, pLayerName);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_ENUMERATE_DEVICE_EXTENSIONS, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint32_t count = 0;
    if (!vkfw_cur_u32(&body, &count)) {
        vkfw_buf_free(&reply);
        *pCount = 0;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pProps) {
        vkfw_buf_free(&reply);
        *pCount = count;
        return VK_SUCCESS;
    }
    uint32_t wrote = 0;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        char *name = vkfw_cur_str(&body);
        uint32_t version = 0;
        if (!vkfw_cur_u32(&body, &version) || !name) {
            free(name);
            result = VK_ERROR_INITIALIZATION_FAILED;
            break;
        }
        if (i < *pCount) {
            VkExtensionProperties *dst = &pProps[wrote];
            memset(dst, 0, sizeof(*dst));
            size_t n = strlen(name);
            if (n >= sizeof(dst->extensionName)) n = sizeof(dst->extensionName) - 1;
            memcpy(dst->extensionName, name, n);
            dst->extensionName[n] = '\0';
            dst->specVersion = version;
            wrote++;
        }
        free(name);
    }
    vkfw_buf_free(&reply);

    if (count > *pCount) {
        *pCount = wrote;
        return VK_INCOMPLETE;
    }
    *pCount = wrote;
    return result;
}

static VkResult vkfw_vkEnumerateDeviceLayerProperties(VkPhysicalDevice pd, uint32_t *pCount,
                                                    VkLayerProperties *pProps)
{
    if (!pCount) return VK_ERROR_INITIALIZATION_FAILED;
    (void)pd;
    if (pProps) memset(pProps, 0, sizeof(*pProps) * (*pCount));
    *pCount = 0;
    return VK_SUCCESS;
}

static void vkfw_vkGetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice pd,
                                                           const VkPhysicalDeviceExternalBufferInfo *info,
                                                           VkExternalBufferProperties *props)
{
    if (!info || !props) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)info->flags);
    vkfw_buf_put_u32(&req, (uint32_t)info->usage);
    vkfw_buf_put_u32(&req, (uint32_t)info->handleType);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc == VK_SUCCESS) vkfw_cur_bytes(&body, props, sizeof(*props));
    vkfw_buf_free(&reply);
}

static void vkfw_vkGetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice pd,
                                                          const VkPhysicalDeviceExternalFenceInfo *info,
                                                          VkExternalFenceProperties *props)
{
    if (!info || !props) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)info->handleType);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_FENCE_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc == VK_SUCCESS) vkfw_cur_bytes(&body, props, sizeof(*props));
    vkfw_buf_free(&reply);
}

static void vkfw_vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice pd, const VkPhysicalDeviceExternalSemaphoreInfo *info,
    VkExternalSemaphoreProperties *props)
{
    if (!info || !props) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    vkfw_buf_put_u32(&req, (uint32_t)info->handleType);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_PROPERTIES, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc == VK_SUCCESS) vkfw_cur_bytes(&body, props, sizeof(*props));
    vkfw_buf_free(&reply);
}

static VkResult vkfw_vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *info,
                                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    (void)pAllocator;
    if (!info || !pDevice || info->queueCreateInfoCount == 0) return VK_ERROR_INITIALIZATION_FAILED;
    *pDevice = VK_NULL_HANDLE;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, vkfw_phys_id(pd));
    VkPhysicalDeviceFeatures features;
    memset(&features, 0, sizeof(features));
    if (info->pEnabledFeatures) features = *info->pEnabledFeatures;
    vkfw_buf_put(&req, &features, sizeof(features));

    vkfw_chain chain;
    if (!vkfw_chain_collect(info->pNext, &chain)) {
        vkfw_buf_free(&req);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    vkfw_chain_put(&req, &chain);
    vkfw_chain_free(&chain);

    vkfw_buf_put_u32(&req, info->queueCreateInfoCount);
    for (uint32_t i = 0; i < info->queueCreateInfoCount; i++) {
        const VkDeviceQueueCreateInfo *q = &info->pQueueCreateInfos[i];
        uint32_t count = q->queueCount;
        vkfw_buf_put_u32(&req, q->queueFamilyIndex);
        vkfw_buf_put_u32(&req, count);
        for (uint32_t n = 0; n < count; n++) {
            float priority = q->pQueuePriorities ? q->pQueuePriorities[n] : 1.0f;
            uint32_t bits = 0;
            memcpy(&bits, &priority, sizeof(bits));
            vkfw_buf_put_u32(&req, bits);
        }
    }

    vkfw_buf_put_u32(&req, info->enabledLayerCount);
    for (uint32_t i = 0; i < info->enabledLayerCount; i++)
        vkfw_buf_put_str(&req, info->ppEnabledLayerNames[i]);
    vkfw_buf_put_u32(&req, info->enabledExtensionCount);
    for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
        vkfw_buf_put_str(&req, info->ppEnabledExtensionNames[i]);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_CREATE_DEVICE, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return vkfw_map_error(rc, VK_ERROR_INITIALIZATION_FAILED);
    }

    uint64_t id = 0;
    if (!vkfw_cur_u64(&body, &id) || id == 0) {
        vkfw_buf_free(&reply);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vkfw_buf_free(&reply);

    struct vkfw_device *dev = calloc(1, sizeof(*dev));
    if (!dev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkfw_obj_init(&dev->base, VKFW_TYPE_DEVICE, id);
    dev->phys_dev = pd;
    *pDevice = (VkDevice)dev;
    return VK_SUCCESS;
}

static void vkfw_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
    (void)pAllocator;
    struct vkfw_device *dev = vkfw_as_device(device);
    if (!dev) return;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, dev->base.id);
    vkfw_buf reply;
    vkfw_buf_init(&reply);
    vkfw_invoke(VKFW_CMD_DESTROY_DEVICE, &req, &reply);
    vkfw_buf_free(&req);
    vkfw_buf_free(&reply);

    vkfw_cache_free(&dev->children);
    free(dev);
}

static VkResult vkfw_vkDeviceWaitIdle(VkDevice device)
{
    struct vkfw_device *dev = vkfw_as_device(device);
    if (!dev) return VK_ERROR_DEVICE_LOST;

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, dev->base.id);
    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_DEVICE_WAIT_IDLE, &req, &reply, &body);
    vkfw_buf_free(&req);
    vkfw_buf_free(&reply);
    return rc == VK_SUCCESS ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

static void vkfw_vkGetDeviceQueue(VkDevice device, uint32_t familyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
    if (!pQueue) return;
    *pQueue = VK_NULL_HANDLE;
    struct vkfw_device *dev = vkfw_as_device(device);
    if (!dev) return;

    void *cached = vkfw_cache_lookup(dev->children, 0, familyIndex, queueIndex);
    if (cached) {
        *pQueue = (VkQueue)cached;
        return;
    }

    vkfw_buf req;
    vkfw_buf_init(&req);
    vkfw_buf_put_u64(&req, dev->base.id);
    vkfw_buf_put_u32(&req, familyIndex);
    vkfw_buf_put_u32(&req, queueIndex);

    vkfw_buf reply;
    vkfw_cur body;
    int32_t rc = vkfw_invoke_status(VKFW_CMD_GET_DEVICE_QUEUE, &req, &reply, &body);
    vkfw_buf_free(&req);
    if (rc != VK_SUCCESS) {
        vkfw_buf_free(&reply);
        return;
    }
    uint64_t id = 0;
    if (!vkfw_cur_u64(&body, &id)) {
        vkfw_buf_free(&reply);
        return;
    }
    vkfw_buf_free(&reply);

    struct vkfw_object *obj = calloc(1, sizeof(*obj));
    if (!obj) return;
    vkfw_obj_init(obj, VKFW_TYPE_QUEUE, id);
    *pQueue = (VkQueue)vkfw_cache_insert(&dev->children, 0, familyIndex, queueIndex, obj);
}

/* ---- dispatchables ------------------------------------------------------ */

/* Set VKFW_TRACE=1 to log the entry points we cannot serve. Those are exactly
 * the calls a real Vulkan app will crash on, so this is the first thing to look
 * at when something misbehaves on device. */
static void vkfw_trace_missing(const char *table, const char *pName)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("VKFW_TRACE") != NULL;
    /* Which table matters: a miss on the device table is what a tool reaches
     * for only after it has a device, and a NULL it does not check for is a
     * call through address zero. */
    if (enabled) fprintf(stderr, "vkforward: unsupported %s entry point %s\n", table, pName);
}

/*
 * The loader builds a trampoline for every core command whether or not the ICD
 * behind it can serve that command, and the trampoline dispatches through a
 * dispatch table it filled from this ICD. So a core command we answer NULL for
 * is not a polite refusal, it is a call through address zero: a segfault at
 * 0x0, with a stack that names nothing. vulkaninfo dies that way the moment it
 * has a device, which is exactly when M1 has nothing left to hide behind.
 *
 * Extension commands are the opposite and must keep answering NULL: the loader
 * answers NULL for those itself once the ICD does not have them, and that NULL
 * is what an app checks before using the extension.
 */
static const char *const VKFW_VENDOR_TAGS[] = {
    "KHR", "EXT", "NV", "NVX", "AMD", "AMDX", "ARM", "ANDROID", "MVK", "KHX",
    "QCOM", "INTEL", "IMG", "FUCHSIA", "SEC", "VALVE", "GOOGLE", "HUAWEI",
    "MTL", "NN", "QNX", "OHOS", "MSFT", "NSOBJC", NULL,
};

static bool vkfw_is_core_command(const char *pName)
{
    size_t n = strlen(pName);
    for (const char *const *tag = VKFW_VENDOR_TAGS; *tag; tag++) {
        size_t taglen = strlen(*tag);
        if (n > taglen && strcmp(pName + n - taglen, *tag) == 0) return false;
    }
    return true;
}

/* Stands in for every core command this ICD has not reached yet. AArch64
 * passes arguments in registers and the caller cleans up nothing, so one
 * no-argument function answers every signature: whatever the caller put in
 * x0-x7 is simply left alone, and x0 comes back holding the error. */
static VKAPI_ATTR VkResult VKAPI_CALL vkfw_core_not_implemented(void)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/* NULL for an extension we cannot serve, a stub for a core command we cannot
 * serve yet. M2 replaces the stubs one at a time. */
static PFN_vkVoidFunction vkfw_absent(const char *table, const char *pName)
{
    vkfw_trace_missing(table, pName);
    if (!vkfw_is_core_command(pName)) return NULL;
    return (PFN_vkVoidFunction)(size_t)&vkfw_core_not_implemented;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkfw_vkGetDeviceProcAddr(VkDevice device, const char *pName);

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkfw_vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    /* Queries that reach us with instance == NULL come from the loader probing
     * the ICD before any instance exists. */
    (void)instance;
    if (!pName) return NULL;

#define ENTRY(name)                                                                            \
    if (!strcmp(pName, #name)) return (PFN_vkVoidFunction)(size_t)&vkfw_##name
/* A pre-promotion / vendor spelling that points at the same implementation. */
#define ALIAS(name, impl)                                                                      \
    if (!strcmp(pName, #name)) return (PFN_vkVoidFunction)(size_t)&vkfw_vk##impl

    ENTRY(vkGetInstanceProcAddr);
    ENTRY(vkEnumerateInstanceVersion);
    ENTRY(vkEnumerateInstanceExtensionProperties);
    ENTRY(vkEnumerateInstanceLayerProperties);
    ENTRY(vkCreateInstance);
    ENTRY(vkDestroyInstance);
    ENTRY(vkEnumeratePhysicalDevices);
    ENTRY(vkEnumeratePhysicalDeviceGroups);
    ENTRY(vkGetPhysicalDeviceProperties);
    ENTRY(vkGetPhysicalDeviceProperties2);
    ENTRY(vkGetPhysicalDeviceFeatures);
    ENTRY(vkGetPhysicalDeviceFeatures2);
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties);
    ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2);
    ENTRY(vkGetPhysicalDeviceMemoryProperties);
    ENTRY(vkGetPhysicalDeviceMemoryProperties2);
    ENTRY(vkGetPhysicalDeviceFormatProperties);
    ENTRY(vkGetPhysicalDeviceFormatProperties2);
    ENTRY(vkGetPhysicalDeviceImageFormatProperties);
    ENTRY(vkGetPhysicalDeviceImageFormatProperties2);
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties);
    ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties2);
    ENTRY(vkEnumerateDeviceExtensionProperties);
    ENTRY(vkEnumerateDeviceLayerProperties);
    ENTRY(vkGetPhysicalDeviceExternalBufferProperties);
    ENTRY(vkGetPhysicalDeviceExternalFenceProperties);
    ENTRY(vkGetPhysicalDeviceExternalSemaphoreProperties);
    ENTRY(vkGetPhysicalDeviceToolProperties);
    /* Pre-promotion aliases: tools that enable VK_KHR_get_physical_device_properties2
     * still call these commands under their KHR names, and the EXT-suffixed tool
     * properties is the same command, so all spellings must resolve. */
    ALIAS(vkEnumeratePhysicalDeviceGroupsKHR, EnumeratePhysicalDeviceGroups);
    ALIAS(vkGetPhysicalDeviceProperties2KHR, GetPhysicalDeviceProperties2);
    ALIAS(vkGetPhysicalDeviceFeatures2KHR, GetPhysicalDeviceFeatures2);
    ALIAS(vkGetPhysicalDeviceQueueFamilyProperties2KHR, GetPhysicalDeviceQueueFamilyProperties2);
    ALIAS(vkGetPhysicalDeviceMemoryProperties2KHR, GetPhysicalDeviceMemoryProperties2);
    ALIAS(vkGetPhysicalDeviceFormatProperties2KHR, GetPhysicalDeviceFormatProperties2);
    ALIAS(vkGetPhysicalDeviceImageFormatProperties2KHR, GetPhysicalDeviceImageFormatProperties2);
    ALIAS(vkGetPhysicalDeviceSparseImageFormatProperties2KHR, GetPhysicalDeviceSparseImageFormatProperties2);
    ALIAS(vkGetPhysicalDeviceExternalBufferPropertiesKHR, GetPhysicalDeviceExternalBufferProperties);
    ALIAS(vkGetPhysicalDeviceExternalFencePropertiesKHR, GetPhysicalDeviceExternalFenceProperties);
    ALIAS(vkGetPhysicalDeviceExternalSemaphorePropertiesKHR, GetPhysicalDeviceExternalSemaphoreProperties);
    ALIAS(vkGetPhysicalDeviceToolPropertiesEXT, GetPhysicalDeviceToolProperties);
    ENTRY(vkCreateDevice);
    ENTRY(vkDestroyDevice);
    ENTRY(vkGetDeviceProcAddr);
    ENTRY(vkGetDeviceQueue);
    ENTRY(vkDeviceWaitIdle);
#undef ENTRY
#undef ALIAS
    return vkfw_absent("instance", pName);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkfw_vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    (void)device;
    if (!pName) return NULL;

#define ENTRY(name)                                                                            \
    if (!strcmp(pName, #name)) return (PFN_vkVoidFunction)(size_t)&vkfw_##name

    ENTRY(vkGetDeviceProcAddr);
    ENTRY(vkDestroyDevice);
    ENTRY(vkGetDeviceQueue);
    ENTRY(vkDeviceWaitIdle);
#undef ENTRY
#undef ALIAS
    return vkfw_absent("device", pName);
}

/* ---- loader-facing exports ---------------------------------------------- */

__attribute__((visibility("default")))
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    return vkfw_vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default")))
PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char *pName)
{
    return vkfw_vkGetInstanceProcAddr(instance, pName);
}

__attribute__((visibility("default")))
VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion)
{
    /* Interface 5 is the loader's requirement for an ICD advertising Vulkan
     * 1.1 (policy LDP_DRIVER_7); vk_icdGetPhysicalDeviceProcAddr, which v4+
     * asks for, is exported below. */
    if (!pSupportedVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pSupportedVersion > 5) *pSupportedVersion = 5;
    return VK_SUCCESS;
}
