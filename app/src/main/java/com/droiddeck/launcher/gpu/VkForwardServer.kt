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

    /**
     * Starts the accept loop on hardware route B exists for, and stays quiet
     * everywhere else. Adreno devices already have a native driver in the
     * runtime, so an extra listener there would only be an idle socket.
     *
     * The empty device name keeps the check cheap: it skips the Vulkan query
     * and falls through to the mali_kbase node, the Mali HAL and
     * ro.hardware.vulkan, which is what a XRING O3 has and an Adreno does not.
     */
    @Synchronized
    fun startOnMali(): Boolean {
        // Say why we are not starting. A device with no listener and no log for
        // it is indistinguishable from one whose code was never shipped, and
        // the session log is the only thing to read out here.
        if (!GpuInfo.isMaliDevice("")) {
            Log.i(TAG, "not a Mali device, leaving the server down")
            return false
        }
        return start()
    }

    /** Starts the accept loop. Safe to call more than once per process. */
    @Synchronized
    fun start(): Boolean {
        if (listeners > 0) return true
        // Fold the load into the call: a device without a Mali driver still has
        // to boot the launcher, and dlopen("libvulkan.so") failing there is the
        // expected outcome rather than a crash worth reporting.
        val fd = try {
            System.loadLibrary("vkforward_server")
            nativeStart(SOCKET_NAME)
        } catch (t: Throwable) {
            Log.w(TAG, "vkforward server not available", t)
            -1
        }
        if (fd < 0) {
            Log.w(TAG, "server not started (native error $fd: ${explain(fd)})")
            return false
        }
        listeners++
        Log.i(TAG, "vkforward server up, abstract socket '$SOCKET_NAME'")
        return true
    }

    /** The native error codes, so a negative result says more than "failed". */
    private fun explain(code: Int): String = when (code) {
        -1 -> "no Vulkan library to forward to"
        -2 -> "cannot bind abstract socket '$SOCKET_NAME'"
        -3 -> "cannot start the accept loop"
        -4 -> "no socket name given"
        else -> "unknown"
    }
}
