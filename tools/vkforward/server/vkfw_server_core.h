/*
 * App-side forwarding server (core) — see tools/vkforward/README.md.
 *
 * Owns the real Vulkan stack in the Android app process: dlopen's the system
 * Vulkan library (which resolves to the vendor HAL on device) and executes the
 * calls the ICD stub sends over the socket. No JNI and no Android-specific
 * code beyond logging and the driver search order.
 */
#ifndef VKFW_SERVER_CORE_H
#define VKFW_SERVER_CORE_H

#include "vkfw_codec.h"
#include "vkfw_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the real driver. `driver_path` overrides the search order (tests).
 * Returns 0 on success; safe to call repeatedly. */
int vkfw_server_init(const char *driver_path);

/* Serve one connection until it breaks. */
void vkfw_serve_connection(int fd);

/* Blocking accept loop over a listening socket from vkfw_listen(). */
int vkfw_server_main(int listen_fd);

#ifdef __cplusplus
}
#endif

#endif /* VKFW_SERVER_CORE_H */
