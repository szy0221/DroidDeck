/*
 * Standalone server process.
 *
 * Useful in three places: the CI end-to-end run against lavapipe, adb shell on
 * the tablet (the app integration is optional), and manual debugging.
 *
 * Usage: vkfw_server [endpoint]   endpoint defaults to the abstract socket;
 *                                 an absolute path uses a file socket instead.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vkfw_server_core.h"
#include "vkfw_transport.h"

int main(int argc, char **argv)
{
    const char *endpoint = (argc > 1) ? argv[1] : NULL;
    if (vkfw_server_init(getenv("VKFW_DRIVER")) != 0) {
        fprintf(stderr, "vkfw_server: no usable Vulkan driver found\n");
        return 2;
    }
    int fd = vkfw_listen(endpoint);
    if (fd < 0) {
        fprintf(stderr, "vkfw_server: cannot listen on %s\n", endpoint ? endpoint : "(abstract) vkforward");
        return 3;
    }
    fprintf(stderr, "vkfw_server: listening on %s\n", endpoint ? endpoint : "(abstract) vkforward");
    fflush(stderr);
    return vkfw_server_main(fd) == 0 ? 0 : 4;
}
