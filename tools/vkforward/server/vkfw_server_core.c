#include "vkfw_server_core.h"
#include "vkfw_transport.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#ifdef __ANDROID__
#include <android/log.h>
#define VKFW_LOG_TAG "vkforward"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, VKFW_LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...)                                                                              \
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

/* ---- per-connection state ----------------------------------------------- */

enum vkfw_handle_type {
    VKFW_OBJ_NONE = 0,
    VKFW_OBJ_INSTANCE,
    VKFW_OBJ_PHYSICAL_DEVICE,
    VKFW_OBJ_DEVICE,
    VKFW_OBJ_QUEUE,
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
    vkfw_write_frame(req->conn->fd, cmd, req->seq, payload->data, payload->len);
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
    vkfw_buf_put_result(req->out, props ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED);
    vkfw_buf_put_u32(req->out, props ? count : 0);
    for (uint32_t i = 0; props && i < count; i++) {
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
    info.enabledExtensionCount = ext_count;
    info.ppEnabledExtensionNames = (const char *const *)exts;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&info, NULL, &instance);
    if (r != VK_SUCCESS) {
        LOGE("vkCreateInstance failed (%d), %u extensions requested", r, ext_count);
        for (uint32_t i = 0; i < ext_count; i++) LOGE("  %s", exts[i]);
        vkfw_buf_put_result(req->out, r);
        goto cleanup;
    }
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

    { VKFW_CMD_CREATE_DEVICE, h_create_device, NULL },
    { VKFW_CMD_DESTROY_DEVICE, h_destroy_device, NULL },
    { VKFW_CMD_GET_DEVICE_QUEUE, h_get_device_queue, NULL },
    { VKFW_CMD_DEVICE_WAIT_IDLE, h_device_wait_idle, NULL },
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
        struct vkfw_request req = { .conn = &conn, .seq = hdr.seq, .out = &reply };
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

int vkfw_server_main(int listen_fd)
{
    for (;;) {
        int client = accept(listen_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        vkfw_serve_connection(client);
    }
}
