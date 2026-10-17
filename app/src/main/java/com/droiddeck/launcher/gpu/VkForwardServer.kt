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

    // @JvmStatic is load-bearing, not decoration: without it Kotlin emits an
    // instance method and passes `this` as an argument the native side is not
    // expecting, so every parameter lands one slot off and the jstring is read
    // as a jobject. The C side is written for the static signature, as the
    // rest of the app's native methods are.
    @JvmStatic
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
        val mali = GpuInfo.isMaliDevice("")
        val ok = if (!mali) {
            Log.i(TAG, "not a Mali device, leaving the server down")
            false
        } else {
            start()
        }
        // Into the session bundle as well: the app's own log for a session
        // starts after this runs, so anything said here is otherwise lost.
        try {
            com.droiddeck.launcher.session.SessionEvents.record(
                "vkforward.server",
                mapOf("mali" to mali, "started" to ok, "listening" to (listeners > 0)),
            )
        } catch (_: Throwable) {
            // Recording is diagnostics; never let it stop the session.
        }
        return ok
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
            // A signature mismatch lands here too, and reads like a missing
            // library, so name the method in the message.
            Log.w(TAG, "nativeStart failed: ${t.javaClass.simpleName}: ${t.message}", t)
            -9
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
        -9 -> "the native method could not be called at all"
        else -> "unknown"
    }
}
