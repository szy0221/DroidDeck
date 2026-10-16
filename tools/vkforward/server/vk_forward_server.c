/*
 * JNI bridge between the Kotlin server object and the forwarding core.
 *
 * Binds an abstract-namespace socket (so nothing has to be bind-mounted into
 * the proot session and no file needs a path inside the app sandbox), then runs
 * one accept loop for the whole app process. The core itself is shared with the
 * host tests and knows nothing about JNI.
 */
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vkfw_server_core.h"
#include "vkfw_transport.h"

#ifdef __ANDROID__
#include <android/log.h>
#define VKFW_LOG_TAG "vkforward"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, VKFW_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, VKFW_LOG_TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define LOGI(...)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "[vkforward] " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                   \
    } while (0)
#define LOGE(...)                                                                              \
    do {                                                                                       \
        fprintf(stderr, "[vkforward] " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                   \
    } while (0)
#endif

#define VKFW_ABSTRACT_SOCKET "vkforward"

struct vkfw_thread_arg {
    int listen_fd;
};

static void *vkfw_jni_thread(void *p)
{
    struct vkfw_thread_arg *arg = (struct vkfw_thread_arg *)p;
    int fd = arg->listen_fd;
    free(arg);
    vkfw_server_main(fd);
    close(fd);
    return NULL;
}

JNIEXPORT jint JNICALL Java_com_droiddeck_launcher_gpu_VkForwardServer_nativeStart(JNIEnv *env,
                                                                                   jobject thiz,
                                                                                   jstring endpoint)
{
    (void)thiz;

    const char *name = VKFW_ABSTRACT_SOCKET;
    if (endpoint) name = (*env)->GetStringUTFChars(env, endpoint, NULL);
    if (!name) {
        LOGE("no socket name");
        return -4;
    }

    if (vkfw_server_init(NULL) != 0) {
        LOGE("server init failed: no Vulkan library");
        if (endpoint) (*env)->ReleaseStringUTFChars(env, endpoint, name);
        return -1;
    }

    int listen_fd = vkfw_listen(name);
    if (listen_fd < 0) {
        LOGE("cannot listen on abstract socket %s (errno follows)", name);
        if (endpoint) (*env)->ReleaseStringUTFChars(env, endpoint, name);
        return -2;
    }
    if (endpoint) (*env)->ReleaseStringUTFChars(env, endpoint, name);

    struct vkfw_thread_arg *arg = malloc(sizeof(*arg));
    if (!arg) {
        close(listen_fd);
        LOGE("out of memory starting the accept loop");
        return -3;
    }
    arg->listen_fd = listen_fd;

    pthread_t thread;
    if (pthread_create(&thread, NULL, vkfw_jni_thread, arg) != 0) {
        free(arg);
        close(listen_fd);
        LOGE("pthread_create failed");
        return -3;
    }
    pthread_detach(thread);
    LOGI("listening (abstract socket '%s', fd %d)", name, listen_fd);
    return listen_fd;
}
