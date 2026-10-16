package com.droiddeck.launcher.gpu

import android.system.Os
import android.system.OsConstants
import android.util.Log
import java.io.File

/**
 * Android-side half of the Mali Vulkan forwarding layer (M1 skeleton).
 *
 * Binds a Unix socket inside the app sandbox; the session script bind-mounts
 * it into the proot runtime so the ICD stub can reach it. The native server
 * (`vk_forward_server.c`) owns the real vendor Vulkan driver.
 *
 * Not wired into session startup yet — see
 * docs/development/mali-vulkan-forwarding.md for the plan.
 */
object VkForwardServer {
    private const val TAG = "VkForwardServer"
    private const val SOCKET_NAME = "vkforward.sock"

    @Volatile private var running = false

    private external fun nativeMain(listenFd: Int): Int

    fun socketDir(context: android.content.Context): File =
        File(context.filesDir, ".vkforward").apply { mkdirs() }

    /** Starts the server thread. Safe to call once per app process. */
    @Synchronized
    fun start(context: android.content.Context) {
        if (running) return
        val socket = File(socketDir(context), SOCKET_NAME)
        socket.delete()
        val fd = Os.socket(OsConstants.AF_UNIX, OsConstants.SOCK_STREAM, 0)
        try {
            Os.bind(fd, java.net.UnixDomainSocketAddress.of(socket.path))
            Os.listen(fd, 4)
            System.loadLibrary("vkforward_server")
            Thread({ nativeMain(fd) }, "vkforward-server").apply { isDaemon = true }.start()
            running = true
            Log.i(TAG, "listening on ${socket.path}")
        } catch (t: Throwable) {
            Os.close(fd)
            Log.e(TAG, "server start failed", t)
        }
    }
}
