/*
 * Buffer / cursor helpers used on both sides of the vkforward socket.
 *
 * Both halves are compiled against the same Vulkan headers, so Vulkan POD
 * structs (including the pNext chains of the *2 queries) can be copied as
 * blobs. Nodes are rebuilt server-side with a zero tail so a driver built
 * against newer headers cannot read off the end of our copy.
 */
#ifndef VKFW_CODEC_H
#define VKFW_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} vkfw_buf;

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} vkfw_cur;

typedef struct {
    void *node;       /* native struct copy (allocated wide, see .c) */
    size_t size;      /* size the peer declared */
    uint32_t sType;
} vkfw_chain_node;

typedef struct {
    vkfw_chain_node *v;
    uint32_t count;
} vkfw_chain;

/* ---- buffer writing -------------------------------------------------- */
void vkfw_buf_init(vkfw_buf *b);
void vkfw_buf_free(vkfw_buf *b);
void vkfw_buf_clear(vkfw_buf *b);
void *vkfw_buf_extend(vkfw_buf *b, size_t n);
void vkfw_buf_put(vkfw_buf *b, const void *p, size_t n);
void vkfw_buf_put_u32(vkfw_buf *b, uint32_t v);
void vkfw_buf_put_u64(vkfw_buf *b, uint64_t v);
void vkfw_buf_put_f32(vkfw_buf *b, float v);
void vkfw_buf_put_str(vkfw_buf *b, const char *s);      /* NULL encodes as len 0 */
void vkfw_buf_put_result(vkfw_buf *b, int32_t result);

/* ---- cursor reading -------------------------------------------------- */
void vkfw_cur_init(vkfw_cur *c, const void *data, size_t len);
const uint8_t *vkfw_cur_ptr(vkfw_cur *c, size_t n);     /* NULL on overrun */
bool vkfw_cur_bytes(vkfw_cur *c, void *dst, size_t n);
bool vkfw_cur_peek_u32(vkfw_cur *c, uint32_t *v);
bool vkfw_cur_u32(vkfw_cur *c, uint32_t *v);
bool vkfw_cur_u64(vkfw_cur *c, uint64_t *v);
bool vkfw_cur_skip(vkfw_cur *c, size_t n);
size_t vkfw_cur_left(vkfw_cur *c);
/* String / list results are malloc'd; free with vkfw_str_free / vkfw_strlist_free. */
char *vkfw_cur_str(vkfw_cur *c);
bool vkfw_cur_strlist(vkfw_cur *c, uint32_t *count, char ***out);
void vkfw_strlist_free(uint32_t count, char **list);

/* ---- pNext chain ------------------------------------------------------ */
/* Collect the caller's chain into `out` (used by the ICD when sending). */
bool vkfw_chain_collect(const void *head, vkfw_chain *out);
void vkfw_chain_free(vkfw_chain *c);
/* Serialise a collected chain: [u32 count][per node: u32 size][bytes]. */
void vkfw_chain_put(vkfw_buf *b, const vkfw_chain *c);
/* Parse a serialised chain; nodes are malloc'd with a zero tail. */
bool vkfw_chain_parse(vkfw_cur *c, vkfw_chain *out);
/* Link a parsed chain into a real pNext list. Returns the first node. */
void *vkfw_chain_head(const vkfw_chain *c);
/* Copy chains coming back from the server into the caller's nodes. */
bool vkfw_chain_apply(vkfw_cur *c, void *client_head);

#ifdef __cplusplus
}
#endif

#endif /* VKFW_CODEC_H */
