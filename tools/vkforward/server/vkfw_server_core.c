#include "vkfw_server_core.h"
#include "vkfw_transport.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#ifdef __ANDROID__
#include <android/log.h>
#define VKFW_LOG_TAG "vkforward"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, VKFW_LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "[vkforward] " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                   \
    } while (0)
#define LOGW(...)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "[vkforward] " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                   \
    } while (0)
#define LOGE(...)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "[vkforward] " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                   \
    } while (0)
#define LOGD(...)                                                                              \
    do {                                                                                       \
        if (getenv("VKFW_VERBOSE")) {                                                          \
            fprintf(stderr, "[vkforward] " __VA_ARGS__);                                       \
            fputc('\n', stderr);                                                               \
        }                                                                                      \
    } while (0)
#endif

#define VKFW_MAX_QUEUES 16u
#define VKFW_MAX_QUEUE_COUNT 64u

/* ---- driver loading ----------------------------------------------------- */

static void *g_libvulkan;
static PFN_vkGetInstanceProcAddr g_gipa;

/* VKFW_DRIVER is an escape hatch for the host loopback tests; on device the
 * loader resolves ro.hardware.vulkan -> vulkan.mali.so, so no vendor path has
 * to be hardcoded. The raw HAL is a last resort. */
static const char *const kDrivers[] = {
#ifdef __ANDROID__
    "libvulkan.so",
    "/vendor/lib64/hw/vulkan.mali.so",
#else
    "libvulkan.so.1",
    "libvulkan.so",
#endif
};

int vkfw_server_init(const char *driver_path)
{
    if (g_gipa) return 0;

    for (int i = -1; i < (int)(sizeof(kDrivers) / sizeof(*kDrivers)); i++) {
        const char *path = (i < 0 && driver_path && *driver_path) ? driver_path : kDrivers[i + 1];
        if (!path || !*path) continue;

        void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            LOGD("dlopen(%s): %s", path, dlerror());
            continue;
        }
        PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
        if (!gipa) {
            /* A bare ICD exports only the ICD entry point, which has the same
             * signature for global queries. */
            gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
        }
        if (!gipa) {
            dlclose(lib);
            continue;
        }
        g_libvulkan = lib;
        g_gipa = gipa;
        LOGI("loaded Vulkan driver %s", path);
        return 0;
    }
    LOGE("no usable Vulkan library found");
    return -1;
}

/* Global commands are resolvable with a NULL instance; everything else needs
 * the owning instance, which is why every handle remembers its parent. */
static void *vkfw_global_proc(const char *name) { return (void *)(size_t)g_gipa(NULL, name); }

static void *vkfw_instance_proc(VkInstance instance, const char *name)
{
    return instance ? (void *)(size_t)g_gipa(instance, name) : NULL;
}

/*
 * Device commands are asked for of the device.
 *
 * Going through the instance works for the commands Vulkan 1.0 defined, and
 * quietly stops working at 1.2: a driver asked for vkSignalSemaphore on an
 * instance it was told is older than that answers NULL, not because it cannot
 * do it but because the question was put to the wrong object. The layer looked
 * broken in a way that had nothing to do with forwarding - the create calls
 * around it all worked, so only the newer half of a resource failed.
 *
 * vkGetDeviceProcAddr is where a device command is meant to be asked for, and
 * it answers for every command the device supports. The instance is still the
 * fallback, for the commands that live there and nowhere else.
 */
static void *vkfw_device_proc(VkInstance instance, VkDevice device, const char *name)
{
    if (!device) return NULL;
    PFN_vkGetDeviceProcAddr gdpa =
        (PFN_vkGetDeviceProcAddr)(instance ? vkfw_instance_proc(instance, "vkGetDeviceProcAddr")
                                           : NULL);
    if (gdpa) {
        void *fn = (void *)(size_t)gdpa(device, name);
        if (fn) return fn;
    }
    return vkfw_instance_proc(instance, name);
}

/* ---- per-connection state ----------------------------------------------- */

enum vkfw_handle_type {
    VKFW_OBJ_NONE = 0,
    VKFW_OBJ_INSTANCE,
    VKFW_OBJ_PHYSICAL_DEVICE,
    VKFW_OBJ_DEVICE,
    VKFW_OBJ_QUEUE,
    VKFW_OBJ_IMAGE,
    VKFW_OBJ_DEVICE_MEMORY,
    VKFW_OBJ_BUFFER,
    VKFW_OBJ_FENCE,
    VKFW_OBJ_SEMAPHORE,
};

struct vkfw_handle_slot {
    uint32_t type;
    uint64_t real;  /* driver-side handle, as an integer */
    uint64_t owner; /* handle id of the parent object */
};

struct vkfw_handles {
    struct vkfw_handle_slot *v;
    uint32_t count;
    uint32_t cap;
};

struct vkfw_conn {
    int fd;
    struct vkfw_handles handles;
};

struct vkfw_request {
    struct vkfw_conn *conn;
    uint64_t seq;
    vkfw_cur in;
    vkfw_buf *out;
    /* A descriptor to hand over with the reply, or -1. Set by handlers that
     * allocate memory the client is meant to map or export; the reply owns it
     * once vkfw_reply has sent it. */
    int passfd;
};

/* Ids are 1-based indexes into the slot array; destroyed slots are reused.
 * One connection is served by exactly one thread, so no locking here. */
static uint64_t vkfw_handle_add(struct vkfw_handles *h, uint32_t type, void *real, uint64_t owner)
{
    struct vkfw_handle_slot *slot = NULL;
    uint64_t id = 0;
    for (uint32_t i = 0; i < h->count; i++) {
        if (h->v[i].type == VKFW_OBJ_NONE) {
            slot = &h->v[i];
            id = i + 1;
            break;
        }
    }
    if (!slot) {
        if (h->count == h->cap) {
            uint32_t cap = h->cap ? h->cap * 2 : 32;
            struct vkfw_handle_slot *grown = realloc(h->v, cap * sizeof(*grown));
            if (!grown) return 0;
            memset(grown + h->cap, 0, (cap - h->cap) * sizeof(*grown));
            h->v = grown;
            h->cap = cap;
        }
        slot = &h->v[h->count];
        id = ++h->count;
    }
    slot->type = type;
    slot->real = (uint64_t)(uintptr_t)real;
    slot->owner = owner;
    return id;
}

static void *vkfw_handle_get(struct vkfw_handles *h, uint64_t id, uint32_t want)
{
    if (id == 0 || id > h->count) return NULL;
    struct vkfw_handle_slot *s = &h->v[id - 1];
    if (s->type != want || s->real == 0) return NULL;
    return (void *)(uintptr_t)s->real;
}

static void vkfw_handle_drop(struct vkfw_handles *h, uint64_t id)
{
    if (id == 0 || id > h->count) return;
    h->v[id - 1].type = VKFW_OBJ_NONE;
    h->v[id - 1].real = 0;
    h->v[id - 1].owner = 0;
}

/* Walk up to the owning instance: physical devices are owned by one, devices
 * by a physical device, queues by a device. */
static VkInstance vkfw_instance_of(struct vkfw_conn *c, uint64_t id)
{
    for (int hop = 0; hop < 4; hop++) {
        if (id == 0 || id > c->handles.count) return NULL;
        struct vkfw_handle_slot *s = &c->handles.v[id - 1];
        if (s->type == VKFW_OBJ_INSTANCE) return (VkInstance)(uintptr_t)s->real;
        id = s->owner;
    }
    return NULL;
}

static void vkfw_reply(struct vkfw_request *req, uint16_t cmd, const vkfw_buf *payload)
{
    vkfw_write_frame_fd(req->conn->fd, cmd, req->seq, payload->data, payload->len, req->passfd);
    if (req->passfd >= 0) {
        close(req->passfd);
        req->passfd = -1;
    }
}

/* ---- handlers ----------------------------------------------------------- */

static void h_ping(struct vkfw_request *req, void *unused)
{
    (void)unused;
    vkfw_buf_put_result(req->out, VK_SUCCESS);
}

static void h_enumerate_instance_version(struct vkfw_request *req, void *unused)
{
    (void)unused;
    PFN_vkEnumerateInstanceVersion fn = (PFN_vkEnumerateInstanceVersion)vkfw_global_proc("vkEnumerateInstanceVersion");
    uint32_t version = VK_MAKE_VERSION(1, 0, 0);
    if (fn && fn(&version) != VK_SUCCESS) version = VK_MAKE_VERSION(1, 0, 0);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, version);
}

static void h_enumerate_instance_layers(struct vkfw_request *req, void *unused)
{
    (void)unused;
    PFN_vkEnumerateInstanceLayerProperties fn =
        (PFN_vkEnumerateInstanceLayerProperties)vkfw_global_proc("vkEnumerateInstanceLayerProperties");
    uint32_t count = 0;
    VkResult r = fn ? fn(&count, NULL) : VK_ERROR_INITIALIZATION_FAILED;
    VkLayerProperties *props = NULL;
    if ((r == VK_SUCCESS || r == VK_INCOMPLETE) && count > 0) {
        props = calloc(count, sizeof(*props));
        if (!props) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        r = fn(&count, props);
    }
    vkfw_buf_put_result(req->out, props ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED);
    vkfw_buf_put_u32(req->out, props ? count : 0);
    for (uint32_t i = 0; props && i < count; i++) {
        vkfw_buf_put_str(req->out, props[i].layerName);
        vkfw_buf_put_u32(req->out, props[i].specVersion);
        vkfw_buf_put_u32(req->out, props[i].implementationVersion);
        vkfw_buf_put_str(req->out, props[i].description);
    }
    free(props);
}

static void h_enumerate_instance_extensions(struct vkfw_request *req, void *unused)
{
    (void)unused;
    char *layer = vkfw_cur_str(&req->in);
    PFN_vkEnumerateInstanceExtensionProperties fn =
        (PFN_vkEnumerateInstanceExtensionProperties)vkfw_global_proc("vkEnumerateInstanceExtensionProperties");
    uint32_t count = 0;
    VkResult r = fn ? fn(layer, &count, NULL) : VK_ERROR_INITIALIZATION_FAILED;
    VkExtensionProperties *props = NULL;
    if ((r == VK_SUCCESS || r == VK_INCOMPLETE) && count > 0) {
        props = calloc(count, sizeof(*props));
        if (!props) {
            free(layer);
            vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
            return;
        }
        r = fn(layer, &count, props);
    }
    /* The driver's list, minus anything the ICD serves itself (it will not
     * have listed them anyway), plus the WSI the ICD owns. The ICD appends
     * its own set on the way out; this side only has to not contradict it. */
    uint32_t kept = 0;
    for (uint32_t i = 0; props && i < count; i++)
        if (!vkfw_wsi_owned(props[i].extensionName)) kept++;

    vkfw_buf_put_result(req->out, count == 0 && !props ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS);
    vkfw_buf_put_u32(req->out, kept);
    for (uint32_t i = 0; props && i < count; i++) {
        if (vkfw_wsi_owned(props[i].extensionName)) continue;
        vkfw_buf_put_str(req->out, props[i].extensionName);
        vkfw_buf_put_u32(req->out, props[i].specVersion);
    }
    free(props);
    free(layer);
}

static void h_create_instance(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint32_t api_version = 0, app_version = 0, engine_version = 0;
    uint32_t layer_count = 0, ext_count = 0;
    char *app_name = NULL, *engine_name = NULL;
    char **layers = NULL, **exts = NULL;

    if (!vkfw_cur_u32(&req->in, &api_version)) goto bad;
    app_name = vkfw_cur_str(&req->in);
    if (!vkfw_cur_u32(&req->in, &app_version)) goto bad;
    engine_name = vkfw_cur_str(&req->in);
    if (!vkfw_cur_u32(&req->in, &engine_version)) goto bad;
    if (!vkfw_cur_strlist(&req->in, &layer_count, &layers)) goto bad;
    if (!vkfw_cur_strlist(&req->in, &ext_count, &exts)) goto bad;

    PFN_vkCreateInstance create = (PFN_vkCreateInstance)vkfw_global_proc("vkCreateInstance");
    if (!create) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        goto cleanup;
    }

    /* The driver never hears about an extension the ICD serves: it would
     * refuse it, and the instance would not be created. What is left is what
     * the caller and the driver genuinely agree on. */
    char **kept = ext_count ? calloc(ext_count, sizeof(*kept)) : NULL;
    uint32_t kept_count = 0;
    if (ext_count && !kept) {
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        goto cleanup;
    }
    for (uint32_t i = 0; i < ext_count; i++) {
        if (vkfw_wsi_owned(exts[i])) continue;
        kept[kept_count++] = exts[i];
    }

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = app_name;
    app.applicationVersion = app_version;
    app.pEngineName = engine_name;
    app.engineVersion = engine_version;
    app.apiVersion = api_version;

    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = (app_name || engine_name) ? &app : NULL;
    info.enabledLayerCount = layer_count;
    info.ppEnabledLayerNames = (const char *const *)layers;
    info.enabledExtensionCount = kept_count;
    info.ppEnabledExtensionNames = (const char *const *)kept;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&info, NULL, &instance);
    if (r != VK_SUCCESS) {
        LOGE("vkCreateInstance failed (%d), %u extensions requested, %u passed on",
             r, ext_count, kept_count);
        for (uint32_t i = 0; i < kept_count; i++) LOGE("  %s", kept[i]);
        free(kept);
        vkfw_buf_put_result(req->out, r);
        goto cleanup;
    }
    free(kept);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, vkfw_handle_add(&req->conn->handles, VKFW_OBJ_INSTANCE, instance, 0));
    goto cleanup;

bad:
    vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
cleanup:
    free(app_name);
    free(engine_name);
    vkfw_strlist_free(layer_count, layers);
    vkfw_strlist_free(ext_count, exts);
}

static void h_destroy_instance(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t id = 0;
    if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkInstance instance = (VkInstance)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_INSTANCE);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    if (instance) {
        PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)vkfw_instance_proc(instance, "vkDestroyInstance");
        if (destroy) destroy(instance, NULL);
        vkfw_handle_drop(&req->conn->handles, id);
    }
}

static void h_enumerate_physical_devices(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t instance_id = 0;
    if (!vkfw_cur_u64(&req->in, &instance_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkInstance instance = (VkInstance)vkfw_handle_get(&req->conn->handles, instance_id, VKFW_OBJ_INSTANCE);
    PFN_vkEnumeratePhysicalDevices fn =
        (PFN_vkEnumeratePhysicalDevices)vkfw_instance_proc(instance, "vkEnumeratePhysicalDevices");
    if (!instance || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    uint32_t count = 0;
    VkResult r = fn(instance, &count, NULL);
    VkPhysicalDevice *devices = NULL;
    if ((r == VK_SUCCESS || r == VK_INCOMPLETE) && count > 0) {
        devices = calloc(count, sizeof(*devices));
        if (!devices) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        r = fn(instance, &count, devices);
    }
    vkfw_buf_put_result(req->out, r);
    vkfw_buf_put_u32(req->out, devices ? count : 0);
    for (uint32_t i = 0; devices && i < count; i++)
        vkfw_buf_put_u64(req->out,
                         vkfw_handle_add(&req->conn->handles, VKFW_OBJ_PHYSICAL_DEVICE, devices[i], instance_id));
    free(devices);
}

static void h_enumerate_physical_device_groups(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t instance_id = 0;
    if (!vkfw_cur_u64(&req->in, &instance_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkInstance instance = (VkInstance)vkfw_handle_get(&req->conn->handles, instance_id, VKFW_OBJ_INSTANCE);
    PFN_vkEnumeratePhysicalDeviceGroups fn =
        (PFN_vkEnumeratePhysicalDeviceGroups)vkfw_instance_proc(instance, "vkEnumeratePhysicalDeviceGroups");
    if (!instance || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    uint32_t count = 0;
    VkResult r = fn(instance, &count, NULL);
    VkPhysicalDeviceGroupProperties *groups = NULL;
    if ((r == VK_SUCCESS || r == VK_INCOMPLETE) && count > 0) {
        groups = calloc(count, sizeof(*groups));
        if (!groups) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        for (uint32_t i = 0; i < count; i++) groups[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
        r = fn(instance, &count, groups);
    }

    vkfw_buf_put_result(req->out, groups ? r : VK_SUCCESS);
    vkfw_buf_put_u32(req->out, groups ? count : 0);
    /* Only counts and ids cross: the array holds server-side pointers. */
    for (uint32_t i = 0; groups && i < count; i++) {
        vkfw_buf_put_u32(req->out, groups[i].physicalDeviceCount);
        vkfw_buf_put_u32(req->out, groups[i].subsetAllocation);
        for (uint32_t d = 0; d < groups[i].physicalDeviceCount; d++)
            vkfw_buf_put_u64(req->out, vkfw_handle_add(&req->conn->handles, VKFW_OBJ_PHYSICAL_DEVICE,
                                                       groups[i].physicalDevices[d], instance_id));
    }
    free(groups);
}

static void h_get_physical_device_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceProperties fn =
        (PFN_vkGetPhysicalDeviceProperties)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                              "vkGetPhysicalDeviceProperties");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkPhysicalDeviceProperties props;
    memset(&props, 0, sizeof(props));
    fn(pd, &props);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &props, sizeof(props));
}

/* Both the 1.0 and the *2 variant of a query land here; chained==true means a
 * pNext chain is expected in the request and echoed back in the reply. */
static void h_get_physical_device_properties2(struct vkfw_request *req, void *chained)
{
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    PFN_vkGetPhysicalDeviceProperties2 fn2 =
        (PFN_vkGetPhysicalDeviceProperties2)vkfw_instance_proc(instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkGetPhysicalDeviceProperties fn1 =
        (PFN_vkGetPhysicalDeviceProperties)vkfw_instance_proc(instance, "vkGetPhysicalDeviceProperties");
    if (!pd || !fn1) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    if (chained && !vkfw_chain_parse(&req->in, &chain)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }

    VkPhysicalDeviceProperties props;
    memset(&props, 0, sizeof(props));
    VkResult r;
    if (fn2) {
        VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        p2.pNext = vkfw_chain_head(&chain);
        fn2(pd, &p2); /* VK 1.1 queries return void */
        props = p2.properties;
        r = VK_SUCCESS;
    } else {
        fn1(pd, &props);
        r = VK_SUCCESS;
    }

    vkfw_buf_put_result(req->out, r);
    vkfw_buf_put(req->out, &props, sizeof(props));
    if (chained) {
        vkfw_chain_put(req->out, &chain);
        vkfw_chain_free(&chain);
    }
}

static void h_get_physical_device_features(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceFeatures fn =
        (PFN_vkGetPhysicalDeviceFeatures)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                            "vkGetPhysicalDeviceFeatures");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkPhysicalDeviceFeatures features;
    memset(&features, 0, sizeof(features));
    fn(pd, &features);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &features, sizeof(features));
}

static void h_get_physical_device_features2(struct vkfw_request *req, void *chained)
{
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    PFN_vkGetPhysicalDeviceFeatures2 fn2 =
        (PFN_vkGetPhysicalDeviceFeatures2)vkfw_instance_proc(instance, "vkGetPhysicalDeviceFeatures2");
    PFN_vkGetPhysicalDeviceFeatures fn1 =
        (PFN_vkGetPhysicalDeviceFeatures)vkfw_instance_proc(instance, "vkGetPhysicalDeviceFeatures");
    if (!pd || !fn1) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    if (chained && !vkfw_chain_parse(&req->in, &chain)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }

    VkPhysicalDeviceFeatures features;
    memset(&features, 0, sizeof(features));
    VkResult r;
    if (fn2) {
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = vkfw_chain_head(&chain);
        fn2(pd, &f2); /* VK 1.1 queries return void */
        features = f2.features;
        r = VK_SUCCESS;
    } else {
        fn1(pd, &features);
        r = VK_SUCCESS;
    }

    vkfw_buf_put_result(req->out, r);
    vkfw_buf_put(req->out, &features, sizeof(features));
    if (chained) {
        vkfw_chain_put(req->out, &chain);
        vkfw_chain_free(&chain);
    }
}

static void h_get_queue_family_properties(struct vkfw_request *req, void *chained)
{
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fn1 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkfw_instance_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 fn2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)vkfw_instance_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (!pd || !fn1) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    if (chained && !vkfw_chain_parse(&req->in, &chain)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }

    uint32_t count = 0;
    fn1(pd, &count, NULL);
    VkQueueFamilyProperties *props = NULL;
    if (count > 0) {
        props = calloc(count, sizeof(*props));
        if (!props) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        fn1(pd, &count, props);
    }

    /* The chain describes one family's extras, so it can only be filled from a
     * per-family query. Callers that need it ask for it explicitly. */
    if (chained && fn2 && chain.count > 0 && count > 0) {
        VkQueueFamilyProperties2 q2 = { VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2 };
        q2.pNext = vkfw_chain_head(&chain);
        fn2(pd, 0, &q2);
    }

    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, count);
    for (uint32_t i = 0; i < count; i++) vkfw_buf_put(req->out, &props[i], sizeof(props[i]));
    if (chained) {
        vkfw_chain_put(req->out, &chain);
        vkfw_chain_free(&chain);
    }
    free(props);
}

/* The *2 form takes one pNext chain per family, so each family is queried
 * separately on the server: the extra round trips only cost us on a cold query. */
static void h_get_queue_family_properties2(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties fn1 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkfw_instance_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 fn2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)vkfw_instance_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (!pd || !fn1) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    uint32_t wanted = 0;    if (!vkfw_cur_u32(&req->in, &wanted) || wanted > VKFW_MAX_QUEUE_COUNT) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }

    vkfw_chain *chains = calloc(wanted ? wanted : 1, sizeof(*chains));
    if (!chains) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
    for (uint32_t i = 0; i < wanted; i++) {
        if (!vkfw_chain_parse(&req->in, &chains[i])) {
            for (uint32_t j = 0; j <= i; j++) vkfw_chain_free(&chains[j]);
            free(chains);
            vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
            return;
        }
    }

    uint32_t count = 0;
    fn1(pd, &count, NULL);
    if (count == 0) {
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put_u32(req->out, 0);
        for (uint32_t i = 0; i < wanted; i++) {
            vkfw_chain_put(req->out, &chains[i]);
            vkfw_chain_free(&chains[i]);
        }
        free(chains);
        return;
    }

    /* The *2 query takes the whole array at once, each element carrying its own
     * pNext, so one call fills every family's chain. */
    VkQueueFamilyProperties2 *q2 = calloc(count, sizeof(*q2));
    VkQueueFamilyProperties *props = calloc(count, sizeof(*props));
    if (!q2 || !props) {
        free(q2);
        free(props);
        for (uint32_t i = 0; i < wanted; i++) vkfw_chain_free(&chains[i]);
        free(chains);
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        q2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        if (i < wanted) q2[i].pNext = vkfw_chain_head(&chains[i]);
    }
    fn2(pd, &count, q2);
    for (uint32_t i = 0; i < count; i++) props[i] = q2[i].queueFamilyProperties;

    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, count);
    for (uint32_t i = 0; i < count; i++) vkfw_buf_put(req->out, &props[i], sizeof(props[i]));
    for (uint32_t i = 0; i < wanted; i++) {
        vkfw_chain_put(req->out, &chains[i]);
        vkfw_chain_free(&chains[i]);
    }
    free(chains);
    free(q2);
    free(props);
}

static void h_get_sparse_image_format_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    uint32_t format = 0, type = 0, samples = 0, usage = 0, tiling = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id) || !vkfw_cur_u32(&req->in, &format) ||
        !vkfw_cur_u32(&req->in, &type) || !vkfw_cur_u32(&req->in, &samples) ||
        !vkfw_cur_u32(&req->in, &usage) || !vkfw_cur_u32(&req->in, &tiling)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties fn =
        (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)vkfw_instance_proc(
            vkfw_instance_of(req->conn, pd_id), "vkGetPhysicalDeviceSparseImageFormatProperties");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    uint32_t count = 0;
    fn(pd, (VkFormat)format, (VkImageType)type, (VkSampleCountFlagBits)samples,
       (VkImageUsageFlags)usage, (VkImageTiling)tiling, &count, NULL);

    VkSparseImageFormatProperties *props = NULL;
    if (count > 0) {
        props = calloc(count, sizeof(*props));
        if (!props) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        fn(pd, (VkFormat)format, (VkImageType)type, (VkSampleCountFlagBits)samples,
           (VkImageUsageFlags)usage, (VkImageTiling)tiling, &count, props);
    }

    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, count);
    for (uint32_t i = 0; i < count; i++) vkfw_buf_put(req->out, &props[i], sizeof(props[i]));
    free(props);
}

static void h_get_memory_properties(struct vkfw_request *req, void *chained)
{
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    PFN_vkGetPhysicalDeviceMemoryProperties fn1 =
        (PFN_vkGetPhysicalDeviceMemoryProperties)vkfw_instance_proc(instance, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties2 fn2 =
        (PFN_vkGetPhysicalDeviceMemoryProperties2)vkfw_instance_proc(instance, "vkGetPhysicalDeviceMemoryProperties2");
    if (!pd || !fn1) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    if (chained && !vkfw_chain_parse(&req->in, &chain)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }

    VkPhysicalDeviceMemoryProperties props;
    memset(&props, 0, sizeof(props));
    if (chained && fn2) {
        VkPhysicalDeviceMemoryProperties2 m2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
        m2.pNext = vkfw_chain_head(&chain);
        fn2(pd, &m2);
        props = m2.memoryProperties;
    } else {
        fn1(pd, &props);
    }

    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &props, sizeof(props));
    if (chained) {
        vkfw_chain_put(req->out, &chain);
        vkfw_chain_free(&chain);
    }
}

static void h_get_format_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    uint32_t format = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id) || !vkfw_cur_u32(&req->in, &format)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceFormatProperties fn =
        (PFN_vkGetPhysicalDeviceFormatProperties)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                                    "vkGetPhysicalDeviceFormatProperties");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkFormatProperties props;
    memset(&props, 0, sizeof(props));
    fn(pd, (VkFormat)format, &props);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &props, sizeof(props));
}

static void h_get_image_format_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    uint32_t format = 0, type = 0, tiling = 0, usage = 0, flags = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id) || !vkfw_cur_u32(&req->in, &format) ||
        !vkfw_cur_u32(&req->in, &type) || !vkfw_cur_u32(&req->in, &tiling) ||
        !vkfw_cur_u32(&req->in, &usage) || !vkfw_cur_u32(&req->in, &flags)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceImageFormatProperties fn =
        (PFN_vkGetPhysicalDeviceImageFormatProperties)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                                         "vkGetPhysicalDeviceImageFormatProperties");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkImageFormatProperties props;
    memset(&props, 0, sizeof(props));
    VkResult r = fn(pd, (VkFormat)format, (VkImageType)type, (VkImageTiling)tiling,
                    (VkImageUsageFlags)usage, (VkImageCreateFlags)flags, &props);
    vkfw_buf_put_result(req->out, r);
    vkfw_buf_put(req->out, &props, sizeof(props));
}

static void h_enumerate_device_extensions(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    char *layer = vkfw_cur_str(&req->in);
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkEnumerateDeviceExtensionProperties fn =
        (PFN_vkEnumerateDeviceExtensionProperties)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                                     "vkEnumerateDeviceExtensionProperties");
    if (!pd || !fn) {
        free(layer);
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }

    uint32_t count = 0;
    VkResult r = fn(pd, layer, &count, NULL);
    VkExtensionProperties *props = NULL;
    if ((r == VK_SUCCESS || r == VK_INCOMPLETE) && count > 0) {
        props = calloc(count, sizeof(*props));
        if (!props) { free(layer); vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        r = fn(pd, layer, &count, props);
    }
    vkfw_buf_put_result(req->out, r);
    vkfw_buf_put_u32(req->out, props ? count : 0);
    for (uint32_t i = 0; props && i < count; i++) {
        vkfw_buf_put_str(req->out, props[i].extensionName);
        vkfw_buf_put_u32(req->out, props[i].specVersion);
    }
    free(props);
    free(layer);
}

static void h_enumerate_device_layers(struct vkfw_request *req, void *unused)
{
    (void)unused;
    /* Removed in Vulkan 1.1 and reported as empty by current drivers. */
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    (void)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, 0);
}

static void h_get_physical_device_tool_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceToolProperties fn =
        (PFN_vkGetPhysicalDeviceToolProperties)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id),
                                                                  "vkGetPhysicalDeviceToolProperties");
    if (!pd) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
    if (!fn) {
        /* Nothing to report rather than a failure: this is information, and an
         * absent entry point means the driver predates it. */
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put_u32(req->out, 0);
        return;
    }

    uint32_t count = 0;
    if (fn(pd, &count, NULL) != VK_SUCCESS || count == 0) {
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put_u32(req->out, 0);
        return;
    }
    VkPhysicalDeviceToolProperties *props = calloc(count, sizeof(*props));
    if (!props) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
    for (uint32_t i = 0; i < count; i++) props[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES;
    if (fn(pd, &count, props) != VK_SUCCESS) {
        free(props);
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put_u32(req->out, 0);
        return;
    }

    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, count);
    for (uint32_t i = 0; i < count; i++) vkfw_buf_put(req->out, &props[i], sizeof(props[i]));
    free(props);
}

static void h_get_external_buffer_properties(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    uint32_t flags = 0, usage = 0, handle_type = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id) || !vkfw_cur_u32(&req->in, &flags) ||
        !vkfw_cur_u32(&req->in, &usage) || !vkfw_cur_u32(&req->in, &handle_type)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkGetPhysicalDeviceExternalBufferProperties fn =
        (PFN_vkGetPhysicalDeviceExternalBufferProperties)vkfw_instance_proc(
            vkfw_instance_of(req->conn, pd_id), "vkGetPhysicalDeviceExternalBufferProperties");
    if (!pd || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkPhysicalDeviceExternalBufferInfo info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO };
    info.flags = (VkBufferCreateFlags)flags;
    info.usage = (VkBufferUsageFlags)usage;
    info.handleType = (VkExternalMemoryHandleTypeFlagBits)handle_type;

    VkExternalBufferProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
    fn(pd, &info, &props);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &props, sizeof(props));
}

static void h_get_external_sync_properties(struct vkfw_request *req, void *is_fence)
{
    uint64_t pd_id = 0;
    uint32_t handle_type = 0;
    if (!vkfw_cur_u64(&req->in, &pd_id) || !vkfw_cur_u32(&req->in, &handle_type)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    VkInstance instance = vkfw_instance_of(req->conn, pd_id);
    if (!pd) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    if (is_fence) {
        PFN_vkGetPhysicalDeviceExternalFenceProperties fn =
            (PFN_vkGetPhysicalDeviceExternalFenceProperties)vkfw_instance_proc(
                instance, "vkGetPhysicalDeviceExternalFenceProperties");
        if (!fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
        VkPhysicalDeviceExternalFenceInfo info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO };
        info.handleType = (VkExternalFenceHandleTypeFlagBits)handle_type;
        VkExternalFenceProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES };
        fn(pd, &info, &props);
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put(req->out, &props, sizeof(props));
    } else {
        PFN_vkGetPhysicalDeviceExternalSemaphoreProperties fn =
            (PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)vkfw_instance_proc(
                instance, "vkGetPhysicalDeviceExternalSemaphoreProperties");
        if (!fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
        VkPhysicalDeviceExternalSemaphoreInfo info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO };
        info.handleType = (VkExternalSemaphoreHandleTypeFlagBits)handle_type;
        VkExternalSemaphoreProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES };
        fn(pd, &info, &props);
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        vkfw_buf_put(req->out, &props, sizeof(props));
    }
}

static void h_create_device(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t pd_id = 0;
    uint32_t queue_count = 0, layer_count = 0, ext_count = 0;
    char **layers = NULL, **exts = NULL;
    VkDeviceQueueCreateInfo *queues = NULL;
    float **owned_priorities = NULL;
    vkfw_chain chain;

    memset(&chain, 0, sizeof(chain));

    if (!vkfw_cur_u64(&req->in, &pd_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&req->conn->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    PFN_vkCreateDevice create =
        (PFN_vkCreateDevice)vkfw_instance_proc(vkfw_instance_of(req->conn, pd_id), "vkCreateDevice");
    if (!pd || !create) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkPhysicalDeviceFeatures features;
    memset(&features, 0, sizeof(features));
    if (!vkfw_cur_bytes(&req->in, &features, sizeof(features))) goto bad;
    if (!vkfw_chain_parse(&req->in, &chain)) goto bad;

    if (!vkfw_cur_u32(&req->in, &queue_count) || queue_count > VKFW_MAX_QUEUES) goto bad;
    if (queue_count) {
        queues = calloc(queue_count, sizeof(*queues));
        owned_priorities = calloc(queue_count, sizeof(float *));
        if (!queues || !owned_priorities) goto bad;
    }
    for (uint32_t i = 0; i < queue_count; i++) {
        uint32_t family = 0, count = 0;
        if (!vkfw_cur_u32(&req->in, &family) || !vkfw_cur_u32(&req->in, &count) ||
            count == 0 || count > VKFW_MAX_QUEUE_COUNT) {
            goto bad;
        }
        float *priorities = calloc(count, sizeof(float));
        if (!priorities) goto bad;
        owned_priorities[i] = priorities;
        for (uint32_t q = 0; q < count; q++) {
            uint32_t bits = 0;
            if (!vkfw_cur_u32(&req->in, &bits)) goto bad;
            memcpy(&priorities[q], &bits, sizeof(float));
        }
        queues[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[i].queueFamilyIndex = family;
        queues[i].queueCount = count;
        queues[i].pQueuePriorities = priorities;
    }

    if (!vkfw_cur_strlist(&req->in, &layer_count, &layers)) goto bad;
    if (!vkfw_cur_strlist(&req->in, &ext_count, &exts)) goto bad;

    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.pNext = vkfw_chain_head(&chain);
    info.queueCreateInfoCount = (uint32_t)queue_count;
    info.pQueueCreateInfos = queues;
    info.enabledLayerCount = layer_count;
    info.ppEnabledLayerNames = (const char *const *)layers;
    info.enabledExtensionCount = ext_count;
    info.ppEnabledExtensionNames = (const char *const *)exts;
    info.pEnabledFeatures = &features;

    VkDevice device = VK_NULL_HANDLE;
    VkResult r = create(pd, &info, NULL, &device);
    if (r != VK_SUCCESS) {
        LOGE("vkCreateDevice failed (%d), %u extensions requested", r, ext_count);
        for (uint32_t i = 0; i < ext_count; i++) LOGE("  %s", exts[i]);
    }
    vkfw_buf_put_result(req->out, r);
    if (r == VK_SUCCESS)
        vkfw_buf_put_u64(req->out, vkfw_handle_add(&req->conn->handles, VKFW_OBJ_DEVICE, device, pd_id));

    if (owned_priorities) for (uint32_t i = 0; i < queue_count; i++) free(owned_priorities[i]);
    free(owned_priorities);
    free(queues);
    vkfw_chain_free(&chain);
    vkfw_strlist_free(layer_count, layers);
    vkfw_strlist_free(ext_count, exts);
    return;

bad:
    vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
    if (owned_priorities) for (uint32_t i = 0; i < queue_count; i++) free(owned_priorities[i]);
    free(owned_priorities);
    free(queues);
    vkfw_strlist_free(layer_count, layers);
    vkfw_strlist_free(ext_count, exts);
}

static void h_destroy_device(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t id = 0;
    if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_DEVICE);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    if (device) {
        PFN_vkDestroyDevice destroy =
            (PFN_vkDestroyDevice)vkfw_instance_proc(vkfw_instance_of(req->conn, id), "vkDestroyDevice");
        if (destroy) destroy(device, NULL);
        vkfw_handle_drop(&req->conn->handles, id);
    }
}

static void h_get_device_queue(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    uint32_t family = 0, index = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &family) ||
        !vkfw_cur_u32(&req->in, &index)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkGetDeviceQueue fn =
        (PFN_vkGetDeviceQueue)vkfw_instance_proc(vkfw_instance_of(req->conn, dev_id), "vkGetDeviceQueue");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkQueue queue = VK_NULL_HANDLE;
    fn(device, family, index, &queue);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, vkfw_handle_add(&req->conn->handles, VKFW_OBJ_QUEUE, queue, dev_id));
}

static void h_device_wait_idle(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkDeviceWaitIdle fn =
        (PFN_vkDeviceWaitIdle)vkfw_instance_proc(vkfw_instance_of(req->conn, dev_id), "vkDeviceWaitIdle");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
    vkfw_buf_put_result(req->out, fn(device));
}

/* ---- images ------------------------------------------------------------- */

/*
 * VkImageCreateInfo holds two pointers: the pNext chain and the queue family
 * index array. The ICD sends the struct with both cleared and hands over the
 * array separately, so what the driver sees is a struct whose every pointer
 * was made here.
 */
static VkResult vkfw_image_create_info(struct vkfw_request *req, VkImageCreateInfo *info)
{
    if (!vkfw_cur_bytes(&req->in, info, sizeof(*info))) return VKFW_ERR_PROTOCOL;
    info->pNext = NULL;
    uint32_t count = 0;
    if (!vkfw_cur_u32(&req->in, &count)) return VKFW_ERR_PROTOCOL;
    if (count > 64) return VKFW_ERR_PROTOCOL;
    if (count == 0) {
        info->queueFamilyIndexCount = 0;
        info->pQueueFamilyIndices = NULL;
        return VK_SUCCESS;
    }
    uint32_t *indices = calloc(count, sizeof(*indices));
    if (!indices) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < count; i++) {
        if (!vkfw_cur_u32(&req->in, &indices[i])) {
            free(indices);
            return VKFW_ERR_PROTOCOL;
        }
    }
    info->queueFamilyIndexCount = count;
    info->pQueueFamilyIndices = indices;
    return VK_SUCCESS;
}

static void h_create_image(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkCreateImage fn =
        (PFN_vkCreateImage)vkfw_instance_proc(vkfw_instance_of(req->conn, dev_id), "vkCreateImage");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkImageCreateInfo info;
    VkResult r = vkfw_image_create_info(req, &info);
    if (r != VK_SUCCESS) { vkfw_buf_put_result(req->out, r); return; }

    VkImage image = VK_NULL_HANDLE;
    r = fn(device, &info, NULL, &image);
    free((void *)info.pQueueFamilyIndices);
    if (r != VK_SUCCESS) { vkfw_buf_put_result(req->out, r); return; }

    uint64_t id = vkfw_handle_add(&req->conn->handles, VKFW_OBJ_IMAGE, image, dev_id);
    if (id == 0) {
        PFN_vkDestroyImage destroy =
            (PFN_vkDestroyImage)vkfw_instance_proc(vkfw_instance_of(req->conn, dev_id), "vkDestroyImage");
        if (destroy) destroy(device, image, NULL);
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, id);
}

static void h_destroy_image(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t id = 0;
    if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    if (id == 0 || id > req->conn->handles.count) {
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        return;
    }
    VkImage image = (VkImage)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_IMAGE);
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles,
                                                req->conn->handles.v[id - 1].owner, VKFW_OBJ_DEVICE);
    if (device && image) {
        PFN_vkDestroyImage fn =
            (PFN_vkDestroyImage)vkfw_instance_proc(vkfw_instance_of(req->conn, id), "vkDestroyImage");
        if (fn) fn(device, image, NULL);
    }
    vkfw_handle_drop(&req->conn->handles, id);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
}

static void h_get_image_memory_requirements(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, img_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &img_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkImage image = (VkImage)vkfw_handle_get(&req->conn->handles, img_id, VKFW_OBJ_IMAGE);
    PFN_vkGetImageMemoryRequirements fn =
        (PFN_vkGetImageMemoryRequirements)vkfw_instance_proc(vkfw_instance_of(req->conn, img_id),
                                                             "vkGetImageMemoryRequirements");
    if (!device || !image || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    VkMemoryRequirements needs;
    memset(&needs, 0, sizeof(needs));
    fn(device, image, &needs);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &needs, sizeof(needs));
}

static void h_get_image_sparse_memory_requirements(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, img_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &img_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkImage image = (VkImage)vkfw_handle_get(&req->conn->handles, img_id, VKFW_OBJ_IMAGE);
    PFN_vkGetImageSparseMemoryRequirements fn =
        (PFN_vkGetImageSparseMemoryRequirements)vkfw_instance_proc(
            vkfw_instance_of(req->conn, img_id), "vkGetImageSparseMemoryRequirements");
    if (!device || !image || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    uint32_t count = 0;
    fn(device, image, &count, NULL);
    VkSparseImageMemoryRequirements *reqs = NULL;
    if (count > 0) {
        reqs = calloc(count, sizeof(*reqs));
        if (!reqs) { vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY); return; }
        fn(device, image, &count, reqs);
    }
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u32(req->out, count);
    for (uint32_t i = 0; i < count; i++) vkfw_buf_put(req->out, &reqs[i], sizeof(reqs[i]));
    free(reqs);
}

/* ---- device memory ------------------------------------------------------ */

/*
 * Memory the other process can map, and memory a compositor can import.
 *
 * A pointer the driver hands out here means nothing over there, so mapping is
 * the one thing that cannot be forwarded: there is no value to send back.
 * What does travel is a descriptor. We create the backing store ourselves as
 * a memfd, import it into the driver as a dma-buf, and hand the same
 * descriptor to the client - which maps it for vkMapMemory and passes it on
 * from vkGetMemoryFdKHR to anything that wants to import the image, which is
 * exactly what a Wayland compositor does with a swapchain image.
 *
 * A driver that will not import a dma-buf gets a plain allocation instead.
 * That memory cannot be mapped or exported from the other side, and the caller
 * finds that out from vkMapMemory rather than from vkAllocateMemory: refusing
 * the allocation outright would break every caller that never maps.
 */
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1u
#endif

/*
 * Whether the driver will import a descriptor of this kind at all.
 *
 * Asking first is the difference between a fallback and a hang. lavapipe
 * rejects an opaque fd outright, then spends minutes on a dma-buf it cannot
 * use, and the caller waits with no way to report why; a driver is entitled
 * to take its time over an import it said it supported, not over one it never
 * claimed to.
 */
static bool vkfw_memory_importable(struct vkfw_conn *c, uint64_t dev_id,
                                   VkExternalMemoryHandleTypeFlagBits type)
{
    if (dev_id == 0 || dev_id > c->handles.count) return false;
    uint64_t pd_id = c->handles.v[dev_id - 1].owner;
    VkPhysicalDevice pd = (VkPhysicalDevice)vkfw_handle_get(&c->handles, pd_id, VKFW_OBJ_PHYSICAL_DEVICE);
    if (!pd) return false;

    PFN_vkGetPhysicalDeviceExternalBufferProperties fn =
        (PFN_vkGetPhysicalDeviceExternalBufferProperties)vkfw_instance_proc(
            vkfw_instance_of(c, dev_id), "vkGetPhysicalDeviceExternalBufferProperties");
    if (!fn) return false;

    VkPhysicalDeviceExternalBufferInfo ask = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO
    };
    ask.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ask.handleType = type;
    VkExternalBufferProperties props = { VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES };
    fn(pd, &ask, &props);
    return (props.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

/*
 * Allocating on a thread we can walk away from.
 *
 * A driver is entitled to refuse an import; it is not entitled to hang, but
 * lavapipe does exactly that when handed a dma-buf - and it reports both
 * handle types as importable when asked, so there is no query that rules it
 * out beforehand. The caller upstream of us has a timeout of its own and no
 * way to explain what it is waiting for, so a hang here becomes a dead
 * session rather than an error anyone can act on.
 *
 * So the attempt gets a budget. If the driver comes back within it we join
 * the thread and use the answer; if it does not, we leave the thread where it
 * is - we cannot take back a call we have already made - and fall back to
 * memory the driver will definitely allocate. Once a driver has hung on a
 * handle type we do not offer it that type again.
 */
#define VKFW_IMPORT_BUDGET_SECONDS 10

struct vkfw_alloc_job {
    PFN_vkAllocateMemory fn;
    VkDevice device;
    VkMemoryAllocateInfo info;
    VkImportMemoryFdInfoKHR import;
    void *dedicated;
    VkDeviceMemory memory;
    VkResult result;
    bool done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static void *vkfw_alloc_thread(void *p)
{
    struct vkfw_alloc_job *j = (struct vkfw_alloc_job *)p;
    j->result = j->fn(j->device, &j->info, NULL, &j->memory);
    pthread_mutex_lock(&j->lock);
    j->done = true;
    pthread_cond_signal(&j->cond);
    pthread_mutex_unlock(&j->lock);
    return NULL;
}

/* false means the driver never answered; the job is then deliberately leaked,
 * because the thread inside it still holds a pointer to it. */
static bool vkfw_alloc_bounded(struct vkfw_alloc_job *j)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, vkfw_alloc_thread, j) != 0) return false;

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += VKFW_IMPORT_BUDGET_SECONDS;

    pthread_mutex_lock(&j->lock);
    while (!j->done) {
        if (pthread_cond_timedwait(&j->cond, &j->lock, &deadline) == ETIMEDOUT) break;
    }
    bool done = j->done;
    pthread_mutex_unlock(&j->lock);

    if (done) pthread_join(thread, NULL);
    else pthread_detach(thread);
    return done;
}

static int vkfw_make_memfd(uint64_t size)
{
    int fd = -1;
#if defined(SYS_memfd_create)
    fd = (int)syscall(SYS_memfd_create, "vkforward", MFD_CLOEXEC);
#elif defined(__NR_memfd_create)
    fd = (int)syscall(__NR_memfd_create, "vkforward", MFD_CLOEXEC);
#else
    errno = ENOSYS;
#endif
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void h_allocate_memory(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    VkMemoryAllocateInfo info;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_bytes(&req->in, &info, sizeof(info))) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    info.pNext = NULL;
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkAllocateMemory fn = (PFN_vkAllocateMemory)vkfw_instance_proc(
        vkfw_instance_of(req->conn, dev_id), "vkAllocateMemory");
    if (!device || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }

    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    vkfw_chain_parse(&req->in, &chain);

    uint32_t export_types = 0;
    void *dedicated = NULL;
    for (uint32_t i = 0; i < chain.count; i++) {
        if (chain.v[i].sType == VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO) {
            const VkExportMemoryAllocateInfo *e = (const VkExportMemoryAllocateInfo *)chain.v[i].node;
            export_types |= (uint32_t)e->handleTypes;
        } else if (chain.v[i].sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) {
            dedicated = chain.v[i].node;
        }
    }
    if (dedicated) ((VkBaseInStructure *)dedicated)->pNext = NULL;

    uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t rounded = ((info.allocationSize + page - 1) / page) * page;

    /*
     * Opaque first, dma-buf second. Both mean the same thing here - an fd to
     * the memfd we just made - and opaque is the one every driver that has
     * external memory at all accepts. Importing as a dma-buf asks the driver
     * to treat it as a buffer it may have to negotiate a layout for, which is
     * a bigger ask than "here is the memory, use it".
     *
     * Exporting is a different question and does not change: a memfd is a
     * dma-buf as far as anything importing it is concerned, which is exactly
     * how Mesa hands out its own exportable allocations.
     */
    static const VkExternalMemoryHandleTypeFlagBits kImportAs[] = {
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };

    static bool imports_reported = false;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    int backing = vkfw_make_memfd(rounded);
    if (backing < 0)
        LOGW("no memfd for %llu bytes (%s): this allocation cannot be mapped or exported",
             (unsigned long long)info.allocationSize, strerror(errno));

    bool can_opaque = vkfw_memory_importable(req->conn, dev_id,
                                             VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
    bool can_dma_buf = vkfw_memory_importable(req->conn, dev_id,
                                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
    if (!imports_reported) {
        imports_reported = true;
        LOGI("memory imports: opaque_fd=%d dma_buf=%d", can_opaque, can_dma_buf);
    }

    /* Handle types a driver has hung on; never offered twice. */
    static bool hung_opaque = false, hung_dma_buf = false;

    for (size_t i = 0; backing >= 0 && i < sizeof(kImportAs) / sizeof(kImportAs[0]); i++) {
        if (kImportAs[i] == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT &&
            (!can_opaque || hung_opaque))
            continue;
        if (kImportAs[i] == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
            (!can_dma_buf || hung_dma_buf))
            continue;

        struct vkfw_alloc_job *job = calloc(1, sizeof(*job));
        if (!job) break;
        job->fn = fn;
        job->device = device;
        job->info = info;
        job->import.handleType = kImportAs[i];
        job->import.fd = backing;
        job->import.pNext = dedicated;
        job->info.pNext = &job->import;
        pthread_mutex_init(&job->lock, NULL);
        pthread_cond_init(&job->cond, NULL);

        bool answered = vkfw_alloc_bounded(job);
        if (!answered) {
            if (kImportAs[i] == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) hung_opaque = true;
            else hung_dma_buf = true;
            LOGW("allocating %llu bytes through a %s import did not come back in %d s",
                 (unsigned long long)info.allocationSize,
                 kImportAs[i] == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ? "opaque fd"
                                                                              : "dma-buf",
                 VKFW_IMPORT_BUDGET_SECONDS);
            continue;
        }

        r = job->result;
        memory = job->memory;
        pthread_mutex_destroy(&job->lock);
        pthread_cond_destroy(&job->cond);
        free(job);

        if (r == VK_SUCCESS && memory) break;
        memory = VK_NULL_HANDLE;
        LOGW("allocating %llu bytes through a %s import failed (%d)",
             (unsigned long long)info.allocationSize,
             kImportAs[i] == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ? "opaque fd" : "dma-buf",
             r);
    }
    if (r != VK_SUCCESS || !memory) {
        if (backing >= 0) {
            close(backing);
            backing = -1;
        }
        info.pNext = dedicated;
        r = fn(device, &info, NULL, &memory);
    }
    vkfw_chain_free(&chain);

    if (r != VK_SUCCESS || !memory) {
        if (backing >= 0) close(backing);
        LOGE("vkAllocateMemory failed (%d) for %llu bytes", r,
             (unsigned long long)info.allocationSize);
        vkfw_buf_put_result(req->out, r != VK_SUCCESS ? r : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        return;
    }

    uint64_t id = vkfw_handle_add(&req->conn->handles, VKFW_OBJ_DEVICE_MEMORY, memory, dev_id);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, id);
    vkfw_buf_put_u64(req->out, info.allocationSize);
    /* Sent only when we have one; a plain allocation has nothing to hand over
     * and the client says so when it is asked to map. */
    req->passfd = backing;

    if (export_types != 0 && backing < 0)
        LOGW("memory was asked for as an exportable handle but the driver took none");
}

static void h_free_memory(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, mem_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &mem_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkDeviceMemory memory =
        (VkDeviceMemory)vkfw_handle_get(&req->conn->handles, mem_id, VKFW_OBJ_DEVICE_MEMORY);
    PFN_vkFreeMemory fn = (PFN_vkFreeMemory)vkfw_instance_proc(vkfw_instance_of(req->conn, dev_id),
                                                               "vkFreeMemory");
    if (!device || !memory || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    fn(device, memory, NULL);
    vkfw_handle_drop(&req->conn->handles, mem_id);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
}

/* ---- buffers ------------------------------------------------------------ */

/*
 * A buffer crosses the way an image does - struct with its pointers cleared,
 * queue family array alongside - except that its pNext chain is forwarded.
 * VkExternalMemoryBufferCreateInfo travels in that chain, and it is the only
 * way a caller can say that the allocation behind this buffer has to be
 * exportable; dropping it would quietly turn every exportable buffer into a
 * private one and the dma-buf handed to the compositor would be refused.
 */
static VkResult vkfw_buffer_create_info(struct vkfw_request *req, VkBufferCreateInfo *info,
                                        vkfw_chain *chain)
{
    if (!vkfw_cur_bytes(&req->in, info, sizeof(*info))) return VKFW_ERR_PROTOCOL;
    info->pNext = NULL;
    info->pQueueFamilyIndices = NULL;
    info->queueFamilyIndexCount = 0;

    uint32_t count = 0;
    if (!vkfw_cur_u32(&req->in, &count)) return VKFW_ERR_PROTOCOL;
    if (count > 64) return VKFW_ERR_PROTOCOL;
    if (count == 0) return VK_SUCCESS;

    uint32_t *indices = calloc(count, sizeof(*indices));
    if (!indices) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < count; i++) {
        if (!vkfw_cur_u32(&req->in, &indices[i])) {
            free(indices);
            return VKFW_ERR_PROTOCOL;
        }
    }
    info->queueFamilyIndexCount = count;
    info->pQueueFamilyIndices = indices;
    return VK_SUCCESS;
}

static void h_create_buffer(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkCreateBuffer fn =
        (PFN_vkCreateBuffer)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkCreateBuffer");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkBufferCreateInfo info;
    memset(&info, 0, sizeof(info));
    vkfw_chain chain;
    memset(&chain, 0, sizeof(chain));
    VkResult r = vkfw_buffer_create_info(req, &info, &chain);
    if (r != VK_SUCCESS) { vkfw_buf_put_result(req->out, r); return; }
    vkfw_chain_parse(&req->in, &chain);
    info.pNext = vkfw_chain_head(&chain);

    VkBuffer buffer = VK_NULL_HANDLE;
    r = fn(device, &info, NULL, &buffer);
    free((void *)info.pQueueFamilyIndices);
    vkfw_chain_release(&chain);
    info.pNext = NULL;
    if (r != VK_SUCCESS || !buffer) {
        vkfw_buf_put_result(req->out, r != VK_SUCCESS ? r : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        return;
    }

    uint64_t id = vkfw_handle_add(&req->conn->handles, VKFW_OBJ_BUFFER, buffer, dev_id);
    if (id == 0) {
        PFN_vkDestroyBuffer destroy =
            (PFN_vkDestroyBuffer)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkDestroyBuffer");
        if (destroy) destroy(device, buffer, NULL);
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, id);
}

static void h_destroy_buffer(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t id = 0;
    if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    if (id == 0 || id > req->conn->handles.count) {
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        return;
    }
    VkBuffer buffer = (VkBuffer)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_BUFFER);
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles,
                                                req->conn->handles.v[id - 1].owner, VKFW_OBJ_DEVICE);
    if (device && buffer) {
        PFN_vkDestroyBuffer fn =
            (PFN_vkDestroyBuffer)vkfw_device_proc(vkfw_instance_of(req->conn, id), device, "vkDestroyBuffer");
        if (fn) fn(device, buffer, NULL);
    }
    vkfw_handle_drop(&req->conn->handles, id);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
}

static void h_get_buffer_memory_requirements(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, buf_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &buf_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkBuffer buffer = (VkBuffer)vkfw_handle_get(&req->conn->handles, buf_id, VKFW_OBJ_BUFFER);
    PFN_vkGetBufferMemoryRequirements fn = (PFN_vkGetBufferMemoryRequirements)vkfw_device_proc(vkfw_instance_of(req->conn, buf_id), device, "vkGetBufferMemoryRequirements");
    if (!device || !buffer || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    VkMemoryRequirements needs;
    memset(&needs, 0, sizeof(needs));
    fn(device, buffer, &needs);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put(req->out, &needs, sizeof(needs));
}

/* ---- binding memory ----------------------------------------------------- */

/*
 * Binding takes a memory handle, a range inside it and the object to bind, and
 * that is all it takes: the offset counts from the start of the allocation the
 * server made, which is the same allocation on both sides, so nothing has to be
 * translated. It is also the last step that can fail in a way the caller cannot
 * see coming, so the driver's answer goes back verbatim.
 */
static void h_bind_buffer_memory(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, buf_id = 0, mem_id = 0;
    uint64_t offset = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &buf_id) ||
        !vkfw_cur_u64(&req->in, &mem_id) || !vkfw_cur_u64(&req->in, &offset)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkBuffer buffer = (VkBuffer)vkfw_handle_get(&req->conn->handles, buf_id, VKFW_OBJ_BUFFER);
    VkDeviceMemory memory =
        (VkDeviceMemory)vkfw_handle_get(&req->conn->handles, mem_id, VKFW_OBJ_DEVICE_MEMORY);
    PFN_vkBindBufferMemory fn = (PFN_vkBindBufferMemory)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkBindBufferMemory");
    if (!device || !buffer || !memory || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    vkfw_buf_put_result(req->out, fn(device, buffer, memory, (VkDeviceSize)offset));
}

static void h_bind_image_memory(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, img_id = 0, mem_id = 0;
    uint64_t offset = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &img_id) ||
        !vkfw_cur_u64(&req->in, &mem_id) || !vkfw_cur_u64(&req->in, &offset)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkImage image = (VkImage)vkfw_handle_get(&req->conn->handles, img_id, VKFW_OBJ_IMAGE);
    VkDeviceMemory memory =
        (VkDeviceMemory)vkfw_handle_get(&req->conn->handles, mem_id, VKFW_OBJ_DEVICE_MEMORY);
    PFN_vkBindImageMemory fn = (PFN_vkBindImageMemory)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkBindImageMemory");
    if (!device || !image || !memory || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    vkfw_buf_put_result(req->out, fn(device, image, memory, (VkDeviceSize)offset));
}

/* ---- fences and semaphores ---------------------------------------------- */

/*
 * Synchronisation objects are the first things here with no contents to carry:
 * a fence is a flag and a semaphore is a counter, both living entirely in the
 * driver. So these are handle-only - create returns an id, destroy drops one -
 * and the state queries are answered by the driver each time rather than
 * cached, because the whole point of them is that they change behind our back.
 */
static void h_create_fence(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    uint32_t flags = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &flags)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkCreateFence fn =
        (PFN_vkCreateFence)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkCreateFence");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    info.flags = (VkFenceCreateFlags)flags;
    VkFence fence = VK_NULL_HANDLE;
    VkResult r = fn(device, &info, NULL, &fence);
    if (r != VK_SUCCESS || !fence) {
        vkfw_buf_put_result(req->out, r != VK_SUCCESS ? r : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        return;
    }
    uint64_t id = vkfw_handle_add(&req->conn->handles, VKFW_OBJ_FENCE, fence, dev_id);
    if (id == 0) {
        PFN_vkDestroyFence destroy =
            (PFN_vkDestroyFence)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkDestroyFence");
        if (destroy) destroy(device, fence, NULL);
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, id);
}

static void h_destroy_sync(struct vkfw_request *req, void *unused)
{
    uint32_t type = (uint32_t)(uintptr_t)unused;
    uint64_t id = 0;
    if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    if (id == 0 || id > req->conn->handles.count) {
        vkfw_buf_put_result(req->out, VK_SUCCESS);
        return;
    }
    void *object = vkfw_handle_get(&req->conn->handles, id, type);
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles,
                                                req->conn->handles.v[id - 1].owner, VKFW_OBJ_DEVICE);
    const char *destroy_name = type == VKFW_OBJ_FENCE ? "vkDestroyFence" : "vkDestroySemaphore";
    if (device && object) {
        if (type == VKFW_OBJ_FENCE) {
            PFN_vkDestroyFence fn =
                (PFN_vkDestroyFence)vkfw_instance_proc(vkfw_instance_of(req->conn, id), destroy_name);
            if (fn) fn(device, (VkFence)object, NULL);
        } else {
            PFN_vkDestroySemaphore fn = (PFN_vkDestroySemaphore)vkfw_instance_proc(
                vkfw_instance_of(req->conn, id), destroy_name);
            if (fn) fn(device, (VkSemaphore)object, NULL);
        }
    }
    vkfw_handle_drop(&req->conn->handles, id);
    vkfw_buf_put_result(req->out, VK_SUCCESS);
}

static void h_reset_fences(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0;
    uint32_t count = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &count)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    if (count > 64) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkResetFences fn =
        (PFN_vkResetFences)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkResetFences");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkFence fences[64];
    for (uint32_t i = 0; i < count; i++) {
        uint64_t id = 0;
        if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
        fences[i] = (VkFence)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_FENCE);
        if (!fences[i]) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
    }
    vkfw_buf_put_result(req->out, fn(device, count, fences));
}

static void h_get_fence_status(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, fence_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &fence_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkFence fence = (VkFence)vkfw_handle_get(&req->conn->handles, fence_id, VKFW_OBJ_FENCE);
    PFN_vkGetFenceStatus fn = (PFN_vkGetFenceStatus)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkGetFenceStatus");
    if (!device || !fence || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    vkfw_buf_put_result(req->out, fn(device, fence));
}

static void h_wait_for_fences(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, timeout = 0;
    uint32_t count = 0, wait_all = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &count) ||
        !vkfw_cur_u32(&req->in, &wait_all) || !vkfw_cur_u64(&req->in, &timeout)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    if (count > 64) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkWaitForFences fn = (PFN_vkWaitForFences)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkWaitForFences");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkFence fences[64];
    for (uint32_t i = 0; i < count; i++) {
        uint64_t id = 0;
        if (!vkfw_cur_u64(&req->in, &id)) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
        fences[i] = (VkFence)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_FENCE);
        if (!fences[i]) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
    }
    /*
     * This is the one call here that blocks, and it blocks on the driver's
     * timeline rather than on anything of ours. A caller asking to wait
     * forever is asking to be stuck here, which is what it asked for; the
     * connection is per-client and a stuck client does not hold up the others.
     */
    vkfw_buf_put_result(req->out, fn(device, count, fences, wait_all != 0, timeout));
}

static void h_create_semaphore(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, initial = 0;
    uint32_t flags = 0, timeline = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &flags) ||
        !vkfw_cur_u32(&req->in, &timeline) || !vkfw_cur_u64(&req->in, &initial)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkCreateSemaphore fn = (PFN_vkCreateSemaphore)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkCreateSemaphore");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    info.flags = (VkSemaphoreCreateFlags)flags;
    VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    /* A timeline semaphore without its initial value is a binary semaphore
     * with a confusing name, so the type node rides along when one was asked
     * for. Either way the driver tells us if it does not want it. */
    if (timeline) {
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = initial;
        info.pNext = &type_info;
    }

    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkResult r = fn(device, &info, NULL, &semaphore);
    if (r != VK_SUCCESS || !semaphore) {
        vkfw_buf_put_result(req->out, r != VK_SUCCESS ? r : VK_ERROR_OUT_OF_DEVICE_MEMORY);
        return;
    }
    uint64_t id = vkfw_handle_add(&req->conn->handles, VKFW_OBJ_SEMAPHORE, semaphore, dev_id);
    if (id == 0) {
        PFN_vkDestroySemaphore destroy = (PFN_vkDestroySemaphore)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkDestroySemaphore");
        if (destroy) destroy(device, semaphore, NULL);
        vkfw_buf_put_result(req->out, VK_ERROR_OUT_OF_HOST_MEMORY);
        return;
    }
    vkfw_buf_put_result(req->out, VK_SUCCESS);
    vkfw_buf_put_u64(req->out, id);
}

static void h_get_semaphore_counter_value(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, sem_id = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &sem_id)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkSemaphore semaphore =
        (VkSemaphore)vkfw_handle_get(&req->conn->handles, sem_id, VKFW_OBJ_SEMAPHORE);
    PFN_vkGetSemaphoreCounterValue fn = (PFN_vkGetSemaphoreCounterValue)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkGetSemaphoreCounterValue");
    if (!device || !semaphore || !fn) {
        LOGW("semaphore counter: device=%d semaphore=%d entry=%d (id %llu of %u slots)",
             device != NULL, semaphore != NULL, fn != NULL, (unsigned long long)sem_id,
             req->conn->handles.count);
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    uint64_t value = 0;
    VkResult r = fn(device, semaphore, &value);
    vkfw_buf_put_result(req->out, r);
    if (r == VK_SUCCESS) vkfw_buf_put_u64(req->out, value);
}

static void h_signal_semaphore(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, sem_id = 0, value = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u64(&req->in, &sem_id) ||
        !vkfw_cur_u64(&req->in, &value)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    VkSemaphore semaphore =
        (VkSemaphore)vkfw_handle_get(&req->conn->handles, sem_id, VKFW_OBJ_SEMAPHORE);
    PFN_vkSignalSemaphore fn = (PFN_vkSignalSemaphore)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkSignalSemaphore");
    if (!device || !semaphore || !fn) {
        vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED);
        return;
    }
    VkSemaphoreSignalInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
    info.semaphore = semaphore;
    info.value = value;
    vkfw_buf_put_result(req->out, fn(device, &info));
}

static void h_wait_semaphores(struct vkfw_request *req, void *unused)
{
    (void)unused;
    uint64_t dev_id = 0, timeout = 0;
    uint32_t count = 0;
    if (!vkfw_cur_u64(&req->in, &dev_id) || !vkfw_cur_u32(&req->in, &count) ||
        !vkfw_cur_u64(&req->in, &timeout)) {
        vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
        return;
    }
    if (count > 64) { vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL); return; }
    VkDevice device = (VkDevice)vkfw_handle_get(&req->conn->handles, dev_id, VKFW_OBJ_DEVICE);
    PFN_vkWaitSemaphores fn = (PFN_vkWaitSemaphores)vkfw_device_proc(vkfw_instance_of(req->conn, dev_id), device, "vkWaitSemaphores");
    if (!device || !fn) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }

    VkSemaphore semaphores[64];
    uint64_t values[64];
    for (uint32_t i = 0; i < count; i++) {
        uint64_t id = 0, value = 0;
        if (!vkfw_cur_u64(&req->in, &id) || !vkfw_cur_u64(&req->in, &value)) {
            vkfw_buf_put_result(req->out, VKFW_ERR_PROTOCOL);
            return;
        }
        semaphores[i] = (VkSemaphore)vkfw_handle_get(&req->conn->handles, id, VKFW_OBJ_SEMAPHORE);
        values[i] = value;
        if (!semaphores[i]) { vkfw_buf_put_result(req->out, VK_ERROR_INITIALIZATION_FAILED); return; }
    }
    VkSemaphoreWaitInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    info.semaphoreCount = count;
    info.pSemaphores = semaphores;
    info.pValues = values;
    vkfw_buf_put_result(req->out, fn(device, &info, timeout));
}

/* ---- dispatch ----------------------------------------------------------- */

struct vkfw_entry {
    uint16_t cmd;
    void (*fn)(struct vkfw_request *req, void *ctx);
    void *ctx;
};

static const struct vkfw_entry kDispatch[] = {
    { VKFW_CMD_PING, h_ping, NULL },

    { VKFW_CMD_ENUMERATE_INSTANCE_VERSION, h_enumerate_instance_version, NULL },
    { VKFW_CMD_ENUMERATE_INSTANCE_LAYERS, h_enumerate_instance_layers, NULL },
    { VKFW_CMD_ENUMERATE_INSTANCE_EXTENSIONS, h_enumerate_instance_extensions, NULL },
    { VKFW_CMD_CREATE_INSTANCE, h_create_instance, NULL },
    { VKFW_CMD_DESTROY_INSTANCE, h_destroy_instance, NULL },

    { VKFW_CMD_ENUMERATE_PHYSICAL_DEVICES, h_enumerate_physical_devices, NULL },
    { VKFW_CMD_ENUMERATE_PHYSICAL_DEVICE_GROUPS, h_enumerate_physical_device_groups, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES, h_get_physical_device_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_PROPERTIES2, h_get_physical_device_properties2, (void *)1 },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES, h_get_physical_device_features, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_FEATURES2, h_get_physical_device_features2, (void *)1 },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES, h_get_queue_family_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_PROPERTIES2, h_get_queue_family_properties2, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_PROPERTIES, h_get_sparse_image_format_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES, h_get_memory_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_MEMORY_PROPERTIES2, h_get_memory_properties, (void *)1 },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_FORMAT_PROPERTIES, h_get_format_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES, h_get_image_format_properties, NULL },
    { VKFW_CMD_ENUMERATE_DEVICE_EXTENSIONS, h_enumerate_device_extensions, NULL },
    { VKFW_CMD_ENUMERATE_DEVICE_LAYERS, h_enumerate_device_layers, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_BUFFER_PROPERTIES, h_get_external_buffer_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_FENCE_PROPERTIES, h_get_external_sync_properties, (void *)1 },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_PROPERTIES, h_get_external_sync_properties, NULL },
    { VKFW_CMD_GET_PHYSICAL_DEVICE_TOOL_PROPERTIES, h_get_physical_device_tool_properties, NULL },

    { VKFW_CMD_CREATE_DEVICE, h_create_device, NULL },
    { VKFW_CMD_DESTROY_DEVICE, h_destroy_device, NULL },
    { VKFW_CMD_GET_DEVICE_QUEUE, h_get_device_queue, NULL },
    { VKFW_CMD_DEVICE_WAIT_IDLE, h_device_wait_idle, NULL },

    { VKFW_CMD_CREATE_IMAGE, h_create_image, NULL },
    { VKFW_CMD_DESTROY_IMAGE, h_destroy_image, NULL },
    { VKFW_CMD_GET_IMAGE_MEMORY_REQUIREMENTS, h_get_image_memory_requirements, NULL },
    { VKFW_CMD_GET_IMAGE_SPARSE_MEMORY_REQUIREMENTS, h_get_image_sparse_memory_requirements, NULL },

    { VKFW_CMD_ALLOCATE_MEMORY, h_allocate_memory, NULL },
    { VKFW_CMD_FREE_MEMORY, h_free_memory, NULL },

    { VKFW_CMD_CREATE_BUFFER, h_create_buffer, NULL },
    { VKFW_CMD_DESTROY_BUFFER, h_destroy_buffer, NULL },
    { VKFW_CMD_GET_BUFFER_MEMORY_REQUIREMENTS, h_get_buffer_memory_requirements, NULL },
    { VKFW_CMD_BIND_BUFFER_MEMORY, h_bind_buffer_memory, NULL },
    { VKFW_CMD_BIND_IMAGE_MEMORY, h_bind_image_memory, NULL },

    { VKFW_CMD_CREATE_FENCE, h_create_fence, NULL },
    { VKFW_CMD_DESTROY_FENCE, h_destroy_sync, (void *)(uintptr_t)VKFW_OBJ_FENCE },
    { VKFW_CMD_RESET_FENCES, h_reset_fences, NULL },
    { VKFW_CMD_GET_FENCE_STATUS, h_get_fence_status, NULL },
    { VKFW_CMD_WAIT_FOR_FENCES, h_wait_for_fences, NULL },
    { VKFW_CMD_CREATE_SEMAPHORE, h_create_semaphore, NULL },
    { VKFW_CMD_DESTROY_SEMAPHORE, h_destroy_sync, (void *)(uintptr_t)VKFW_OBJ_SEMAPHORE },
    { VKFW_CMD_GET_SEMAPHORE_COUNTER_VALUE, h_get_semaphore_counter_value, NULL },
    { VKFW_CMD_SIGNAL_SEMAPHORE, h_signal_semaphore, NULL },
    { VKFW_CMD_WAIT_SEMAPHORES, h_wait_semaphores, NULL },
};

void vkfw_serve_connection(int fd)
{
    struct vkfw_conn conn;
    memset(&conn, 0, sizeof(conn));
    conn.fd = fd;

    struct vkfw_header hdr;
    vkfw_buf payload;
    vkfw_buf_init(&payload);

    LOGD("connection accepted");
    while (vkfw_read_frame(fd, &hdr, &payload)) {
        vkfw_buf reply;
        vkfw_buf_init(&reply);
        struct vkfw_request req = { .conn = &conn, .seq = hdr.seq, .out = &reply, .passfd = -1 };
        vkfw_cur_init(&req.in, payload.data, payload.len);

        const struct vkfw_entry *entry = NULL;
        for (size_t i = 0; i < sizeof(kDispatch) / sizeof(*kDispatch); i++) {
            if (kDispatch[i].cmd == hdr.cmd) { entry = &kDispatch[i]; break; }
        }
        if (entry) {
            entry->fn(&req, entry->ctx);
        } else {
            /* Declared but not implemented yet (the M2/M3 command set). */
            LOGD("unhandled cmd %u", hdr.cmd);
            vkfw_buf_put_result(&reply, VK_ERROR_FEATURE_NOT_PRESENT);
        }
        vkfw_reply(&req, hdr.cmd, &reply);
        vkfw_buf_free(&reply);
    }

    free(conn.handles.v);
    vkfw_buf_free(&payload);
    close(fd);
    LOGD("connection closed");
}

struct vkfw_conn_arg {
    int fd;
};

static void *vkfw_conn_thread(void *p)
{
    struct vkfw_conn_arg *arg = (struct vkfw_conn_arg *)p;
    int fd = arg->fd;
    free(arg);
    vkfw_serve_connection(fd);
    return NULL;
}

/*
 * One thread per connection, not one connection at a time.
 *
 * Serving a connection inline looks fine for as long as only one process ever
 * talks to the driver, which is all a single test does. On a device it is not:
 * a client opens the socket when it first asks a question and holds it for as
 * long as it lives, so the second process to come along waits behind the first
 * one forever with nothing to show for it. Steam asks about the driver from a
 * helper process while the client holds a connection of its own, and what that
 * looks like from the outside is not an error - it is a timeout, then another,
 * and then the app giving up.
 *
 * The ICD serialises its own threads on one socket behind a lock, so these
 * connections are separate clients, not one client's threads; the driver is
 * thread-safe for separate objects, and each connection keeps its own handles.
 */
static void vkfw_serve_detached(int fd)
{
    struct vkfw_conn_arg *arg = malloc(sizeof(*arg));
    if (!arg) {
        LOGE("out of memory serving a connection");
        close(fd);
        return;
    }
    arg->fd = fd;

    pthread_t thread;
    if (pthread_create(&thread, NULL, vkfw_conn_thread, arg) != 0) {
        LOGE("pthread_create failed, dropping this connection");
        free(arg);
        close(fd);
        return;
    }
    pthread_detach(thread);
}

int vkfw_server_main(int listen_fd)
{
    for (;;) {
        int client = accept(listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        vkfw_serve_detached(client);
    }
}
