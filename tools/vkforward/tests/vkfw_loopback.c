/*
 * M1 loopback test: runs the server core and the ICD in one process, talking
 * over a real Unix socket, and prints what the ICD reports.
 *
 * The point is to prove the plumbing end to end on whatever machine it runs
 * on: on CI that is lavapipe, on the tablet it is the real Mali driver. Both
 * halves live in this process, so a failure here means the forwarding layer is
 * broken rather than a missing driver.
 *
 * Usage: vkfw_loopback [endpoint]   endpoint defaults to a per-pid file socket
 * Exit code 0 means at least one physical device was enumerated with real,
 * forwarded properties.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include "vkfw_protocol.h"
#include "vkfw_server_core.h"
#include "vkfw_transport.h"

/* The ICD's loader entry point; driven directly so no loader is needed. */
extern void *vk_icdGetInstanceProcAddr(void *instance, const char *name);

#define FAIL(msg)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "FAIL: %s\n", (msg));                                                  \
        return 1;                                                                              \
    } while (0)

static void *accept_thread(void *p)
{
    int listen_fd = *(int *)p;
    for (;;) {
        int client = accept(listen_fd, NULL, NULL);
        if (client < 0) return NULL;
        vkfw_serve_connection(client);
    }
}

static void dump_device_extensions(PFN_vkEnumerateDeviceExtensionProperties fn, VkPhysicalDevice pd)
{
    uint32_t count = 0;
    if (fn(pd, NULL, &count, NULL) != VK_SUCCESS || count == 0) {
        printf("  extensions: none reported\n");
        return;
    }
    VkExtensionProperties *exts = calloc(count, sizeof(*exts));
    if (!exts) return;
    fn(pd, NULL, &count, exts);
    printf("  extensions: %u total\n", count);
    for (uint32_t i = 0; i < count && i < 6; i++)
        printf("    %s (spec %u)\n", exts[i].extensionName, exts[i].specVersion);
    free(exts);
}

int main(int argc, char **argv)
{
    const char *driver = getenv("VKFW_DRIVER");
    char default_endpoint[256];
    snprintf(default_endpoint, sizeof(default_endpoint), "%s/vkfw-loopback-%ld.sock",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    const char *endpoint = (argc > 1) ? argv[1] : default_endpoint;
    bool abstract = (endpoint[0] != '/');

    if (vkfw_server_init(driver) != 0) FAIL("cannot load a Vulkan library (set VKFW_DRIVER?)");

    int listen_fd = vkfw_listen(endpoint);
    if (listen_fd < 0) FAIL("cannot bind the forwarding socket");

    char socket_ref[280];
    if (abstract) snprintf(socket_ref, sizeof(socket_ref), "@%s", endpoint);
    else snprintf(socket_ref, sizeof(socket_ref), "%s", endpoint);
    setenv(VKFW_SOCKET_ENV, socket_ref, 1);

    pthread_t server;
    pthread_create(&server, NULL, accept_thread, &listen_fd);
    pthread_detach(server);

    PFN_vkCreateInstance create = (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance");
    PFN_vkGetInstanceProcAddr gipa =
        (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr(NULL, "vkGetInstanceProcAddr");
    PFN_vkEnumerateInstanceVersion version_fn =
        (PFN_vkEnumerateInstanceVersion)vk_icdGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    if (!create || !gipa) FAIL("ICD does not expose its core entry points");

    uint32_t instance_api = VK_MAKE_VERSION(1, 0, 0);
    if (version_fn) version_fn(&instance_api);
    printf("instance api version: %u.%u.%u\n", VK_API_VERSION_MAJOR(instance_api),
           VK_API_VERSION_MINOR(instance_api), VK_API_VERSION_PATCH(instance_api));

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vkforward-loopback";
    app.apiVersion = VK_MAKE_VERSION(1, 1, 0);
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&ici, NULL, &instance);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "FAIL: vkCreateInstance returned %d\n", r);
        return 1;
    }

    PFN_vkEnumeratePhysicalDevices enum_pd =
        (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    if (!enum_pd) FAIL("no vkEnumeratePhysicalDevices");

    uint32_t pd_count = 0;
    if (enum_pd(instance, &pd_count, NULL) != VK_SUCCESS || pd_count == 0)
        FAIL("no physical device visible through the forwarder");
    VkPhysicalDevice *devices = calloc(pd_count, sizeof(*devices));
    enum_pd(instance, &pd_count, devices);
    printf("physical devices: %u\n", pd_count);

    PFN_vkGetPhysicalDeviceProperties props_fn =
        (PFN_vkGetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceProperties2 props2_fn =
        (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkGetPhysicalDeviceFeatures features_fn =
        (PFN_vkGetPhysicalDeviceFeatures)gipa(instance, "vkGetPhysicalDeviceFeatures");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties qf_fn =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkGetPhysicalDeviceMemoryProperties mem_fn =
        (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
    PFN_vkEnumerateDeviceExtensionProperties ext_fn =
        (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");

    int failures = 0;
    uint32_t graphics_family = 0;
    bool have_graphics = false;

    for (uint32_t i = 0; i < pd_count; i++) {
        VkPhysicalDeviceProperties props;
        memset(&props, 0, sizeof(props));
        props_fn(devices[i], &props);
        printf("\ndevice %u: %s\n", i, props.deviceName);
        printf("  vendor=0x%04x device=0x%08x type=%d\n", props.vendorID, props.deviceID, props.deviceType);
        printf("  api=%u.%u.%u driver=%u.%u.%u\n", VK_API_VERSION_MAJOR(props.apiVersion),
               VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
               props.driverVersion >> 22, (props.driverVersion >> 12) & 0x3ff, props.driverVersion & 0xfff);
        printf("  limits: maxImageDimension2D=%u maxMemoryAllocationCount=%u maxSampledImages=%u\n",
               props.limits.maxImageDimension2D, props.limits.maxMemoryAllocationCount,
               props.limits.maxPerStageDescriptorSampledImages);
        if (props.deviceName[0] == '\0') failures++;

        if (props2_fn) {
            VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
            VkPhysicalDeviceIDProperties id = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
            driver.pNext = &id;
            VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
            p2.pNext = &driver;
            props2_fn(devices[i], &p2);
            printf("  chained Properties2: driver=%s (%s) maxImageDimension2D=%u\n", driver.driverName,
                   driver.driverInfo, p2.properties.limits.maxImageDimension2D);
            if (p2.properties.limits.maxImageDimension2D == 0) failures++;
        }

        VkPhysicalDeviceFeatures features;
        memset(&features, 0, sizeof(features));
        features_fn(devices[i], &features);
        printf("  features: geometryShader=%d tessellationShader=%d shaderInt64=%d\n",
               features.geometryShader, features.tessellationShader, features.shaderInt64);

        uint32_t qf_count = 0;
        qf_fn(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties *qf = calloc(qf_count ? qf_count : 1, sizeof(*qf));
        qf_fn(devices[i], &qf_count, qf);
        printf("  queue families: %u\n", qf_count);
        for (uint32_t q = 0; q < qf_count; q++) {
            printf("    family %u count=%u flags=0x%x timestampBits=%u\n", q, qf[q].queueCount,
                   qf[q].queueFlags, qf[q].timestampValidBits);
            if ((qf[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !have_graphics) {
                graphics_family = q;
                have_graphics = true;
            }
        }

        VkPhysicalDeviceMemoryProperties mem;
        memset(&mem, 0, sizeof(mem));
        mem_fn(devices[i], &mem);
        unsigned long long heap = 0;
        for (uint32_t h = 0; h < mem.memoryHeapCount; h++)
            if ((mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && mem.memoryHeaps[h].size > heap)
                heap = mem.memoryHeaps[h].size;
        printf("  heaps=%u memoryTypes=%u deviceLocalHeapMiB=%llu\n", mem.memoryHeapCount, mem.memoryTypeCount,
               heap >> 20);
        if (mem.memoryTypeCount == 0) failures++;

        dump_device_extensions(ext_fn, devices[i]);
        free(qf);
    }

    /* Device creation + queue retrieval: the last M1 step before rendering. */
    PFN_vkCreateDevice create_dev = (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
    if (create_dev && have_graphics) {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qi.queueFamilyIndex = graphics_family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qi;

        VkDevice device = VK_NULL_HANDLE;
        r = create_dev(devices[0], &dci, NULL, &device);
        printf("\nvkCreateDevice via forwarder: %d\n", r);
        if (r != VK_SUCCESS) {
            failures++;
        } else {
            PFN_vkGetDeviceQueue get_queue =
                (PFN_vkGetDeviceQueue)gipa(instance, "vkGetDeviceQueue");
            VkQueue queue = VK_NULL_HANDLE;
            if (get_queue) get_queue(device, graphics_family, 0, &queue);
            printf("vkGetDeviceQueue: %s\n", queue ? "OK" : "FAILED");
            if (!queue) failures++;
            PFN_vkDestroyDevice destroy_dev = (PFN_vkDestroyDevice)gipa(instance, "vkDestroyDevice");
            if (destroy_dev) destroy_dev(device, NULL);
        }
    }

    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
    if (destroy) destroy(instance, NULL);
    free(devices);

    printf("\n%s\n", failures == 0 ? "M1 loopback: PASS" : "M1 loopback: FAIL");
    return failures == 0 ? 0 : 1;
}
