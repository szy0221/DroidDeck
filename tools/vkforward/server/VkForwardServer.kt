package com.droiddeck.launcher.gpu

import android.util.Log

/**
 * Android-side half of the Vulkan forwarding layer (route B).
 *
 * Starts the native server once per app process. The native side binds an
 * abstract-namespace Unix socket ("vkforward"), which is why the proot session
 * needs nothing mounted: abstract sockets live outside the filesystem, so the
 * ICD stub inside the runtime can reach them with `VKFW_SOCKET=@vkforward`
 * even though it has its own mount namespace.
 *
 * See docs/development/mali-vulkan-forwarding.md and tools/vkforward/README.md.
 */
object VkForwardServer {
    private const val TAG = "VkForwardServer"
    private const val SOCKET_NAME = "vkforward"

    @Volatile private var listeners = 0

    private external fun nativeStart(endpoint: String): Int

    /** Starts the accept loop. Safe to call more than once per process. */
    @Synchronized
    fun start(): Boolean {
        if (listeners > 0) return true
        val fd = nativeStart(SOCKET_NAME)
        if (fd < 0) {
            Log.w(TAG, "vkforward server not available (fd=$fd)")
            return false
        }
        listeners++
        Log.i(TAG, "vkforward server up, abstract socket '$SOCKET_NAME'")
        return true
    }
}
