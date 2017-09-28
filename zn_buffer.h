/* znet_buffer - send/recv buffer for znet */
#ifndef znet_buffer_h
#define znet_buffer_h


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
#define ZN_BUFFCACHE_MAXLSIZE 31
#define ZN_BUFFCACHE_COUNT    (ZN_BUFFCACHE_MAXLSIZE-ZN_BUFFCACHE_MINLSIZE)


#include <stddef.h>

ZN_NS_BEGIN


/* buffer cache */

typedef struct zn_BufferCache zn_BufferCache;

typedef void *zn_BufferAllocf (void *ud, void *ptr, size_t ns, size_t os);

ZN_API zn_BufferCache *zn_newbuffcache (zn_BufferAllocf *f, void *ud);
ZN_API void            zn_delbuffcache (zn_BufferCache *bc);

ZN_API int zn_prepbuffcache (zn_BufferCache *bc, size_t sz, size_t count);


/* buffer */

typedef struct zn_Buffer {
    char *buff;
    zn_BufferCache *bc;
    size_t size, used;
    char init_buff[ZN_BUFFERSIZE];
} zn_Buffer;

#define zn_buffer(b)      ((b)->buff)
#define zn_bufflen(b)     ((b)->used)
#define zn_addsize(b, sz) ((b)->used += (sz))
#define zn_addchar(b, ch) \
    ((void)((b)->used < (b)->size || zn_prepbuffsize((b), 1)), \
     ((b)->buff[(b)->used++] = (ch)))

ZN_API void zn_initbuffer  (zn_Buffer *b, zn_BufferCache *bc);
ZN_API void zn_resetbuffer (zn_Buffer *b);

ZN_API size_t zn_resizebuffer (zn_Buffer *b, size_t len);

ZN_API char *zn_prepbuffsize (zn_Buffer *b, size_t len);
ZN_API void zn_addlstring    (zn_Buffer *b, const char *s, size_t len);


/* recv buffer */

typedef size_t zn_HeaderHandler (void *ud, const char *buff, size_t len);
typedef void   zn_PacketHandler (void *ud, const char *buff, size_t len);

typedef struct zn_RecvBuffer {
    zn_HeaderHandler *header_handler; void *header_ud;
    zn_PacketHandler *packet_handler; void *packet_ud;
    size_t expected;
    zn_Buffer readed;
    char buff[ZN_BUFFERSIZE];
} zn_RecvBuffer;

#define zn_recvbuff(b)  ((b)->buff)
#define zn_recvsize(b)  ((void)(b), ZN_BUFFERSIZE)

ZN_API void zn_initrecvbuffer  (zn_RecvBuffer *b, zn_BufferCache *bc);
ZN_API void zn_resetrecvbuffer (zn_RecvBuffer *b);

ZN_API void zn_recvonheader (zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud);
ZN_API void zn_recvonpacket (zn_RecvBuffer *b, zn_PacketHandler *h, void *ud);

ZN_API int zn_recvfinish (zn_RecvBuffer *b, size_t count);


/* send buffer */

typedef struct zn_SendBuffer {
    zn_Buffer *sending;
    zn_Buffer *pending;
    size_t sent_count;
    zn_Buffer buffers[2];
} zn_SendBuffer;

ZN_API void zn_initsendbuffer  (zn_SendBuffer *b, zn_BufferCache *bc);
ZN_API void zn_resetsendbuffer (zn_SendBuffer *b);

#define zn_sendbuff(b)   zn_buffer((b)->sending)
#define zn_sendsize(b)   zn_bufflen((b)->sending)
ZN_API int zn_sendprepare (zn_SendBuffer *b, const char *buff, size_t len);

#define zn_curbuffer(b)      (zn_sendsize(b) == 0 ? (b)->sending : (b)->pending)
#define zn_needsend(b, buff) ((buff) == (b)->sending)
ZN_API int zn_sendfinish  (zn_SendBuffer *b, size_t count);


ZN_NS_END

#endif /* znet_buffer_h */


#if defined(ZN_IMPLEMENTATION) && !defined(zn_buffer_implemented)
#define zn_buffer_implemented


#include <stdlib.h>
#include <string.h>

ZN_NS_BEGIN


#ifndef ZN_MAX_SIZET
# define ZN_MAX_SIZET (~(size_t)0 - 100)
#endif

/* buffer cache */

#define znBC_size(count) (sizeof(zn_BufferCache) + (count)*sizeof(void*))

struct zn_BufferCache {
    zn_BufferAllocf *allocf;
    void *ud;
    void *buffs[1];
};

static void *znBC_allocf(void *ud, void *ptr, size_t ns, size_t os) {
    (void)ud, (void)os;
    if (ns == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, ns);
}

static size_t znBC_lowerbound(size_t sz, size_t *ppos) {
    size_t next, realsize = (1 << ZN_BUFFCACHE_MINLSIZE);
    size_t pos = 0;
    while (next = realsize<<1, next <= sz && next < ZN_MAX_SIZET)
        realsize = next, ++pos;
    if (ppos) *ppos = pos <= ZN_BUFFCACHE_MAXLSIZE ?
        pos : ZN_BUFFCACHE_MAXLSIZE;
    return realsize < ZN_MAX_SIZET ? realsize : 0;
}

static size_t znBC_upperbound(size_t sz, size_t *ppos) {
    size_t realsize = (1 << ZN_BUFFCACHE_MINLSIZE);
    size_t pos = 0;
    while (realsize < sz && realsize < ZN_MAX_SIZET)
        realsize <<= 1, ++pos;
    if (ppos) *ppos = pos <= ZN_BUFFCACHE_MAXLSIZE ?
        pos : ZN_BUFFCACHE_MAXLSIZE;
    return realsize < ZN_MAX_SIZET ? realsize : 0;
}

ZN_API zn_BufferCache *zn_newbuffcache(zn_BufferAllocf *f, void *ud) {
    zn_BufferAllocf *allocf = f ? f : znBC_allocf;
    zn_BufferCache *bc = (zn_BufferCache*)allocf(ud, NULL,
            znBC_size(ZN_BUFFCACHE_COUNT), 0);
    int i;
    if (bc == NULL) return NULL;
    bc->allocf = allocf;
    bc->ud = ud;
    for (i = 0; i <= ZN_BUFFCACHE_COUNT; ++i)
        bc->buffs[i] = NULL;
    return bc;
}

ZN_API void zn_delbuffcache(zn_BufferCache *bc) {
    int i;
    if (bc == NULL) return;
    for (i = 0; i <= ZN_BUFFCACHE_COUNT; ++i) {
        void *ptr = bc->buffs[i];
        while (ptr) {
            void *next = *(void**)ptr;
            bc->allocf(bc->ud, ptr, 0, 1<<(ZN_BUFFCACHE_MINLSIZE+i));
            ptr = next;
        }
    }
    bc->allocf(bc->ud, bc, 0, znBC_size(ZN_BUFFCACHE_COUNT));
}

ZN_API int zn_prepbuffcache(zn_BufferCache *bc, size_t sz, size_t count) {
    size_t i, pos;
    int ret = 0;
    if (bc == NULL) return 0;
    sz = znBC_upperbound(sz, &pos);
    for (i = 0; i < count; ++i) {
        void *ptr = bc->allocf(bc->ud, NULL, sz, 0);
        if (ptr) {
            *(void**)ptr = bc->buffs[pos];
            bc->buffs[pos] = ptr;
        }
    }
    return ret;
}

static void znBC_putcache(zn_Buffer *b) {
    zn_BufferCache *bc = b->bc;
    if (b->buff != b->init_buff) {
        size_t pos;
        znBC_lowerbound(b->size, &pos);
        *(void**)b->buff = bc->buffs[pos];
        bc->buffs[pos] = b->buff;
    }
}

static int znBC_getcache(zn_Buffer *b, size_t len) {
    zn_BufferCache *bc = b->bc;
    size_t pos, sz = znBC_upperbound(len, &pos);
    void *newptr;
    if ((newptr = bc->buffs[pos]) != NULL)
        bc->buffs[pos] = *(void**)newptr;
    else if ((newptr = bc->allocf(bc->ud, NULL, sz, 0)) == NULL)
        return 0;
    memcpy(newptr, b->buff, b->used);
    znBC_putcache(b);
    b->buff = newptr;
    b->size = sz;
    return 1;
}


/* buffer */

ZN_API void zn_initbuffer(zn_Buffer *b, zn_BufferCache *bc) {
    b->buff = b->init_buff;
    b->bc   = bc;
    b->size = ZN_BUFFERSIZE;
    b->used = 0;
}

ZN_API void zn_resetbuffer(zn_Buffer *b) {
    if (b->bc)
        znBC_putcache(b);
    else if (b->buff != b->init_buff)
        free(b->buff);
    zn_initbuffer(b, b->bc);
}

ZN_API size_t zn_resizebuffer(zn_Buffer *b, size_t len) {
    size_t newsize = ZN_MIN_ALLOCBUFFSIZE;
    while (newsize < ZN_MAX_SIZET/2 && newsize < len)
        newsize <<= 1;
    if (newsize >= len && (b->bc == NULL || !znBC_getcache(b, len))) {
        char *newbuff;
        if (b->buff != b->init_buff)
            newbuff = (char*)realloc(b->buff, newsize);
        else if ((newbuff = (char*)malloc(newsize)) != NULL)
            memcpy(newbuff, b->buff, b->used);
        if (newbuff) b->buff = newbuff, b->size = newsize;
    }
    return b->size;
}

ZN_API char *zn_prepbuffsize(zn_Buffer *b, size_t len) {
    size_t need = b->used + len, size = b->size;
    if (need > size && zn_resizebuffer(b, need) == size)
        return NULL;
    return &b->buff[b->used];
}

ZN_API void zn_addlstring(zn_Buffer *b, const char *s, size_t len) {
    memcpy(zn_prepbuffsize(b, len), s, len);
    b->used += len;
}


/* recv buffer */

static void znRB_onpacket(void *ud, const char *buff, size_t len)
{ (void)ud; (void)buff; (void)len; }

static size_t znRB_onheader (void *ud, const char *buff, size_t len)
{ (void)ud; (void)buff; return len; }

ZN_API void zn_recvonheader(zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud)
{ b->header_handler = h ? h : znRB_onheader; b->header_ud = ud; }

ZN_API void zn_recvonpacket(zn_RecvBuffer *b, zn_PacketHandler *h, void *ud)
{ b->packet_handler = h ? h : znRB_onpacket; b->packet_ud = ud; }

ZN_API void zn_initrecvbuffer(zn_RecvBuffer *b, zn_BufferCache *bc) {
    b->header_handler = znRB_onheader; b->header_ud = NULL;
    b->packet_handler = znRB_onpacket; b->packet_handler = NULL;
    b->expected = 0;
    zn_initbuffer(&b->readed, bc);
}

ZN_API void zn_resetrecvbuffer(zn_RecvBuffer *b) {
    b->header_handler = znRB_onheader; b->header_ud = NULL;
    b->packet_handler = znRB_onpacket; b->packet_handler = NULL;
    b->expected = 0;
    zn_resetbuffer(&b->readed);
}

ZN_API int zn_recvfinish(zn_RecvBuffer *b, size_t count) {
    char *buff = b->buff;
again:
    if (count == 0) return 0;

    if (b->expected == 0) {
        size_t ret = b->header_handler(b->header_ud, buff, count);
        if (ret == 0) { /* need more result */
            zn_addlstring(&b->readed, buff, count);
            return 1;
        }
        if (ret <= count) {
            if (b->packet_handler)
                b->packet_handler(b->packet_ud, buff, ret);
            buff += ret;
            count -= ret;
            goto again;
        }
        b->expected = ret;
        zn_addlstring(&b->readed, buff, count);
        return 1;
    }

    if (b->readed.used + count >= b->expected) {
        size_t remaining = b->expected - b->readed.used;
        zn_addlstring(&b->readed, buff, remaining);
        if (b->packet_handler)
            b->packet_handler(b->packet_ud, b->readed.buff, b->expected);
        buff += remaining;
        count -= remaining;
        b->expected = 0;
        zn_resetbuffer(&b->readed);
        goto again;
    }

    zn_addlstring(&b->readed, buff, count);
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
    int can_send = b->sending->used == 0;
    zn_Buffer *buffer = can_send ? b->sending : b->pending;
    zn_addlstring(buffer, buff, len);
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
    }
    else { /* still has something to send? */
        char *buff = b->sending->buff;
        b->sent_count += count;
        if (b->sent_count > b->sending->used / 2) { /* too many garbage */
            size_t remaining = b->sending->used - b->sent_count;
            memmove(buff, buff + b->sent_count, remaining);
            b->sending->used = remaining;
            b->sent_count = 0;
        }
        if (b->pending->used != 0) {
            zn_addlstring(b->sending, b->pending->buff, b->pending->used);
            zn_resetbuffer(b->pending);
        }
    }
    return b->sending->used != 0;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc' output='zn_buffer.dll'
   unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc' output='zn_buffer.so' */

