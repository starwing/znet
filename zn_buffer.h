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


#include <stddef.h>

ZN_NS_BEGIN


/* buffer */

typedef struct zn_Buffer {
    char *buff;
    size_t size, used;
    char init_buff[ZN_BUFFERSIZE];
} zn_Buffer;

#define zn_buffer(b)      ((b)->buff)
#define zn_bufflen(b)     ((b)->used)
#define zn_addsize(b, sz) ((b)->used += (sz))
#define zn_addchar(b, ch) \
    ((void)((b)->used < (b)->size || zn_prepbuffsize((b), 1)), \
     ((b)->buff[(b)->used++] = (ch)))

ZN_API void zn_initbuffer  (zn_Buffer *b);
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

ZN_API void zn_initrecvbuffer  (zn_RecvBuffer *b);
ZN_API void zn_resetrecvbuffer (zn_RecvBuffer *b);
ZN_API void zn_recvonheader   (zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud);
ZN_API void zn_recvonpacket   (zn_RecvBuffer *b, zn_PacketHandler *h, void *ud);

ZN_API int zn_recvfinish (zn_RecvBuffer *b, size_t count);


/* send buffer */

typedef struct zn_SendBuffer {
    zn_Buffer *sending;
    zn_Buffer *pending;
    size_t sent_count;
    zn_Buffer buffers[2];
} zn_SendBuffer;

ZN_API void zn_initsendbuffer  (zn_SendBuffer *b);
ZN_API void zn_resetsendbuffer (zn_SendBuffer *b);

#define zn_sendbuff(b)   zn_buffer((b)->sending)
#define zn_sendsize(b)   zn_bufflen((b)->sending)
ZN_API int zn_sendprepare (zn_SendBuffer *b, const char *buff, size_t len);

#define zn_sendbuffer(b)     (zn_sendsize(b) == 0 ? (b)->sending : (b)->pending)
#define zn_needsend(b, buff) ((buff) == (b)->sending)

ZN_API int zn_sendfinish  (zn_SendBuffer *b, size_t count);


ZN_NS_END

#endif /* znet_buffer_h */


#if defined(ZN_IMPLEMENTATION) && !defined(zn_buffer_implemented)
#define zn_buffer_implemented


#include <stdlib.h>
#include <string.h>

ZN_NS_BEGIN


/* buffer */

#ifndef ZN_MAX_SIZET
# define ZN_MAX_SIZET (~(size_t)0 - 100)
#endif

ZN_API void zn_initbuffer(zn_Buffer *b) {
    b->buff = b->init_buff;
    b->size = ZN_BUFFERSIZE;
    b->used = 0;
}

ZN_API void zn_resetbuffer(zn_Buffer *b) {
    if (b->buff != b->init_buff)
        free(b->buff);
    zn_initbuffer(b);
}

ZN_API size_t zn_resizebuffer(zn_Buffer *b, size_t len) {
    char *newbuff;
    size_t newsize = b->size;
    while (newsize < ZN_MAX_SIZET/2 && newsize < len)
        newsize <<= 1;
    newbuff = b->buff != b->init_buff ? b->buff : NULL;
    if (newsize >= len
            && ((newbuff = (char*)realloc(newbuff, newsize)) != NULL))
    {
        b->buff = newbuff;
        b->size = newsize;
    }
    return b->size;
}

ZN_API char *zn_prepbuffsize(zn_Buffer *b, size_t len) {
    if (b->used + len > b->size && zn_resizebuffer(b, b->size+len) == b->size)
        return NULL;
    return &b->buff[b->used];
}

ZN_API void zn_addlstring(zn_Buffer *b, const char *s, size_t len) {
    memcpy(zn_prepbuffsize(b, len), s, len);
    b->used += len;
}

/* recv buffer */

static void zn_def_onpacket(void *ud, const char *buff, size_t len)
{ (void)ud; (void)buff; (void)len; }

static size_t zn_def_onheader (void *ud, const char *buff, size_t len)
{ (void)ud; (void)buff; return len; }

ZN_API void zn_recvonheader(zn_RecvBuffer *b, zn_HeaderHandler *h, void *ud)
{ b->header_handler = h ? h : zn_def_onheader; b->header_ud = ud; }

ZN_API void zn_recvonpacket(zn_RecvBuffer *b, zn_PacketHandler *h, void *ud)
{ b->packet_handler = h ? h : zn_def_onpacket; b->packet_ud = ud; }

ZN_API void zn_initrecvbuffer(zn_RecvBuffer *b) {
    b->header_handler = zn_def_onheader; b->header_ud = NULL;
    b->packet_handler = zn_def_onpacket; b->packet_handler = NULL;
    b->expected = 0;
    zn_initbuffer(&b->readed);
}

ZN_API void zn_resetrecvbuffer(zn_RecvBuffer *b) {
    b->header_handler = zn_def_onheader; b->header_ud = NULL;
    b->packet_handler = zn_def_onpacket; b->packet_handler = NULL;
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
        b->readed.used = 0;
        goto again;
    }

    zn_addlstring(&b->readed, buff, count);
    return 1;
}


/* send buffer */

ZN_API void zn_initsendbuffer(zn_SendBuffer *b) {
    b->sending = &b->buffers[0];
    b->pending = &b->buffers[1];
    b->sent_count = 0;
    zn_initbuffer(&b->buffers[0]);
    zn_initbuffer(&b->buffers[1]);
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
        b->sending->used = 0;
        b->sent_count = 0;
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
            b->pending->used = 0;
        }
    }
    return b->sending->used != 0;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc' output='zn_buffer.dll'
   unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc' output='zn_buffer.so' */

