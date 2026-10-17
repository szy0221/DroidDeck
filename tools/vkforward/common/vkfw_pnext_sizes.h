/* GENERATED FILE - do not edit. Regenerate with scripts/gen_pnext_table.py */
/* Source: Vulkan headers VK_HEADER_VERSION 309, LP64 sizes */
#ifndef VKFW_PNEXT_SIZES_H
#define VKFW_PNEXT_SIZES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VKFW_PNEXT_TABLE_HEADER_VERSION 309

#ifdef __cplusplus
extern "C" {
#endif

/* 0 means 'unknown sType'; the caller must drop such nodes. */
size_t vkfw_pnext_size(uint32_t sType);

/* True for the VkPhysicalDevice*Features* structs: nodes made only of
 * VkBool32 flags. Knowing which nodes those are is what lets a caller
 * ask the driver what it really has and clear the flags it has not,
 * instead of handing over a chain the driver refuses as a whole. */
bool vkfw_pnext_is_features(uint32_t sType);

#ifdef __cplusplus
}
#endif

#endif /* VKFW_PNEXT_SIZES_H */
