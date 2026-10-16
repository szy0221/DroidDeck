/*
 * Loader probe: drives our ICD the way a real Vulkan application does — through
 * the system Vulkan loader — while keeping our own symbols in the process.
 *
 * The loopback test deliberately bypasses the loader, so it cannot see problems
 * in the ICD <-> loader contract (that is where vulkaninfo was crashing, with no
 * symbols to tell us why). This probe goes through dlopen + vkGetInstanceProcAddr
 * instead, so a crash here lands in a binary we built.
 *
 * Usage: VK_ICD_FILENAMES=<our json> vkfw_loader_probe [loader]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#define GUARD(cond, msg)                                                                       \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            fprintf(stderr, "FAIL: %s\n", (msg));                                              \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

/* The promoted KHR names only resolve while the app has enabled the extension
 * they came from, so which spelling is reachable depends on the app. */
#define PROMOTED_EXTENSION "VK_KHR_get_physical_device_properties2"

int main(int argc, char **argv)
{
    const char *loader_path = (argc > 1) ? argv[1] : "libvulkan.so.1";
    void *lib = dlopen(loader_path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "cannot dlopen %s: %s\n", loader_path, dlerror());
        return 2;
    }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    GUARD(gipa != NULL, "loader exposes no vkGetInstanceProcAddr");

    /* Enable every extension the ICD reports, the way a tool such as vulkaninfo
     * does, so the promoted-extension paths are exercised too. */
    PFN_vkEnumerateInstanceExtensionProperties enum_ext =
        (PFN_vkEnumerateInstanceExtensionProperties)gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    GUARD(enum_ext != NULL && create != NULL, "loader exposes no instance entry points");

    uint32_t ext_count = 0;
    enum_ext(NULL, &ext_count, NULL);
    VkExtensionProperties *exts = calloc(ext_count ? ext_count : 1, sizeof(*exts));
    enum_ext(NULL, &ext_count, exts);
    const char **names = calloc(ext_count ? ext_count : 1, sizeof(char *));
    for (uint32_t i = 0; i < ext_count; i++) names[i] = exts[i].extensionName;
    printf("[loader probe] %u instance extension(s) reported through the loader\n", ext_count);

    int promoted = 0;
    for (uint32_t i = 0; i < ext_count; i++)
        if (strcmp(exts[i].extensionName, PROMOTED_EXTENSION) == 0) promoted = 1;

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vkforward-loader-probe";
    app.apiVersion = VK_MAKE_VERSION(1, 1, 0);
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = names;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&ici, NULL, &instance);
    GUARD(r == VK_SUCCESS, "vkCreateInstance failed through the loader");

    PFN_vkGetInstanceProcAddr igipa = (PFN_vkGetInstanceProcAddr)gipa(instance, "vkGetInstanceProcAddr");
    GUARD(igipa != NULL, "no vkGetInstanceProcAddr on the instance");

    PFN_vkEnumeratePhysicalDevices enum_pd =
        (PFN_vkEnumeratePhysicalDevices)igipa(instance, "vkEnumeratePhysicalDevices");
    GUARD(enum_pd != NULL, "no vkEnumeratePhysicalDevices");

    uint32_t count = 0;
    GUARD(enum_pd(instance, &count, NULL) == VK_SUCCESS && count > 0,
          "no physical device visible through the loader");
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    enum_pd(instance, &count, devices);
    printf("[loader probe] %u physical device(s)\n", count);

    PFN_vkGetPhysicalDeviceProperties2 props2 =
        (PFN_vkGetPhysicalDeviceProperties2)igipa(instance, "vkGetPhysicalDeviceProperties2");
    GUARD(props2 != NULL, "loader cannot find vkGetPhysicalDeviceProperties2");
    if (promoted)
        GUARD(igipa(instance, "vkGetPhysicalDeviceProperties2KHR") != NULL,
              "promoted alias unresolved even with the extension enabled");
    PFN_vkGetPhysicalDeviceFeatures2 features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)igipa(instance, "vkGetPhysicalDeviceFeatures2");
    GUARD(features2 != NULL, "loader cannot find vkGetPhysicalDeviceFeatures2");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 qf2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)igipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties2");
    GUARD(qf2 != NULL, "loader cannot find vkGetPhysicalDeviceQueueFamilyProperties2");
    PFN_vkGetPhysicalDeviceMemoryProperties2 mem2 =
        (PFN_vkGetPhysicalDeviceMemoryProperties2)igipa(instance, "vkGetPhysicalDeviceMemoryProperties2");
    GUARD(mem2 != NULL, "loader cannot find vkGetPhysicalDeviceMemoryProperties2");
    PFN_vkEnumerateDeviceExtensionProperties dev_ext =
        (PFN_vkEnumerateDeviceExtensionProperties)igipa(instance, "vkEnumerateDeviceExtensionProperties");
    GUARD(dev_ext != NULL, "loader cannot find vkEnumerateDeviceExtensionProperties");

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceVulkan11Properties v11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
        VkPhysicalDeviceVulkan12Properties v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        v11.pNext = &v12;
        driver.pNext = &v11;
        VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        p2.pNext = &driver;
        props2(devices[i], &p2);
        printf("[loader probe] device %u: %s (api %u.%u)\n", i, p2.properties.deviceName,
               VK_API_VERSION_MAJOR(p2.properties.apiVersion), VK_API_VERSION_MINOR(p2.properties.apiVersion));
        printf("  driver=%s maxImageDimension2D=%u subgroupSize=%u pointClipping=%u\n", driver.driverName,
               p2.properties.limits.maxImageDimension2D, v11.subgroupSize, v11.pointClippingBehavior);

        VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        f11.pNext = &f12;
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = &f11;
        features2(devices[i], &f2);
        printf("  features2: multiview=%u bufferDeviceAddress=%u\n", f11.multiview, f12.bufferDeviceAddress);

        uint32_t qf_count = 0;
        qf2(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties2 *qfs = calloc(qf_count ? qf_count : 1, sizeof(*qfs));
        qf2(devices[i], &qf_count, qfs);
        printf("  queue families: %u\n", qf_count);
        free(qfs);

        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT
        };
        VkPhysicalDeviceMemoryProperties2 m2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
        m2.pNext = &budget;
        mem2(devices[i], &m2);
        printf("  memory types=%u\n", m2.memoryProperties.memoryTypeCount);

        uint32_t dev_ext_count = 0;
        dev_ext(devices[i], NULL, &dev_ext_count, NULL);
        printf("  device extensions: %u\n", dev_ext_count);
    }

    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)igipa(instance, "vkDestroyInstance");
    if (destroy) destroy(instance, NULL);
    free(devices);
    free(exts);
    free(names);
    printf("[loader probe] PASS\n");
    return 0;
}
