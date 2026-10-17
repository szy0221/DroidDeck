#include "vkfw_transport.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

/* ---- framing helpers ---------------------------------------------------- */

static ssize_t vkfw_write_all(int fd, const void *p, size_t n)
{
    const uint8_t *cur = (const uint8_t *)p;
    size_t left = n;
    while (left > 0) {
        ssize_t wrote = write(fd, cur, left);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (wrote == 0) return -1;
        cur += wrote;
        left -= (size_t)wrote;
    }
    return (ssize_t)n;
}

static ssize_t vkfw_read_exact(int fd, void *p, size_t n)
{
    uint8_t *cur = (uint8_t *)p;
    size_t left = n;
    while (left > 0) {
        ssize_t got = read(fd, cur, left);
        if (got < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (got == 0) return 0; /* peer closed cleanly */
        cur += got;
        left -= (size_t)got;
    }
    return (ssize_t)n;
}

static void vkfw_header_put(uint8_t *dst, const struct vkfw_header *h)
{
    memcpy(dst + 0, &h->magic, 4);
    memcpy(dst + 4, &h->cmd, 2);
    memcpy(dst + 6, &h->flags, 2);
    memcpy(dst + 8, &h->payload_len, 4);
    memcpy(dst + 12, &h->seq, 8);
}

/*
 * Passing a file descriptor alongside a frame.
 *
 * A dma-buf is the only thing both halves can name: the driver allocates
 * memory the client cannot address, and the client needs a handle it can hand
 * to a compositor or map into its own address space. A descriptor is the one
 * currency the kernel will carry across for us, so it rides along with the
 * frame header.
 *
 * It is attached to the header rather than the payload because the reader
 * already reads the header first; anything arriving with the payload would be
 * trivially lost by a reader that only wanted the length.
 */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define VKFW_MAX_PASSFD 4

static ssize_t vkfw_send_header(int fd, const uint8_t *header, size_t n, int passfd)
{
    struct iovec iov;
    iov.iov_base = (void *)header;
    iov.iov_len = n;

    char control[CMSG_SPACE(sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (passfd >= 0) {
        memset(control, 0, sizeof(control));
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &passfd, sizeof(int));
    }

    ssize_t sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
    if (sent < 0 && errno == EINTR) sent = sendmsg(fd, &msg, MSG_NOSIGNAL);
    return sent;
}

/* Reads the header, picking up any descriptors riding along with it. Any
 * beyond the first are closed: a frame carries at most one, and a leaked
 * descriptor is a leak in whichever process received it. */
static ssize_t vkfw_recv_header(int fd, uint8_t *header, size_t n, int *recvfd)
{
    struct iovec iov;
    iov.iov_base = header;
    iov.iov_len = n;

    char control[CMSG_SPACE(VKFW_MAX_PASSFD * sizeof(int))];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    ssize_t got = recvmsg(fd, &msg, 0);
    if (got < 0 && errno == EINTR) got = recvmsg(fd, &msg, 0);
    if (got <= 0) return got;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    for (; cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
        int *fds = (int *)(void *)CMSG_DATA(cmsg);
        size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (size_t i = 0; i < count; i++) {
            if (recvfd && *recvfd < 0) *recvfd = fds[i];
            else close(fds[i]);
        }
    }
    return got;
}

bool vkfw_write_frame_fd(int fd, uint16_t cmd, uint64_t seq, const void *payload, size_t len,
                         int passfd)
{
    if (len > VKFW_MAX_PAYLOAD) return false;
    uint8_t header[VKFW_HEADER_SIZE];
    struct vkfw_header h = {
        .magic = VKFW_MAGIC,
        .cmd = cmd,
        .flags = 0,
        .payload_len = (uint32_t)len,
        .seq = seq,
    };
    vkfw_header_put(header, &h);

    if (passfd < 0) {
        if (vkfw_write_all(fd, header, sizeof(header)) < 0) return false;
    } else {
        ssize_t sent = vkfw_send_header(fd, header, sizeof(header), passfd);
        if (sent < 0) return false;
        if ((size_t)sent < sizeof(header) &&
            vkfw_write_all(fd, header + sent, sizeof(header) - (size_t)sent) < 0)
            return false;
    }
    if (len && vkfw_write_all(fd, payload, len) < 0) return false;
    return true;
}

bool vkfw_write_frame(int fd, uint16_t cmd, uint64_t seq, const void *payload, size_t len)
{
    return vkfw_write_frame_fd(fd, cmd, seq, payload, len, -1);
}

bool vkfw_read_frame_fd(int fd, struct vkfw_header *hdr, vkfw_buf *payload, int *recvfd)
{
    if (recvfd) *recvfd = -1;
    uint8_t header[VKFW_HEADER_SIZE];
    ssize_t got = vkfw_recv_header(fd, header, sizeof(header), recvfd);
    if (got <= 0) return false;
    if ((size_t)got < sizeof(header) &&
        vkfw_read_exact(fd, header + got, sizeof(header) - (size_t)got) <= 0)
        return false;

    memcpy(&hdr->magic, header + 0, 4);
    memcpy(&hdr->cmd, header + 4, 2);
    memcpy(&hdr->flags, header + 6, 2);
    memcpy(&hdr->payload_len, header + 8, 4);
    memcpy(&hdr->seq, header + 12, 8);

    if (hdr->magic != VKFW_MAGIC || hdr->payload_len > VKFW_MAX_PAYLOAD) return false;

    vkfw_buf_clear(payload);
    if (hdr->payload_len == 0) return true;
    void *at = vkfw_buf_extend(payload, hdr->payload_len);
    if (!at) return false;
    if (vkfw_read_exact(fd, at, hdr->payload_len) != (ssize_t)hdr->payload_len) {
        if (recvfd && *recvfd >= 0) {
            close(*recvfd);
            *recvfd = -1;
        }
        return false;
    }
    return true;
}

bool vkfw_read_frame(int fd, struct vkfw_header *hdr, vkfw_buf *payload)
{
    return vkfw_read_frame_fd(fd, hdr, payload, NULL);
}

/* ---- command names and tracing ------------------------------------------ */

#define VKFW_CMD_CASE(name, assign)                                            \
    case VKFW_CMD_##name: return #name;

const char *vkfw_cmd_name(uint16_t cmd)
{
    switch (cmd) {
        VKFW_COMMANDS(VKFW_CMD_CASE)
    }
    return "UNKNOWN";
}

#undef VKFW_CMD_CASE

/*
 * A forwarding ICD crashes inside the caller, not inside itself: by the time
 * the fault lands, the interesting thing is the last query whose answer the
 * caller was chewing on. Printing every exchange with VKFW_TRACE=1 makes that
 * the last line of a session log, which is the only debugger a tablet has.
 */
static bool vkfw_trace_on(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("VKFW_TRACE") != NULL;
    return enabled != 0;
}

static void vkfw_trace(uint16_t cmd, size_t len, int rc, const vkfw_buf *reply)
{
    if (!vkfw_trace_on()) return;
    int32_t result = rc == 0 ? vkfw_reply_result(reply) : rc;
    fprintf(stderr, "[vkforward] %s in=%zu -> %d\n", vkfw_cmd_name(cmd), len, result);
}

/* ---- client ------------------------------------------------------------- */

static pthread_mutex_t g_client_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_client_fd = -1;
static uint64_t g_seq = 1;

/* Builds the sockaddr for "name" (abstract) or "/path" (filesystem). */
static size_t vkfw_sockaddr(struct sockaddr_un *addr, const char *endpoint, bool abstract)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    if (abstract) {
        size_t n = strlen(endpoint);
        if (n > sizeof(addr->sun_path) - 2) return 0;
        addr->sun_path[0] = '\0';
        memcpy(addr->sun_path + 1, endpoint, n);
        return offsetof(struct sockaddr_un, sun_path) + 1 + n;
    }
    size_t n = strlen(endpoint);
    if (n >= sizeof(addr->sun_path)) return 0;
    memcpy(addr->sun_path, endpoint, n);
    return offsetof(struct sockaddr_un, sun_path) + n;
}

static int vkfw_try_connect(const char *endpoint, bool abstract)
{
    struct sockaddr_un addr;
    size_t addrlen = vkfw_sockaddr(&addr, endpoint, abstract);
    if (addrlen == 0) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&addr, (socklen_t)addrlen) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* A session log is all there is to debug with on a device, and a failed
 * connect used to be silent: the caller saw VK_ERROR_INITIALIZATION_FAILED
 * with nothing saying which endpoint was tried or why it refused. Say it
 * once per process, since every command would otherwise repeat it. */
static void vkfw_report_unreachable(const char *env, int fs_errno, int abstract_errno)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static bool reported = false;

    pthread_mutex_lock(&lock);
    bool mine = !reported;
    reported = true;
    pthread_mutex_unlock(&lock);
    if (!mine) return;

    fprintf(stderr, "[vkforward] no server reachable: %s (%s): %s; @%s: %s\n",
            VKFW_FS_SOCKET, (env && *env) ? env : VKFW_SOCKET_ENV " unset",
            strerror(fs_errno), VKFW_ABSTRACT_NAME, strerror(abstract_errno));
    fflush(stderr);
}

/* The other half of the report above: knowing the socket was reached tells
 * you the server is up and the fault is further along. One line per process,
 * which is a fair price for not having to guess from silence. */
static void vkfw_report_connected(const char *endpoint, bool abstract)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static bool reported = false;

    pthread_mutex_lock(&lock);
    bool mine = !reported;
    reported = true;
    pthread_mutex_unlock(&lock);
    if (!mine) return;

    fprintf(stderr, "[vkforward] connected to %s%s\n", abstract ? "@" : "", endpoint);
    fflush(stderr);
}

static int vkfw_open_endpoint(void)
{
    const char *env = getenv(VKFW_SOCKET_ENV);
    if (env && *env) {
        bool abstract = (env[0] == '@');
        int fd = vkfw_try_connect(abstract ? env + 1 : env, abstract);
        if (fd >= 0) {
            vkfw_report_connected(abstract ? env + 1 : env, abstract);
            return fd;
        }
    }
    int fd = vkfw_try_connect(VKFW_FS_SOCKET, false);
    if (fd >= 0) {
        vkfw_report_connected(VKFW_FS_SOCKET, false);
        return fd;
    }
    int fs_errno = errno;

    fd = vkfw_try_connect(VKFW_ABSTRACT_NAME, true);
    if (fd >= 0) {
        vkfw_report_connected(VKFW_ABSTRACT_NAME, true);
        return fd;
    }

    vkfw_report_unreachable(env, fs_errno, errno);
    return -1;
}

int vkfw_client_fd(void) { return g_client_fd; }

int vkfw_connect(void)
{
    pthread_mutex_lock(&g_client_lock);
    if (g_client_fd < 0) g_client_fd = vkfw_open_endpoint();
    int fd = g_client_fd;
    pthread_mutex_unlock(&g_client_lock);
    return fd;
}

void vkfw_client_reset(void)
{
    pthread_mutex_lock(&g_client_lock);
    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }
    pthread_mutex_unlock(&g_client_lock);
}

int vkfw_transact_fd(uint16_t cmd, const void *payload, size_t payload_len, vkfw_buf *out,
                     int *recvfd)
{
    vkfw_buf_clear(out);
    pthread_mutex_lock(&g_client_lock);
    if (g_client_fd < 0) g_client_fd = vkfw_open_endpoint();
    if (g_client_fd < 0) {
        pthread_mutex_unlock(&g_client_lock);
        return VKFW_ERR_TRANSPORT;
    }

    int fd = g_client_fd;
    uint64_t seq = g_seq++;
    int rc = VKFW_ERR_TRANSPORT;
    struct vkfw_header hdr;
    vkfw_buf reply;
    vkfw_buf_init(&reply);

    if (!vkfw_write_frame(fd, cmd, seq, payload, payload_len)) goto out;
    if (!vkfw_read_frame_fd(fd, &hdr, &reply, recvfd)) goto out;
    if (hdr.magic != VKFW_MAGIC || hdr.seq != seq || hdr.cmd != cmd) {
        rc = VKFW_ERR_PROTOCOL;
        goto out;
    }
    *out = reply;
    reply.data = NULL;
    reply.len = 0;
    reply.cap = 0;
    rc = 0;

out:
    vkfw_trace(cmd, payload_len, rc, rc == 0 ? out : &reply);
    if (rc != 0 && recvfd && *recvfd >= 0) {
        close(*recvfd);
        *recvfd = -1;
    }
    vkfw_buf_free(&reply);
    pthread_mutex_unlock(&g_client_lock);
    return rc;
}

int vkfw_transact(uint16_t cmd, const void *payload, size_t payload_len, vkfw_buf *out)
{
    return vkfw_transact_fd(cmd, payload, payload_len, out, NULL);
}

int32_t vkfw_reply_result(const vkfw_buf *b)
{
    int32_t result = VKFW_ERR_PROTOCOL;
    if (b->len >= sizeof(int32_t)) memcpy(&result, b->data, sizeof(int32_t));
    return result;
}

bool vkfw_reply_body(const vkfw_buf *b, vkfw_cur *out)
{
    if (b->len < sizeof(int32_t)) return false;
    out->p = b->data + sizeof(int32_t);
    out->end = b->data + b->len;
    return true;
}

/* ---- server ------------------------------------------------------------- */

int vkfw_listen(const char *endpoint)
{
    bool abstract = endpoint && endpoint[0] != '/' && endpoint[0] != '\0';
    if (!endpoint) {
        /* Default: abstract sockets never need a path in the app sandbox. */
        endpoint = VKFW_ABSTRACT_NAME;
        abstract = true;
    }
    struct sockaddr_un addr;
    size_t addrlen = vkfw_sockaddr(&addr, endpoint, abstract);
    if (addrlen == 0) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (!abstract) unlink(endpoint);
    if (bind(fd, (struct sockaddr *)&addr, (socklen_t)addrlen) != 0) {
        close(fd);
        return -1;
    }
    if (!abstract) chmod(endpoint, 0666);
    if (listen(fd, 8) != 0) {
        if (!abstract) unlink(endpoint);
        close(fd);
        return -1;
    }
    return fd;
}
