/* znet_buffer - send/recv buffer for znet */
#ifndef zn_buffer_h
#define zn_buffer_h

#ifndef ZN_NS_BEGIN
# ifdef __cplusplus
#   define ZN_NS_BEGIN extern "C" {
#   define ZN_NS_END   }
# else
#   define ZN_NS_BEGIN
#   define ZN_NS_END
# endif
#endif /* ZN_NS_BEGIN */

#ifdef ZN_STATIC_API
# ifndef ZN_IMPLEMENTATION
#  define ZN_IMPLEMENTATION
# endif
# if __GNUC__
#   define ZN_API static __attribute((unused))
# else
#   define ZN_API static
# endif
#endif

#if !defined(ZN_API) && defined(_WIN32)
# ifdef ZN_IMPLEMENTATION
#  define ZN_API __declspec(dllexport)
# else
#  define ZN_API __declspec(dllimport)
# endif
#endif

#ifndef ZN_API
# define ZN_API extern
#endif

#ifndef ZN_BUFFERSIZE
# define ZN_BUFFERSIZE 2000
#endif

#define ZN_MIN_ALLOCBUFFSIZE  4096
#define ZN_BUFFCACHE_MINLSIZE 14
#define ZN_BUFFCACHE_MAXLSIZE 31 /* largest bucket is 1 << MAXLSIZE */
#define ZN_BUFFCACHE_COUNT    (ZN_BUFFCACHE_MAXLSIZE - ZN_BUFFCACHE_MINLSIZE)
#define ZN_BUFFCACHE_SLOTS    (ZN_BUFFCACHE_COUNT + 1)

#include <stddef.h>

ZN_NS_BEGIN

/* buffer cache */

typedef struct zn_BufferCache zn_BufferCache;

/* realloc-like contract: ptr == NULL allocates, ns == 0 frees, and a failed
 * grow leaves the old block valid; os is the old block size. */
typedef void *zn_BufferAllocf(void *ud, void *ptr, size_t ns, size_t os);

ZN_API zn_BufferCache *zn_newbuffcache(zn_BufferAllocf *f, void *ud);
ZN_API void            zn_delbuffcache(zn_BufferCache *bc);

ZN_API int zn_prepbuffcache(zn_BufferCache *bc, size_t sz, size_t count);

/* buffer */

typedef struct zn_Buffer {
    char           *buff;
    zn_BufferCache *bc;
    size_t          size, used;
    char            init_buff[ZN_BUFFERSIZE];
} zn_Buffer;

#define zn_buffer(b)      ((b)->buff)
#define zn_bufflen(b)     ((b)->used)
#define zn_addsize(b, sz) ((b)->used += (sz))
/* 1 on success, 0 when the buffer could not grow (the char is not stored) */
#define zn_addchar(b, ch)                                       \
    (((b)->used < (b)->size || zn_prepbuffsize((b), 1) != NULL) \
             ? ((b)->buff[(b)->used++] = (ch), 1)               \
             : 0)

ZN_API void zn_initbuffer(zn_Buffer *b, zn_BufferCache *bc);
ZN_API void zn_resetbuffer(zn_Buffer *b);

ZN_API size_t zn_resizebuffer(zn_Buffer *b, size_t len);

ZN_API char *zn_prepbuffsize(zn_Buffer *b, size_t len);
/* 1 on success, 0 when the buffer could not grow (OOM) */
ZN_API int zn_addlstring(zn_Buffer *b, const char *s, size_t len);

/* recv buffer */

typedef size_t zn_HeaderHandler(void *ud, const char *buff, size_t len);
typedef void   zn_PacketHandler(void *ud, const char *buff, size_t len);

typedef struct zn_RecvBuffer {
    zn_HeaderHandler *header_handler;
    void             *header_ud;
    zn_PacketHandler *packet_handler;
    void             *packet_ud;
    size_t            expected;
    zn_Buffer         readed;
    char              buff[ZN_BUFFERSIZE];
} zn_RecvBuffer;

#define zn_recvbuff(b) ((b)->buff)
#define zn_recvsize(b) ((void)(b), ZN_BUFFERSIZE)

ZN_API void zn_initrecvbuffer(zn_RecvBuffer *b, zn_BufferCache *bc);
ZN_API void zn_resetrecvbuffer(zn_RecvBuffer *b);

ZN_API void zn_recvonheader(zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud);
ZN_API void zn_recvonpacket(zn_RecvBuffer *b, zn_PacketHandler *h, void *ud);

/* 0 = everything consumed, 1 = need more bytes, -1 = the accumulator could
 * not grow; on -1 the framing state is undefined, reset before reusing */
ZN_API int zn_recvfinish(zn_RecvBuffer *b, size_t count);

/* send buffer */

typedef struct zn_SendBuffer {
    zn_Buffer *sending;
    zn_Buffer *pending;
    size_t     sent_count;
    zn_Buffer  buffers[2];
} zn_SendBuffer;

ZN_API void zn_initsendbuffer(zn_SendBuffer *b, zn_BufferCache *bc);
ZN_API void zn_resetsendbuffer(zn_SendBuffer *b);

#define zn_sendbuff(b)       zn_buffer((b)->sending)
#define zn_sendsize(b)       zn_bufflen((b)->sending)
#define zn_curbuffer(b)      (zn_sendsize(b) == 0 ? (b)->sending : (b)->pending)
#define zn_needsend(b, buff) ((buff) == (b)->sending)
/* 1 = nothing in flight, send now; 0 = queued behind the in-flight payload;
 * -1 = out of memory (nothing was queued) */
ZN_API int zn_sendprepare(zn_SendBuffer *b, const char *buff, size_t len);
ZN_API int zn_sendfinish(zn_SendBuffer *b, size_t count);

ZN_NS_END

#endif /* zn_buffer_h */

#if defined(ZN_IMPLEMENTATION) && !defined(zn_buffer_implemented)
#define zn_buffer_implemented

#include <stdlib.h>
#include <string.h>

ZN_NS_BEGIN

#ifndef ZN_MAX_SIZET
# define ZN_MAX_SIZET (~(size_t)0 - 100)
#endif

/* buffer cache */

#define znZ_size(slots) \
    (sizeof(zn_BufferCache) + ((slots) - 1) * sizeof(void *))

struct zn_BufferCache {
    zn_BufferAllocf *allocf;
    void            *ud;
    void            *buffs[1];
};

static void *znZ_allocf(void *ud, void *ptr, size_t ns, size_t os) {
    (void)ud, (void)os;
    if (ns == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, ns);
}

static size_t znZ_badindex(size_t *ppos) {
    if (ppos) *ppos = 0;
    return 0;
}

/* round down to the largest cached bucket not above sz; returns 0 when sz is
 * above the last bucket, so the caller cannot index buffs[] at all */
static size_t znZ_lowerbound(size_t sz, size_t *ppos) {
    size_t realsize = (size_t)1 << ZN_BUFFCACHE_MINLSIZE;
    size_t pos = 0, next;
    while ((next = realsize << 1) > realsize && next <= sz
           && next < ZN_MAX_SIZET)
        realsize = next, ++pos;
    if (pos >= ZN_BUFFCACHE_SLOTS) return znZ_badindex(ppos);
    if (ppos) *ppos = pos;
    return realsize;
}

/* round up to the smallest cached bucket covering sz; returns 0 when the
 * request is above the last bucket, so the caller falls back to malloc */
static size_t znZ_upperbound(size_t sz, size_t *ppos) {
    size_t realsize = (size_t)1 << ZN_BUFFCACHE_MINLSIZE;
    size_t pos = 0, next;
    while (realsize < sz && realsize < ZN_MAX_SIZET) {
        next = realsize << 1;
        if (next <= realsize) break; /* shift overflow */
        realsize = next;
        ++pos;
    }
    if (realsize < sz || pos >= ZN_BUFFCACHE_SLOTS) return znZ_badindex(ppos);
    if (ppos) *ppos = pos;
    return realsize;
}

ZN_API zn_BufferCache *zn_newbuffcache(zn_BufferAllocf *f, void *ud) {
    zn_BufferAllocf *allocf = f ? f : znZ_allocf;
    zn_BufferCache  *bc = (zn_BufferCache *)allocf(
            ud, NULL, znZ_size(ZN_BUFFCACHE_SLOTS), 0);
    int i;
    if (bc == NULL) return NULL;
    bc->allocf = allocf;
    bc->ud = ud;
    for (i = 0; i < ZN_BUFFCACHE_SLOTS; ++i) bc->buffs[i] = NULL;
    return bc;
}

ZN_API void zn_delbuffcache(zn_BufferCache *bc) {
    int i;
    if (bc == NULL) return;
    for (i = 0; i < ZN_BUFFCACHE_SLOTS; ++i) {
        void *ptr = bc->buffs[i];
        while (ptr) {
            void *next = *(void **)ptr;
            bc->allocf(bc->ud, ptr, 0,
                       (size_t)1 << (ZN_BUFFCACHE_MINLSIZE + i));
            ptr = next;
        }
    }
    bc->allocf(bc->ud, bc, 0, znZ_size(ZN_BUFFCACHE_SLOTS));
}

ZN_API int zn_prepbuffcache(zn_BufferCache *bc, size_t sz, size_t count) {
    size_t i, pos;
    int    ret = 0;
    if (bc == NULL) return 0;
    sz = znZ_upperbound(sz, &pos);
    if (sz == 0) return 0;
    for (i = 0; i < count; ++i) {
        void *ptr = bc->allocf(bc->ud, NULL, sz, 0);
        if (ptr) {
            *(void **)ptr = bc->buffs[pos];
            bc->buffs[pos] = ptr;
            ++ret;
        }
    }
    return ret;
}

static void znZ_putcache(zn_Buffer *b) {
    zn_BufferCache *bc = b->bc;
    if (b->buff != b->init_buff) {
        size_t pos;
        if (znZ_lowerbound(b->size, &pos) != 0) {
            *(void **)b->buff = bc->buffs[pos];
            bc->buffs[pos] = b->buff;
        } else {
            /* larger than the last bucket: not cacheable; zn_resizebuffer
             * allocated it with the same allocator, so hand it back there */
            bc->allocf(bc->ud, b->buff, 0, b->size);
        }
    }
}

static int znZ_getcache(zn_Buffer *b, size_t len) {
    zn_BufferCache *bc = b->bc;
    size_t          pos, sz = znZ_upperbound(len, &pos);
    void           *newptr;
    if (sz == 0) return 0;
    if ((newptr = bc->buffs[pos]) != NULL)
        bc->buffs[pos] = *(void **)newptr;
    else if ((newptr = bc->allocf(bc->ud, NULL, sz, 0)) == NULL)
        return 0;
    memcpy(newptr, b->buff, b->used);
    znZ_putcache(b);
    b->buff = (char *)newptr;
    b->size = sz;
    return 1;
}

/* buffer */

ZN_API void zn_initbuffer(zn_Buffer *b, zn_BufferCache *bc) {
    b->buff = b->init_buff;
    b->bc = bc;
    b->size = ZN_BUFFERSIZE;
    b->used = 0;
}

ZN_API void zn_resetbuffer(zn_Buffer *b) {
    if (b->bc)
        znZ_putcache(b);
    else if (b->buff != b->init_buff)
        free(b->buff);
    zn_initbuffer(b, b->bc);
}

ZN_API size_t zn_resizebuffer(zn_Buffer *b, size_t len) {
    size_t newsize = ZN_MIN_ALLOCBUFFSIZE;
    while (newsize < ZN_MAX_SIZET / 2 && newsize < len) newsize <<= 1;
    if (newsize >= len && (b->bc == NULL || !znZ_getcache(b, len))) {
        char *newbuff;
        if (b->bc != NULL) {
            /* a block that bypasses the buckets still belongs to the cache
             * allocator, so zn_resetbuffer can hand it back to the same one */
            void *old = b->buff == b->init_buff ? NULL : b->buff;
            newbuff = (char *)b->bc->allocf(b->bc->ud, old, newsize,
                                            old != NULL ? b->size : 0);
        } else if (b->buff != b->init_buff)
            newbuff = (char *)realloc(b->buff, newsize);
        else
            newbuff = (char *)malloc(newsize);
        if (newbuff != NULL) {
            if (b->buff == b->init_buff) memcpy(newbuff, b->buff, b->used);
            b->buff = newbuff;
            b->size = newsize;
        }
    }
    return b->size;
}

ZN_API char *zn_prepbuffsize(zn_Buffer *b, size_t len) {
    size_t need, size = b->size;
    if (len > ZN_MAX_SIZET - b->used) return NULL;
    need = b->used + len;
    if (need > size && zn_resizebuffer(b, need) == size) return NULL;
    return &b->buff[b->used];
}

ZN_API int zn_addlstring(zn_Buffer *b, const char *s, size_t len) {
    char *dst = zn_prepbuffsize(b, len);
    if (dst == NULL) return 0;
    memcpy(dst, s, len);
    b->used += len;
    return 1;
}

/* recv buffer */

static void znX_onpacket(void *ud, const char *buff, size_t len) {
    (void)ud;
    (void)buff;
    (void)len;
}

static size_t znX_onheader(void *ud, const char *buff, size_t len) {
    (void)ud;
    (void)buff;
    return len;
}

ZN_API void zn_recvonheader(zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud) {
    b->header_handler = h ? h : znX_onheader;
    b->header_ud = ud;
}

ZN_API void zn_recvonpacket(zn_RecvBuffer *b, zn_PacketHandler *h, void *ud) {
    b->packet_handler = h ? h : znX_onpacket;
    b->packet_ud = ud;
}

ZN_API void zn_initrecvbuffer(zn_RecvBuffer *b, zn_BufferCache *bc) {
    b->header_handler = znX_onheader;
    b->header_ud = NULL;
    b->packet_handler = NULL;
    b->packet_ud = NULL;
    b->expected = 0;
    zn_initbuffer(&b->readed, bc);
}

ZN_API void zn_resetrecvbuffer(zn_RecvBuffer *b) {
    b->header_handler = znX_onheader;
    b->header_ud = NULL;
    b->packet_handler = NULL;
    b->packet_ud = NULL;
    b->expected = 0;
    zn_resetbuffer(&b->readed);
}

/* Consume as many complete frames as possible from buff[0..count).  The
 * caller guarantees the bytes are contiguous: either the current chunk with
 * nothing buffered, or b->readed after the chunk was appended.  Returns the
 * bytes consumed; if consumed < count the tail is a partial frame and
 * b->expected holds its total length, or 0 while the header is still
 * unknown.  A zero return from the header handler means "need more". */
static size_t znX_parse(zn_RecvBuffer *b, char *buff, size_t count) {
    size_t used = 0;
    while (used < count) {
        size_t ret;
        if (b->expected == 0) {
            ret = b->header_handler(b->header_ud, buff + used, count - used);
            if (ret == 0) break;      /* header incomplete */
            if (ret > count - used) { /* frame spans this buffer */
                b->expected = ret;
                break;
            }
            if (b->packet_handler)
                b->packet_handler(b->packet_ud, buff + used, ret);
            used += ret;
        } else {
            if (count - used < b->expected) break;
            if (b->packet_handler)
                b->packet_handler(b->packet_ud, buff + used, b->expected);
            used += b->expected;
            b->expected = 0;
        }
    }
    return used;
}

ZN_API int zn_recvfinish(zn_RecvBuffer *b, size_t count) {
    size_t used;

    /* a zero-length call never consumes the bytes already buffered */
    if (count == 0) return 0;

    if (b->readed.used == 0 && b->expected == 0) {
        used = znX_parse(b, b->buff, count);
        if (used == count) return 0;
        if (!zn_addlstring(&b->readed, b->buff + used, count - used)) return -1;
        return 1;
    }

    /* a frame started in an earlier call: make it contiguous, parse it, and
     * keep only the tail that is still incomplete */
    if (!zn_addlstring(&b->readed, b->buff, count)) return -1;
    count = b->readed.used;
    used = znX_parse(b, b->readed.buff, count);
    if (used == count) {
        b->readed.used = 0;
        return 0;
    }
    memmove(b->readed.buff, b->readed.buff + used, count - used);
    b->readed.used = count - used;
    return 1;
}

/* send buffer */

ZN_API void zn_initsendbuffer(zn_SendBuffer *b, zn_BufferCache *bc) {
    b->sending = &b->buffers[0];
    b->pending = &b->buffers[1];
    b->sent_count = 0;
    zn_initbuffer(&b->buffers[0], bc);
    zn_initbuffer(&b->buffers[1], bc);
}

ZN_API void zn_resetsendbuffer(zn_SendBuffer *b) {
    b->sending = &b->buffers[0];
    b->pending = &b->buffers[1];
    b->sent_count = 0;
    zn_resetbuffer(&b->buffers[0]);
    zn_resetbuffer(&b->buffers[1]);
}

ZN_API int zn_sendprepare(zn_SendBuffer *b, const char *buff, size_t len) {
    int        can_send = b->sending->used == 0;
    zn_Buffer *buffer = can_send ? b->sending : b->pending;
    if (!zn_addlstring(buffer, buff, len)) return -1;
    return can_send;
}

ZN_API int zn_sendfinish(zn_SendBuffer *b, size_t count) {
    if (b->sending->used - b->sent_count == count) { /* all sent? */
        zn_Buffer *tmp;
        b->sent_count = 0;
        zn_resetbuffer(b->sending);
        /* swap b->sending and b->pending */
        tmp = b->pending;
        b->pending = b->sending;
        b->sending = tmp;
    } else { /* still has something to send? */
        char *buff = b->sending->buff;
        b->sent_count += count;
        if (b->sent_count > b->sending->used / 2) { /* too many garbage */
            size_t remaining = b->sending->used - b->sent_count;
            memmove(buff, buff + b->sent_count, remaining);
            b->sending->used = remaining;
            b->sent_count = 0;
        }
        if (b->pending->used != 0) {
            /* on OOM keep pending: the next finish call retries the merge */
            if (!zn_addlstring(b->sending, b->pending->buff, b->pending->used))
                return 1;
            zn_resetbuffer(b->pending);
        }
    }
    return b->sending->used != 0;
}

ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc' output='zn_buffer.dll'
   unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
   output='zn_buffer.so' */
