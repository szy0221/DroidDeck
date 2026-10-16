/*
 * Loader probe: drives our ICD the way a real Vulkan application does — through
 * the system Vulkan loader — while keeping our own symbols in the process.
 *
 * The loopback test deliberately bypasses the loader, so it cannot see problems
 * in the ICD<->loader contract (that is where vulkaninfo was crashing, with no
 * symbols to tell us why). This probe goes through dlopen+VkGetInstanceProcAddr
 * instead, so a crash here lands in a binary we built.
 *
 * Usage: VK_ICD_FILENAMES=<our json> vkfw_loader_probe [driver]
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

int main(int argc, char **argv)
{
    const char *loader = (argc > 1) ? argv[1] : "libvulkan.so.1";
    void *lib = dlopen(loader, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "cannot dlopen %s: %s\n", loader, dlerror());
        return 2;
    }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    GUARD(gipa != NULL, "loader exposes no vkGetInstanceProcAddr");

    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    GUARD(create != NULL, "no vkCreateInstance");

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "vkforward-loader-probe";
    app.apiVersion = VK_MAKE_VERSION(1, 1, 0);
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = create(&ici, NULL, &instance);
    GUARD(r == VK_SUCCESS, "vkCreateInstance failed through the loader");

    PFN_vkGetInstanceProcAddr instance_gipa = (PFN_vkGetInstanceProcAddr)gipa(instance, "vkGetInstanceProcAddr");
    PFN_vkEnumeratePhysicalDevices enum_pd =
        (PFN_vkEnumeratePhysicalDevices)instance_gipa(instance, "vkEnumeratePhysicalDevices");
    GUARD(enum_pd != NULL, "no vkEnumeratePhysicalDevices");

    uint32_t count = 0;
    GUARD(enum_pd(instance, &count, NULL) == VK_SUCCESS && count > 0,
          "no physical device visible through the loader");
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    enum_pd(instance, &count, devices);
    printf("[loader probe] %u physical device(s)\n", count);

    /* The KHR spellings are what a tool enabling VK_KHR_get_physical_device_properties2
     * reaches for; both must work. */
    PFN_vkGetPhysicalDeviceProperties2 props2 =
        (PFN_vkGetPhysicalDeviceProperties2)instance_gipa(instance, "vkGetPhysicalDeviceProperties2KHR");
    GUARD(props2 != NULL, "loader cannot find vkGetPhysicalDeviceProperties2KHR");
    PFN_vkGetPhysicalDeviceFeatures2 features2 =
        (PFN_vkGetPhysicalDeviceFeatures2)instance_gipa(instance, "vkGetPhysicalDeviceFeatures2KHR");
    GUARD(features2 != NULL, "loader cannot find vkGetPhysicalDeviceFeatures2KHR");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2 qf2 =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)instance_gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    GUARD(qf2 != NULL, "loader cannot find vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    PFN_vkGetPhysicalDeviceMemoryProperties2 mem2 =
        (PFN_vkGetPhysicalDeviceMemoryProperties2)instance_gipa(instance, "vkGetPhysicalDeviceMemoryProperties2KHR");
    GUARD(mem2 != NULL, "loader cannot find vkGetPhysicalDeviceMemoryProperties2KHR");
    PFN_vkGetPhysicalDeviceToolProperties tools_fn =
        (PFN_vkGetPhysicalDeviceToolProperties)instance_gipa(instance, "vkGetPhysicalDeviceToolPropertiesEXT");
    GUARD(tools_fn != NULL, "loader cannot find vkGetPhysicalDeviceToolPropertiesEXT");

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceDriverProperties driver = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
        VkPhysicalDeviceVulkan11Properties v11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES };
        VkPhysicalDeviceVulkan12Properties v12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };
        v12.pNext = NULL;
        v11.pNext = &v12;
        driver.pNext = &v11;
        VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        p2.pNext = &driver;
        props2(devices[i], &p2);
        printf("[loader probe] device %u: %s\n", i, p2.properties.deviceName);
        printf("  driver=%s maxImageDimension2D=%u subgroupSize=%u denormBehaviorIndependence=%u\n",
               driver.driverName, p2.properties.limits.maxImageDimension2D, v11.subgroupSize,
               v11.pointClippingBehavior);

        VkPhysicalDeviceVulkan11Features f11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        f11.pNext = &f12;
        VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.pNext = &f11;
        features2(devices[i], &f2);
        printf("  features2: multiview=%u bufferDeviceAddress=%u\n", f11.multiview,
               f12.bufferDeviceAddress);

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

        uint32_t tool_count = 0;
        tools_fn(devices[i], &tool_count, NULL);
        VkPhysicalDeviceToolProperties *tools = tool_count ? calloc(tool_count, sizeof(*tools)) : NULL;
        if (tools) tools_fn(devices[i], &tool_count, tools);
        printf("  attached tools: %u%s\n", tool_count, tool_count ? "" : " (none reported)");
        free(tools);
    }

    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)instance_gipa(instance, "vkDestroyInstance");
    if (destroy) destroy(instance, NULL);
    free(devices);
    printf("[loader probe] PASS\n");
    return 0;
}
