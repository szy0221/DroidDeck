/*
 * Framed transport over a Unix stream socket. Both halves use these helpers:
 * the ICD (client) connects, the app-side server listens.
 *
 * Endpoint policy: VKFW_SOCKET first (absolute path or @name for an abstract
 * socket), then the plain file socket, then the abstract name. Abstract
 * sockets matter on Android: they need no path inside the app sandbox and no
 * bind-mount into the proot session.
 */
#ifndef VKFW_TRANSPORT_H
#define VKFW_TRANSPORT_H

#include "vkfw_codec.h"
#include "vkfw_protocol.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- client ------------------------------------------------------------ */
int vkfw_connect(void);                 /* fd, or -1 */
int vkfw_client_fd(void);               /* current fd or -1 (no reconnect) */
void vkfw_client_reset(void);           /* drop the cached connection */

/* Send one request, read one reply. Returns 0 when `out` holds a payload,
 * VKFW_ERR_TRANSPORT when the peer is unreachable and VKFW_ERR_PROTOCOL on a
 * malformed frame. The sequence counter is handled here. */
int vkfw_transact(uint16_t cmd, const void *payload, size_t payload_len, vkfw_buf *out);
/* Same, for replies that carry a descriptor: *recvfd is set to -1 when the
 * reply had none, and the caller owns whatever it is set to. */
int vkfw_transact_fd(uint16_t cmd, const void *payload, size_t payload_len, vkfw_buf *out,
                     int *recvfd);

/* Convenience: result code and body of a reply payload. */
int32_t vkfw_reply_result(const vkfw_buf *b);
bool vkfw_reply_body(const vkfw_buf *b, vkfw_cur *out);

/* ---- server ------------------------------------------------------------ */
/* endpoint: NULL for the platform default, "name" for an abstract socket,
 * "/abs/path" for a file socket. Returns a listening fd or -1. */
int vkfw_listen(const char *endpoint);
/* Reads one framed request; false on EOF or a malformed frame.
 * The request/reply loop itself lives with the command handlers, in
 * server/vkfw_server_core.c. */
bool vkfw_read_frame(int fd, struct vkfw_header *hdr, vkfw_buf *payload);
bool vkfw_write_frame(int fd, uint16_t cmd, uint64_t seq, const void *payload, size_t len);

/* Descriptor-carrying variants. passfd is sent with the header (-1 for none);
 * *recvfd picks up one descriptor from the header (-1 when there was none). */
bool vkfw_read_frame_fd(int fd, struct vkfw_header *hdr, vkfw_buf *payload, int *recvfd);
bool vkfw_write_frame_fd(int fd, uint16_t cmd, uint64_t seq, const void *payload, size_t len,
                         int passfd);

#ifdef __cplusplus
}
#endif

#endif /* VKFW_TRANSPORT_H */
