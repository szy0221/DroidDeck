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
#include <sys/time.h>
#include <sys/un.h>
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

/*
 * The real server loop, not a copy of it.
 *
 * The loopback used to have its own accept loop so it could stay inside one
 * process, and a private copy is exactly the kind of thing that quietly stops
 * testing what ships: it served one connection at a time long after the
 * server stopped doing that, and no test could tell the difference. Running
 * vkfw_server_main means the loopback exercises the accept path a device
 * actually uses.
 */
static void *accept_thread(void *p)
{
    int listen_fd = *(int *)p;
    vkfw_server_main(listen_fd);
    return NULL;
}

static void dump_device_extensions(PFN_vkEnumerateDeviceExtensionProperties fn, VkPhysicalDevice pd);

/* Images are the first non-dispatchable objects to cross the socket, and the
 * first whose create info carries pointers of its own. Creating one, asking
 * what it needs and destroying it is the shape every later resource takes. */
static int image_round_trip(PFN_vkGetInstanceProcAddr gipa, VkDevice device)
{
    PFN_vkCreateImage create_image = (PFN_vkCreateImage)gipa(NULL, "vkCreateImage");
    PFN_vkDestroyImage destroy_image = (PFN_vkDestroyImage)gipa(NULL, "vkDestroyImage");
    PFN_vkGetImageMemoryRequirements needs =
        (PFN_vkGetImageMemoryRequirements)gipa(NULL, "vkGetImageMemoryRequirements");
    PFN_vkGetImageSparseMemoryRequirements sparse =
        (PFN_vkGetImageSparseMemoryRequirements)gipa(NULL, "vkGetImageSparseMemoryRequirements");
    if (!create_image || !destroy_image || !needs) return 1;

    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent.width = 64;
    ici.extent.height = 64;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    VkResult r = create_image(device, &ici, NULL, &image);
    printf("[images] vkCreateImage 64x64 R8G8B8A8: %d\n", r);
    if (r != VK_SUCCESS || !image) return 1;

    int failures = 0;
    VkMemoryRequirements mem;
    memset(&mem, 0, sizeof(mem));
    needs(device, image, &mem);
    printf("[images] memory: size=%llu alignment=%llu types=0x%x\n",
           (unsigned long long)mem.size, (unsigned long long)mem.alignment, mem.memoryTypeBits);
    if (mem.size == 0 || mem.alignment == 0 || mem.memoryTypeBits == 0) failures++;

    if (sparse) {
        uint32_t count = 0;
        sparse(device, image, &count, NULL);
        VkSparseImageMemoryRequirements *reqs = count ? calloc(count, sizeof(*reqs)) : NULL;
        if (count) {
            sparse(device, image, &count, reqs);
            printf("[images] sparse requirements: %u\n", count);
        }
        free(reqs);
    }

    destroy_image(device, image, NULL);
    printf("[images] destroyed\n");
    return failures;
}

/*
 * Memory is the first thing that has to cross the socket as more than an id.
 *
 * The caller is handed a pointer it writes through and a descriptor it hands
 * to a compositor, and both have to be real: a mapping the driver cannot see
 * is worse than none, because the data simply goes nowhere, and a descriptor
 * that is not a dma-buf gets refused by the thing that was going to import it.
 * Writing a pattern and reading it back proves the first; asking how big the
 * descriptor is proves the second is at least a file the right size.
 */
static int memory_round_trip(PFN_vkGetInstanceProcAddr gipa, VkDevice device)
{
    PFN_vkAllocateMemory alloc_memory = (PFN_vkAllocateMemory)gipa(NULL, "vkAllocateMemory");
    PFN_vkFreeMemory free_memory = (PFN_vkFreeMemory)gipa(NULL, "vkFreeMemory");
    PFN_vkMapMemory map_memory = (PFN_vkMapMemory)gipa(NULL, "vkMapMemory");
    PFN_vkUnmapMemory unmap_memory = (PFN_vkUnmapMemory)gipa(NULL, "vkUnmapMemory");
    PFN_vkGetMemoryFdKHR get_memory_fd = (PFN_vkGetMemoryFdKHR)gipa(NULL, "vkGetMemoryFdKHR");
    if (!alloc_memory || !free_memory || !map_memory || !unmap_memory || !get_memory_fd) {
        printf("[memory] entry points: missing\n");
        return 1;
    }

    int failures = 0;
    const uint64_t bytes = 65536;

    VkExportMemoryAllocateInfo export_info = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.pNext = &export_info;
    ai.allocationSize = bytes;
    ai.memoryTypeIndex = 0;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult r = alloc_memory(device, &ai, NULL, &memory);
    printf("[memory] vkAllocateMemory %llu bytes, exportable: %d\n", (unsigned long long)bytes, r);
    if (r != VK_SUCCESS || !memory) return 1;

    void *at = NULL;
    r = map_memory(device, memory, 0, 4096, 0, &at);
    printf("[memory] vkMapMemory: %d (%s)\n", r, at ? "mapped" : "no pointer");
    if (r != VK_SUCCESS || !at) {
        failures++;
    } else {
        unsigned char *p = (unsigned char *)at;
        for (int i = 0; i < 256; i++) p[i] = (unsigned char)(i * 7 + 1);
        int bad = 0;
        for (int i = 0; i < 256; i++)
            if (p[i] != (unsigned char)(i * 7 + 1)) bad++;
        printf("[memory] write then read back: %s\n", bad == 0 ? "matches" : "DOES NOT MATCH");
        if (bad) failures++;
        unmap_memory(device, memory);
    }

    int fd = -1;
    VkMemoryGetFdInfoKHR gi = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    gi.memory = memory;
    gi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    r = get_memory_fd(device, &gi, &fd);
    printf("[memory] vkGetMemoryFdKHR: %d (fd %d)\n", r, fd);
    if (r != VK_SUCCESS || fd < 0) {
        failures++;
    } else {
        off_t size = lseek(fd, 0, SEEK_END);
        printf("[memory] the descriptor is %ld bytes\n", (long)size);
        if (size < (off_t)bytes) failures++;
        close(fd);
    }

    free_memory(device, memory, NULL);
    printf("[memory] done\n");
    return failures;
}

/* Phase 2 does what a real Vulkan tool does: enables every reported extension
 * and walks the long pNext chains (the VkPhysicalDeviceVulkan1x* structs) that
 * vulkaninfo relies on. Anything wrong in the chain marshalling shows up here,
 * in a binary that carries our symbols, instead of inside a stripped tool. */
static int tool_style_sweep(PFN_vkGetInstanceProcAddr gipa)
{
    PFN_vkEnumerateInstanceExtensionProperties enum_inst_ext =
        (PFN_vkEnumerateInstanceExtensionProperties)gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    PFN_vkDestroyInstance destroy_inst = (PFN_vkDestroyInstance)gipa(NULL, "vkDestroyInstance");
    if (!enum_inst_ext || !create) return 1;
    int failures = 0;

    uint32_t count = 0;
    enum_inst_ext(NULL, &count, NULL);
    VkExtensionProperties *exts = calloc(count ? count : 1, sizeof(*exts));
    enum_inst_ext(NULL, &count, exts);
    const char **names = calloc(count ? count : 1, sizeof(char *));
    for (uint32_t i = 0; i < count; i++) names[i] = exts[i].extensionName;
    printf("\n[tool sweep] instance extensions reported: %u\n", count);

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vkforward-tool-sweep";
    app.apiVersion = VK_MAKE_VERSION(1, 3, 0);
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = count;
    ici.ppEnabledExtensionNames = names;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&ici, NULL, &instance);
    printf("[tool sweep] create instance with all extensions: %d\n", r);
    printf("[tool sweep] sweeping instance extensions: %d\n", r);
    if (r != VK_SUCCESS) {
        free(exts);
        free(names);
        return 1;
    }

    gipa = (PFN_vkGetInstanceProcAddr)gipa(instance, "vkGetInstanceProcAddr");
    PFN_vkEnumeratePhysicalDevices enum_pd = (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties2 props2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(instance, "vkGetPhysicalDeviceProperties2");
    PFN_vkGetPhysicalDeviceFeatures2 features2 = (PFN_vkGetPhysicalDeviceFeatures2)gipa(instance, "vkGetPhysicalDeviceFeatures2");
    PFN_vkGetPhysicalDeviceMemoryProperties2 mem2 =
        (PFN_vkGetPhysicalDeviceMemoryProperties2)gipa(instance, "vkGetPhysicalDeviceMemoryProperties2");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 qf2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    PFN_vkGetPhysicalDeviceImageFormatProperties2 image2 =
        (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(instance, "vkGetPhysicalDeviceImageFormatProperties2");
    PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 sparse2 =
        (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2)gipa(instance, "vkGetPhysicalDeviceSparseImageFormatProperties2");
    PFN_vkEnumerateDeviceExtensionProperties dev_ext =
        (PFN_vkEnumerateDeviceExtensionProperties)gipa(instance, "vkEnumerateDeviceExtensionProperties");

    uint32_t pd_count = 0;
    enum_pd(instance, &pd_count, NULL);
    VkPhysicalDevice *pds = calloc(pd_count ? pd_count : 1, sizeof(*pds));
    enum_pd(instance, &pd_count, pds);

    for (uint32_t i = 0; i < pd_count; i++) {
        VkPhysicalDeviceVulkan13Properties v13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
        VkPhysicalDeviceVulkan12Properties v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        VkPhysicalDeviceVulkan11Properties v11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
        VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceMaintenance3Properties maint3 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES };
        v12.pNext = &v13;
        v11.pNext = &v12;
        driver.pNext = &v11;
        maint3.pNext = &driver;
        VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        p2.pNext = &maint3;
        props2(pds[i], &p2);
        printf("[tool sweep] %s: updateAfterBind=%u subgroupSize=%u driverID=%d\n",
               p2.properties.deviceName, v12.robustBufferAccessUpdateAfterBind, v11.subgroupSize, driver.driverID);

        /*
         * The links are the caller's, and the server has addresses of its
         * own. In one process a leaked server pointer still dereferences,
         * so the sweep would pass and the tablet would crash: check the
         * links are the ones we laid down, not merely plausible.
         */
        if (p2.pNext != &maint3 || maint3.pNext != &driver || driver.pNext != &v11 ||
            v11.pNext != &v12 || v12.pNext != &v13 || v13.pNext != NULL) {
            printf("[tool sweep] FAIL: Properties2 rewrote the caller's pNext links\n");
            failures++;
        }

        VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        f12.pNext = &f13;
        f11.pNext = &f12;
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = &f11;
        features2(pds[i], &f2);
        printf("[tool sweep] chained features: timelineSemaphore=%u synchronization2=%u dynamicRendering=%u\n",
               f12.timelineSemaphore, f13.synchronization2, f13.dynamicRendering);

        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT
        };
        VkPhysicalDeviceMemoryProperties2 m2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
        m2.pNext = &budget;
        mem2(pds[i], &m2);
        printf("[tool sweep] memory types=%u heap budget[0]=%llu KiB\n", m2.memoryProperties.memoryTypeCount,
               (unsigned long long)budget.heapBudget[0] / 1024);

        uint32_t qf_count = 0;
        qf2(pds[i], &qf_count, NULL);
        VkQueueFamilyProperties2 *qfs = calloc(qf_count ? qf_count : 1, sizeof(*qfs));
        for (uint32_t q = 0; q < qf_count; q++) qfs[q].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        qf2(pds[i], &qf_count, qfs);
        printf("[tool sweep] queue families via Properties2: %u (first has %u queues)\n", qf_count,
               qf_count ? qfs[0].queueFamilyProperties.queueCount : 0);
        free(qfs);

        VkPhysicalDeviceImageFormatInfo2 info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.type = VK_IMAGE_TYPE_2D;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        VkImageFormatProperties2 props = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
        VkResult ir = image2(pds[i], &info, &props);
        printf("[tool sweep] image format properties: %d maxExtent=%u\n", ir,
               props.imageFormatProperties.maxExtent.width);

        VkPhysicalDeviceSparseImageFormatInfo2 sinfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2 };
        sinfo.format = info.format;
        sinfo.type = info.type;
        sinfo.tiling = info.tiling;
        sinfo.usage = info.usage;
        sinfo.samples = VK_SAMPLE_COUNT_1_BIT;
        uint32_t sparse_count = 0;
        sparse2(pds[i], &sinfo, &sparse_count, NULL);
        printf("[tool sweep] sparse image format properties count=%u\n", sparse_count);

        dump_device_extensions(dev_ext, pds[i]);
    }

    free(pds);
    free(exts);
    free(names);
    if (destroy_inst) destroy_inst(instance, NULL);
    printf("[tool sweep] %s\n", failures == 0 ? "done" : "done, with failures");
    return failures == 0 ? 0 : 1;
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

/* The instance-level WSI the ICD provides itself: a Wayland client has to be
 * able to discover an extension no Android driver offers, then create an
 * instance asking for it, without the driver ever hearing the name. */
static int wsi_round_trip(PFN_vkGetInstanceProcAddr gipa)
{
    PFN_vkEnumerateInstanceExtensionProperties list =
        (PFN_vkEnumerateInstanceExtensionProperties)gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkGetInstanceProcAddr igipa = gipa;
    int failures = 0;
    if (!list) {
        printf("[wsi] vkEnumerateInstanceExtensionProperties: missing\n");
        return 1;
    }

    uint32_t count = 0;
    list(NULL, &count, NULL);
    VkExtensionProperties *props = calloc(count, sizeof(*props));
    if (!props) return 1;
    list(NULL, &count, props);

    static const char *const wanted[] = {
        "VK_KHR_surface", "VK_KHR_swapchain", "VK_KHR_wayland_surface",
        "VK_EXT_surface_maintenance1",
    };
    VkBool32 have[sizeof(wanted) / sizeof(wanted[0])] = { 0 };
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t w = 0; w < sizeof(wanted) / sizeof(wanted[0]); w++)
            if (!strcmp(props[i].extensionName, wanted[w])) have[w] = VK_TRUE;

    printf("[wsi] instance reports %u extensions\n", count);
    for (uint32_t w = 0; w < sizeof(wanted) / sizeof(wanted[0]); w++) {
        printf("[wsi]   %-32s %s\n", wanted[w], have[w] ? "advertised" : "MISSING");
        if (!have[w]) failures++;
    }
    free(props);
    if (failures) return failures;

    /* Now ask for them all, which is what a compositor does and what the
     * driver must never be told. */
    VkInstance wsi_instance = VK_NULL_HANDLE;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = (uint32_t)(sizeof(wanted) / sizeof(wanted[0]));
    info.ppEnabledExtensionNames = wanted;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)igipa(NULL, "vkCreateInstance");
    VkResult r = create ? create(&info, NULL, &wsi_instance) : VK_ERROR_INITIALIZATION_FAILED;
    printf("[wsi] vkCreateInstance asking for them all: %s (%d)\n", r == VK_SUCCESS ? "OK" : "FAILED", r);
    if (r != VK_SUCCESS) return ++failures;

    /* A surface is ours, not the driver's: no display lives here. */
    PFN_vkCreateWaylandSurfaceKHR make_surface =
        (PFN_vkCreateWaylandSurfaceKHR)igipa(wsi_instance, "vkCreateWaylandSurfaceKHR");
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!make_surface) {
        printf("[wsi] vkCreateWaylandSurfaceKHR: missing from the instance table\n");
        return ++failures;
    }
    VkWaylandSurfaceCreateInfoKHR surface_info = {
        VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, NULL, 0, NULL, NULL
    };
    r = make_surface(wsi_instance, &surface_info, NULL, &surface);
    printf("[wsi] vkCreateWaylandSurfaceKHR: %s\n", (r == VK_SUCCESS && surface) ? "OK" : "FAILED");
    if (r != VK_SUCCESS || !surface) return ++failures;

    PFN_vkEnumeratePhysicalDevices pd_list =
        (PFN_vkEnumeratePhysicalDevices)igipa(wsi_instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR caps =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)igipa(wsi_instance,
                                                            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fmts =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)igipa(wsi_instance,
                                                        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    uint32_t pd_count = 0;
    VkPhysicalDevice *pds = NULL;
    if (pd_list) {
        pd_list(wsi_instance, &pd_count, NULL);
        pds = calloc(pd_count, sizeof(*pds));
        if (pds) pd_list(wsi_instance, &pd_count, pds);
    }
    if (!pds || !caps || !fmts) {
        printf("[wsi] surface queries: missing\n");
        return ++failures;
    }
    VkSurfaceCapabilitiesKHR cap = { 0 };
    r = caps(pds[0], surface, &cap);
    printf("[wsi] capabilities: %s %ux%u, %u-%u images\n", r == VK_SUCCESS ? "OK" : "FAILED",
           cap.currentExtent.width, cap.currentExtent.height, cap.minImageCount, cap.maxImageCount);
    if (r != VK_SUCCESS || cap.currentExtent.width == 0) failures++;

    uint32_t fmt_count = 0;
    fmts(pds[0], surface, &fmt_count, NULL);
    VkSurfaceFormatKHR *f = calloc(fmt_count, sizeof(*f));
    printf("[wsi] surface formats: %u offered\n", fmt_count);
    if (f && fmt_count) fmts(pds[0], surface, &fmt_count, f);
    if (!f || fmt_count == 0) failures++;

    /* Presenting itself is not here yet, but the entry points have to be: a
     * missing one is a jump through whatever the loader left behind, not an
     * error a compositor could report. They must answer, and say no. */
    PFN_vkCreateSwapchainKHR make_chain =
        (PFN_vkCreateSwapchainKHR)igipa(wsi_instance, "vkCreateSwapchainKHR");
    PFN_vkDestroySwapchainKHR kill_chain =
        (PFN_vkDestroySwapchainKHR)igipa(wsi_instance, "vkDestroySwapchainKHR");
    PFN_vkQueuePresentKHR present = (PFN_vkQueuePresentKHR)igipa(wsi_instance, "vkQueuePresentKHR");
    PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR wayland_ok =
        (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)igipa(
            wsi_instance, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");

    printf("[wsi] wayland presentation supported: %s\n",
           wayland_ok && wayland_ok(pds[0], 0, NULL) ? "yes" : "no");
    if (!wayland_ok || !wayland_ok(pds[0], 0, NULL)) failures++;

    if (!make_chain || !kill_chain || !present) {
        printf("[wsi] swapchain entry points: missing\n");
        failures++;
    } else {
        VkSwapchainKHR chain = VK_NULL_HANDLE;
        r = make_chain(VK_NULL_HANDLE, NULL, NULL, &chain);
        /* It must refuse rather than jump through an absent implementation. */
        printf("[wsi] vkCreateSwapchainKHR refuses cleanly: %s (chain %s)\n",
               r != VK_SUCCESS ? "yes" : "NO, it claimed to work",
               chain ? "returned" : "left null");
        if (r == VK_SUCCESS || chain) failures++;
        kill_chain(VK_NULL_HANDLE, chain, NULL);
    }

    PFN_vkDestroySurfaceKHR kill =
        (PFN_vkDestroySurfaceKHR)igipa(wsi_instance, "vkDestroySurfaceKHR");
    if (kill) kill(wsi_instance, surface, NULL);
    PFN_vkDestroyInstance destroy_inst =
        (PFN_vkDestroyInstance)igipa(wsi_instance, "vkDestroyInstance");
    if (destroy_inst) destroy_inst(wsi_instance, NULL);
    free(f);
    free(pds);
    printf("[wsi] done\n");
    return failures;
}

/*
 * Two clients at once, which is what a device does and a single test does not.
 *
 * A client opens the socket the first time it asks something and keeps it for
 * as long as it lives. Serving connections one at a time therefore works for
 * the first process and hangs the second, and a hang is not something the
 * caller can report: it waits, gives up, and the app shows nothing useful.
 * Steam asks about the driver from a helper while its client already holds a
 * connection, which is exactly this shape.
 *
 * The first connection stays open on purpose - that is the whole point - and
 * the second one is given a deadline, so a server that cannot serve two
 * clients fails this instead of running until the job times out.
 */
static int raw_connect(const char *endpoint)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    socklen_t len = sizeof(addr);
    if (endpoint[0] == '@') {
        const char *name = endpoint + 1;
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path + 1, name, sizeof(addr.sun_path) - 2);
        len = (socklen_t)(sizeof(sa_family_t) + 1 + strlen(name));
    } else {
        strncpy(addr.sun_path, endpoint, sizeof(addr.sun_path) - 1);
    }
    if (connect(fd, (struct sockaddr *)&addr, len) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool ask_instance_version(int fd)
{
    if (!vkfw_write_frame(fd, VKFW_CMD_ENUMERATE_INSTANCE_VERSION, 1, NULL, 0)) return false;
    struct vkfw_header hdr;
    vkfw_buf payload;
    vkfw_buf_init(&payload);
    bool ok = vkfw_read_frame(fd, &hdr, &payload);
    vkfw_buf_free(&payload);
    return ok && hdr.cmd == VKFW_CMD_ENUMERATE_INSTANCE_VERSION;
}

static int two_clients(const char *endpoint)
{
    int failures = 0;
    printf("[clients] two connections at once\n");

    int a = raw_connect(endpoint);
    if (a < 0) {
        printf("[clients]   first connection: FAILED\n");
        return 1;
    }
    /* Both connections get a deadline. The first one is the trap: the ICD's
     * own connection is still open at this point, so a server that serves one
     * connection at a time never even accepts this one, and a read with no
     * deadline waits forever instead of failing. */
    struct timeval deadline = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(a, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
    if (!ask_instance_version(a)) {
        printf("[clients]   first client served: NO\n");
        failures++;
    } else {
        printf("[clients]   first client served: yes (connection held open)\n");
    }

    int b = raw_connect(endpoint);
    if (b < 0) {
        printf("[clients]   second connection: FAILED\n");
        failures++;
    } else {
        setsockopt(b, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
        if (!ask_instance_version(b)) {
            printf("[clients]   second client served: NO - the server is stuck on the first\n");
            failures++;
        } else {
            printf("[clients]   second client served: yes\n");
        }
        close(b);
    }
    close(a);
    printf("[clients] done\n");
    return failures;
}

int main(int argc, char **argv)
{
    /* Line buffered: what has been printed so far is the only clue when this
     * test hangs on CI, and a job that gets killed takes a full stdout buffer
     * with it. */
    setvbuf(stdout, NULL, _IOLBF, 0);

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
            failures += image_round_trip(gipa, device);
            printf("\n[memory] device memory across the socket\n");
            failures += memory_round_trip(gipa, device);
            PFN_vkDestroyDevice destroy_dev = (PFN_vkDestroyDevice)gipa(instance, "vkDestroyDevice");
            if (destroy_dev) destroy_dev(device, NULL);
        }
    }

    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)gipa(instance, "vkDestroyInstance");
    if (destroy) destroy(instance, NULL);
    free(devices);

    printf("\n%s\n", failures == 0 ? "M1 loopback: PASS" : "M1 loopback: FAIL");
    int sweep = tool_style_sweep(gipa);
    printf("\n[wsi] instance-level window system integration\n");
    int wsi = wsi_round_trip(gipa);
    printf("\n");
    int clients = two_clients(socket_ref);
    return (failures == 0 && sweep == 0 && wsi == 0 && clients == 0) ? 0 : 1;
}
