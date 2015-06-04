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

#define ZN_MIN_BUFFSIZ 512


#include <stddef.h>

ZN_NS_BEGIN


/* ring buffer */

typedef struct zn_RingBuffer {
    char *buff;
    size_t size, head, tail;
    char init_buff[ZN_MIN_BUFFSIZ];
} zn_RingBuffer;

#define zn_size(b) ((b)->size)

ZN_API void zn_initbuffer  (zn_RingBuffer *b);
ZN_API void zn_resetbuffer (zn_RingBuffer *b);

ZN_API size_t zn_count (zn_RingBuffer *b);

ZN_API char  *zn_prepare  (zn_RingBuffer *b, size_t len);
ZN_API size_t zn_pushsize (zn_RingBuffer *b, size_t len);

ZN_API const char *zn_buffer  (zn_RingBuffer *b, size_t *plen);
ZN_API const char *zn_splice  (zn_RingBuffer *b, size_t *plen);
ZN_API size_t      zn_popsize (zn_RingBuffer *b, size_t len);

ZN_API size_t zn_enqueue (zn_RingBuffer *b, const char *s, size_t len);
ZN_API size_t zn_dequeue (zn_RingBuffer *b, char *s, size_t len);


/* recv buffer */

typedef int zn_HeaderParser(void *ud, const char *buff, size_t len);

typedef struct zn_RecvBuffer {
    zn_RingBuffer recving;
    zn_HeaderParser *parser;
    void *ud;
} zn_RecvBuffer;

ZN_API char *zn_recvmore(zn_RecvBuffer *b, size_t *plen);
ZN_API int zn_recvresult(zn_RecvBuffer *b, int count);


/* send buffer */

typedef struct zn_SendBuffer {
    zn_RingBuffer pending;
    zn_RingBuffer sending;
} zn_SendBuffer;

ZN_API void zn_initsend(zn_SendBuffer *sb);
ZN_API void zn_resetsend(zn_SendBuffer *sb);
ZN_API char *zn_prepsend(zn_SendBuffer *sb, size_t len);
ZN_API int zn_sendmore(zn_SendBuffer *sb, const char *buff, size_t len);
ZN_API int zn_sendresult(zn_SendBuffer *sb, int count);


ZN_NS_END

#endif /* znet_buffer_h */


#ifdef ZN_IMPLEMENTATION


#include <stdlib.h>
#include <string.h>

ZN_NS_BEGIN


/* ring buffer */

#define ZN_MAX_SIZET (~(size_t)0 - 100)

static int zn_issplit(zn_RingBuffer *b)
{ return b->head != 0 && b->head >= b->tail; }

static void zn_reverse(char *b, char *e) {
    for (; b < e; ++b, --e) {
        int ch = *b;
        *b = *e;
        *e = ch;
    }
}

ZN_API void zn_initbuffer(zn_RingBuffer *b) {
    b->buff = b->init_buff;
    b->size = ZN_MIN_BUFFSIZ;
    b->head = b->tail = 0;
}

ZN_API void zn_resetbuffer(zn_RingBuffer *b) {
    if (b->buff != b->init_buff)
        free(b->buff);
    zn_initbuffer(b);
}

ZN_API size_t zn_count(zn_RingBuffer *b) {
    return zn_issplit(b) ?
        (b->size - b->head) + b->tail :
        b->tail - b->head;
}

ZN_API char *zn_prepare(zn_RingBuffer *b, size_t len) {
    int issplit = zn_issplit(b);
    char *newbuff;
    size_t required, newsize;
    size_t space = issplit ? b->head - b->tail : b->size - b->tail;
    if (space >= len)
        return &b->buff[b->tail];
    if (!issplit && space + b->head <= len) {
        memmove(&b->buff[0], &b->buff[b->head], b->tail - b->head);
        b->tail -= b->head;
        b->head = 0;
        return &b->buff[b->tail];
    }
    /* new memory required */
    required = len + (issplit ?
            b->size - b->head + b->tail : b->tail - b->head);
    newsize = b->size;
    while (newsize < ZN_MAX_SIZET/2 && newsize < required)
        newsize <<= 1;
    if (newsize < required || (newbuff = (char*)malloc(newsize)) == NULL)
        return NULL;
    if (issplit) {
        size_t headlen = b->size - b->head;
        memcpy(newbuff, &b->buff[b->head], headlen);
        memcpy(&newbuff[headlen], &b->buff[0], b->tail);
    }
    else {
        memcpy(newbuff, b->buff, b->tail - b->head);
    }
    if (b->buff != b->init_buff) free(b->buff);
    b->buff = newbuff;
    b->size = newsize;
    b->head = 0;
    b->tail = required - len;
    return &b->buff[b->tail];
}

ZN_API size_t zn_pushsize(zn_RingBuffer *b, size_t len) {
    size_t space, total = 0;
    if (!zn_issplit(b)) {
        space = b->size - b->tail;
        if (len < space || b->head == 0)
            goto out;
        b->tail = 0;
        len -= space;
        total += space;
    }
    space = b->head - b->tail;
out:
    space = len < space ? len : space;
    b->tail += space;
    return total + space;
}

ZN_API const char *zn_splice(zn_RingBuffer *b, size_t *plen) {
    if (plen) *plen = zn_issplit(b) ?
        b->size - b->head : b->tail - b->head;
    return &b->buff[b->head];
}

ZN_API const char *zn_buffer(zn_RingBuffer *b, size_t *plen) {
    if (zn_issplit(b)) {
        size_t size = b->size - b->head + b->tail;
        zn_reverse(&b->buff[b->head], &b->buff[b->size-1]);
        zn_reverse(&b->buff[0], &b->buff[b->head-1]);
        zn_reverse(&b->buff[0], &b->buff[b->size-1]);
        b->head = 0;
        b->tail = size;
    }
    if (plen)
        *plen = b->tail - b->head;
    return &b->buff[b->head];
}

ZN_API size_t zn_popsize(zn_RingBuffer *b, size_t len) {
    size_t space, total = 0;
    if (zn_issplit(b)) {
        space = b->size - b->head;
        if (len < space) {
            b->head += len;
            return len;
        }
        b->head = 0;
        len -= space;
        total += space;
    }
    space = b->tail - b->head;
    space = len < space ? len : space;
    b->head += space;
    if (b->head == b->tail)
        b->head = b->tail = 0;
    return total + space;
}

ZN_API size_t zn_enqueue(zn_RingBuffer *b, const char *s, size_t len) {
    size_t space, total = 0;
    if (len > (space = b->size - zn_count(b))) {
        char *buff;
        if ((buff = zn_prepare(b, len)) != NULL) {
            memcpy(buff, s, len);
            return zn_pushsize(b, len);
        }
        len = space;
    }
    if (!zn_issplit(b)) {
        space = b->size - b->tail;
        if (space > len) space = len;
        memcpy(&b->buff[b->tail], s, space);
        if (b->head == 0) {
            b->tail += space;
            return space;
        }
        s += space;
        len -= space;
        total += space;
        b->tail = 0;
    }
    memcpy(&b->buff[b->tail], s, len);
    b->tail += len;
    return total + len;
}

ZN_API size_t zn_dequeue(zn_RingBuffer *b, char *s, size_t len) {
    size_t slen;
    const char *splice = zn_splice(b, &slen);
    if (slen >= len) {
        memcpy(s, splice, len);
        b->head += len;
        if (b->head == b->tail)
            b->head = b->tail = 0;
        return len;
    }
    memcpy(s, splice, slen);
    if (!zn_issplit(b)) {
        b->head = b->tail = 0;
        return slen;
    }
    memcpy(s + slen, &b->buff[0], b->head);
    len = b->head + slen;
    b->head = b->tail = 0;
    return len;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* cc: flags+='-mdll -s -O3 -DZN_IMPLEMENTATION -xc' output='znet_buffer.dll' */
