/* znet - C version of zsummerX */
#ifndef znet_h
#define znet_h

#include <stddef.h>

#ifndef ZN_NS_BEGIN
# ifdef __cplusplus
#   define ZN_NS_BEGIN extern "C" {
#   define ZN_NS_END   }
# else
#   define ZN_NS_BEGIN
#   define ZN_NS_END
# endif
#endif /* ZN_NS_BEGIN */

#ifndef ZN_STATIC
# if __GNUC__
#   define ZN_STATIC static __attribute__((unused))
# else
#   define ZN_STATIC static
# endif
#endif /* ZN_STATIC */

#ifdef ZN_STATIC_API
# ifndef ZN_IMPLEMENTATION
#  define ZN_IMPLEMENTATION
# endif
# define ZN_API ZN_STATIC
#endif /* ZN_STATIC_API */

#if !defined(ZN_API) && defined(_WIN32)
# ifdef ZN_IMPLEMENTATION
#  define ZN_API __declspec(dllexport)
# else
#  define ZN_API __declspec(dllimport)
# endif
#endif /* ZN_API */

#ifndef ZN_API
# define ZN_API extern
#endif /* ZN_API */

/* width follows the target; 32-bit builds still wrap at 49.7 days */
typedef size_t zn_Time;

#ifndef ZN_MAX_EVENTS
# define ZN_MAX_EVENTS 1024
#endif /* ZN_MAX_EVENTS */

/* 1200 never fragments over IPv6; larger datagrams are the caller's */
#ifndef ZN_MAX_UDPSEND
# define ZN_MAX_UDPSEND 1200
#endif /* ZN_MAX_UDPSEND */

#define ZN_PAGESIZE 4096 /* one OS page */

#define ZN_MAX_ADDRLEN   60  /* addr text buffer, NUL included */
#define ZN_MIN_TIMERHEAP 512 /* initial heap capacity; doubles */

#define ZN_TIMER_NOINDEX (~(unsigned)0)        /* sentinel: not in heap */
#define ZN_FOREVER       ((~(zn_Time)0) - 100) /* "wait forever" timeout */
#define ZN_MAX_SIZET     ((~(size_t)0) - 100)  /* size_t max minus slack */

ZN_NS_BEGIN

#define ZN_ERRORS(X)                     \
    X(OK, "No error")                    \
    X(ERROR, "Operation failed")         \
    X(ECLOSE, "socket closed")           \
    X(ECLOSED, "Remote socket closed")   \
    X(EHANGUP, "Remote socket hang up")  \
    X(ESOCKET, "Socket creation error")  \
    X(ECONNECT, "Connect error")         \
    X(EBIND, "Local address bind error") \
    X(EPARAM, "Parameter error")         \
    X(EPOLL, "Register to poll error")   \
    X(ESTATE, "State error")             \
    X(EBUSY, "Another operation performed")

typedef enum zn_Error {
#define X(name, msg) ZN_##name,
    ZN_ERRORS(X)
#undef X
            ZN_ERROR_COUNT
} zn_Error;

typedef struct zn_State  zn_State;
typedef struct zn_Accept zn_Accept;
typedef struct zn_Tcp    zn_Tcp;
typedef struct zn_Udp    zn_Udp;
typedef struct zn_Timer  zn_Timer;

typedef struct zn_PeerInfo {
    char     addr[ZN_MAX_ADDRLEN]; /* numeric text, v4 or v6 */
    unsigned port;                 /* host byte order */
} zn_PeerInfo;

typedef zn_Time zn_TimerHandler(void *ud, zn_Timer *timer, zn_Time delayed);

typedef void zn_PostHandler(void *ud, zn_State *S);
typedef void zn_AcceptHandler(void *ud, zn_Accept *accept, unsigned err,
                              zn_Tcp *tcp);
typedef void zn_ConnectHandler(void *ud, zn_Tcp *tcp, unsigned err);
typedef void zn_SendHandler(void *ud, zn_Tcp *tcp, unsigned err,
                            unsigned count);
typedef void zn_RecvHandler(void *ud, zn_Tcp *tcp, unsigned err,
                            unsigned count);
typedef void zn_RecvFromHandler(void *ud, zn_Udp *udp, unsigned err,
                                unsigned count, const char *addr,
                                unsigned port);

/* znet state routines */

ZN_API void zn_initialize(void);
ZN_API void zn_deinitialize(void);

ZN_API const char *zn_strerror(int err);
ZN_API const char *zn_engine(void);

ZN_API zn_State *zn_newstate(void);
ZN_API void      zn_close(zn_State *S);

ZN_API void *zn_getuserdata(zn_State *S);
ZN_API void  zn_setuserdata(zn_State *S, void *ud);

ZN_API unsigned zn_retain(zn_State *S);
ZN_API unsigned zn_release(zn_State *S);

ZN_API int zn_run(zn_State *S, int mode);
#define ZN_RUN_ONCE  0 /* one blocking poll */
#define ZN_RUN_CHECK 1 /* poll, do not block */
#define ZN_RUN_LOOP  2 /* poll until idle */

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud);

/* znet timer routines */

ZN_API zn_Time zn_time(void);

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *cb, void *ud);
ZN_API void      zn_deltimer(zn_Timer *timer);

ZN_API int  zn_starttimer(zn_Timer *timer, zn_Time delayms);
ZN_API void zn_canceltimer(zn_Timer *timer);

/* znet accept routines */

ZN_API zn_Accept *zn_newaccept(zn_State *S, int type);
ZN_API int        zn_closeaccept(zn_Accept *accept);
ZN_API void       zn_delaccept(zn_Accept *accept);

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port);
ZN_API int zn_accept(zn_Accept *accept, zn_AcceptHandler *cb, void *ud);

/* znet tcp socket routines */

ZN_API zn_Tcp *zn_newtcp(zn_State *S);
ZN_API int     zn_closetcp(zn_Tcp *tcp);
ZN_API void    zn_deltcp(zn_Tcp *tcp);

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info);

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, int type,
                      zn_ConnectHandler *cb, void *ud);

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len,
                   zn_SendHandler *cb, void *ud);
ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb,
                   void *ud);

/* znet udp socket routines */

ZN_API zn_Udp *zn_newudp(zn_State *S, const char *addr, unsigned port);
ZN_API int     zn_closeudp(zn_Udp *udp);
ZN_API void    zn_deludp(zn_Udp *udp);

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len,
                     const char *addr, unsigned port);
ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len,
                       zn_RecvFromHandler *cb, void *ud);

ZN_NS_END

#endif /* znet_h */

/* implementations */

#if defined(ZN_IMPLEMENTATION) && !defined(zn_implemented)
#define zn_implemented

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
        || defined(__DragonFly__) || defined(__MACOSX__)                \
        || (defined(__APPLE__) && defined(__MACH__))
# define ZN_HAVE_KQUEUE
#endif /* ZN_HAVE_KQUEUE */

#if !defined(_WIN32)
# define ZN_HAVE_INFO /* all objects have zn_SocketInfo field */
#endif                /* _WIN32 */

ZN_NS_BEGIN

#define zn_use_mempool
#define zn_use_timer
#include "znet.h"

#if defined(_WIN32)
# define zn_use_platform_win32
# define zn_use_backend_iocp
#else /* POSIX systems */
# define zn_use_platform_posix
# define zn_use_postqueue
# if defined(__linux__) && !defined(ZN_USE_SELECT)
#   define zn_use_resultqueue
#   define zn_use_backend_reactive
#   define zn_use_backend_epoll
# elif defined(ZN_HAVE_KQUEUE) && !defined(ZN_USE_SELECT)
#   define zn_use_resultqueue
#   define zn_use_backend_reactive
#   define zn_use_backend_kqueue
# else
#   define zn_use_backend_select
# endif
#endif /* _WIN32 */
#define zn_use_addr
#define zn_use_state
#include "znet.h"

#define zn_use_api
#include "znet.h"

ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

#ifndef zn_list_h
#define zn_list_h

#define znL_rawset(l, r) (*(void **)&(l) = (void *)(r))
#define znL_entry(T) \
    T *next;         \
    T *prev
#define znL_head(T)    \
    struct T##_hlist { \
        znL_entry(T);  \
    }
#define znL_init(h)  (znL_rawset((h)->prev, h), znL_rawset((h)->next, h))
#define znL_empty(h) ((void *)(h)->prev == (void *)&(h))

#define znL_insert(h, n)                                                     \
    (znL_rawset((n)->next, h), (n)->prev = (h)->prev, (h)->prev->next = (n), \
     (h)->prev = (n))

#define znL_remove(n) ((n)->prev->next = (n)->next, (n)->next->prev = (n)->prev)

#define znL_apply(T, h, stmt)                                       \
    do {                                                            \
        T *cur, *next_ = (h)->next;                                 \
        while (cur = next_, next_ = next_->next, cur != (T *)(h)) { \
            stmt;                                                   \
        }                                                           \
    } while (0)

#define znQ_entry(T) T *next
#define znQ_head(T)        \
    struct T##_qlist {     \
        T *first, **plast; \
    }
#define znQ_init(h)  ((h)->first = NULL, (h)->plast = &(h)->first)
#define znQ_first(h) ((h)->first)
#define znQ_empty(h) ((h)->first == NULL)

#define znQ_enqueue(h, n) \
    ((n)->next = NULL, *(h)->plast = (n), (h)->plast = &(n)->next)

#define znQ_dequeue(h, pn)                                           \
    do {                                                             \
        if (((pn) = (h)->first) != NULL) {                           \
            (h)->first = (h)->first->next;                           \
            if ((h)->plast == &(pn)->next) (h)->plast = &(h)->first; \
        }                                                            \
    } while (0)

#define znQ_apply(T, h, stmt)  \
    do {                       \
        T *cur = (h), *next_;  \
        while (cur) {          \
            next_ = cur->next; \
            stmt;              \
            cur = next_;       \
        }                      \
    } while (0)

#endif /* zn_list_h */

#ifdef zn_use_mempool /* memory pool */
#undef zn_use_mempool

#include <assert.h>
#include <stdlib.h>

typedef struct zn_MemPool {
    void  *pages; /* page chain, link at tail */
    void  *freed; /* free slot list */
    size_t size;  /* slot size, pointer multiple */
} zn_MemPool;

static void znM_put(zn_MemPool *pool, void *obj) {
    *(void **)obj = pool->freed;
    pool->freed = obj;
}

static void znM_init(zn_MemPool *pool, size_t size) {
    size_t sp = sizeof(void *);
    pool->pages = NULL;
    pool->freed = NULL;
    pool->size = size;
    assert(((sp - 1) & sp) == 0);
    assert(size >= sp && size % sp == 0);
    assert(ZN_PAGESIZE / size > 2);
}

static void znM_free(zn_MemPool *pool) {
    const size_t offset = ZN_PAGESIZE - sizeof(void *);
    while (pool->pages != NULL) {
        void *next = *(void **)((char *)pool->pages + offset);
        free(pool->pages), pool->pages = next;
    }
    znM_init(pool, pool->size);
}

static void *znM_get(zn_MemPool *pool) {
    void *obj = pool->freed;
    if (obj == NULL) {
        const size_t offset = ZN_PAGESIZE - sizeof(void *);
        void        *end, *newpage = malloc(ZN_PAGESIZE);
        if (newpage == NULL) return NULL;
        *(void **)((char *)newpage + offset) = pool->pages;
        pool->pages = newpage;
        end = (char *)newpage + (offset / pool->size - 1) * pool->size;
        while (end != newpage) {
            *(void **)end = pool->freed;
            pool->freed = (void **)end;
            end = (char *)end - pool->size;
        }
        return end;
    }
    pool->freed = *(void **)obj;
    return obj;
}

#endif /* zn_use_mempool */

#ifdef zn_use_timer /* need: mempool */
#undef zn_use_timer

#include <string.h>

typedef struct zn_TimerState {
    zn_MemPool timers;   /* pool of zn_Timer */
    zn_Time   *keys;     /* heap order, emit per slot (parallel to nodes) */
    zn_Timer **nodes;    /* heap payload, the timers (parallel to keys) */
    zn_Time    nexttime; /* earliest emit, or FOREVER */
    unsigned   used;     /* heap occupancy */
    unsigned   size;     /* heap capacity */
} zn_TimerState;

struct zn_Timer {
    union {
        zn_Timer *next; /* mempool link, never read */
        void     *ud;   /* handler userdata */
    } u;
    zn_TimerHandler *handler; /* fires when emit passes */
    zn_TimerState   *ts;      /* owning state */
    unsigned         index;   /* heap slot, or NOINDEX */
    zn_Time          start;   /* arm time */
    zn_Time          emit;    /* absolute due time */
};

static int znT_hastimers(zn_TimerState *ts) { return ts->used != 0; }

static void znT_clear(zn_TimerState *ts) {
    znM_free(&ts->timers);
    free(ts->keys), free(ts->nodes);
    memset(ts, 0, sizeof(*ts));
    ts->nexttime = ZN_FOREVER;
}

static void znT_update(zn_TimerState *ts, zn_Time current) {
    if (ts->nexttime > current) return;
    while (ts->used && ts->keys[0] <= current) {
        zn_Timer *timer = ts->nodes[0];
        zn_canceltimer(timer);
        if (timer->handler) {
            zn_Time r = timer->handler(timer->u.ud, timer,
                                       current - timer->start);
            if (r > 0) zn_starttimer(timer, r);
        }
    }
    ts->nexttime = ts->used == 0 ? ZN_FOREVER : ts->keys[0];
}

static zn_Time znT_gettimeout(zn_TimerState *ts, zn_Time current) {
    zn_Time emit = ts->nexttime;
    if (ts->used == 0) return ZN_FOREVER;
    if (emit < current) return 0;
    return emit - current;
}

static int znT_resize(zn_TimerState *ts, size_t size) {
    zn_Time   *keys;
    zn_Timer **nodes;
    size_t     realsize = ZN_MIN_TIMERHEAP;
    while (realsize < size && realsize < ZN_MAX_SIZET / sizeof(zn_Timer *) / 2)
        realsize <<= 1;
    if (realsize < size) return 0;
    keys = (zn_Time *)malloc(realsize * sizeof(zn_Time));
    if (keys == NULL) return 0;
    nodes = (zn_Timer **)malloc(realsize * sizeof(zn_Timer *));
    if (nodes == NULL) return free(keys), 0;
    if (ts->used) {
        memcpy(keys, ts->keys, ts->used * sizeof(zn_Time));
        memcpy(nodes, ts->nodes, ts->used * sizeof(zn_Timer *));
    }
    free(ts->keys), free(ts->nodes);
    ts->keys = keys, ts->nodes = nodes, ts->size = (unsigned)realsize;
    return 1;
}

static zn_Timer *znT_new(zn_TimerState *ts, zn_TimerHandler *cb, void *ud) {
    zn_Timer *timer = (zn_Timer *)znM_get(&ts->timers);
    if (timer == NULL) return NULL;
    timer->u.ud = ud, timer->handler = cb;
    timer->ts = ts;
    timer->index = ZN_TIMER_NOINDEX;
    return timer;
}

ZN_API void zn_deltimer(zn_Timer *timer) {
    zn_canceltimer(timer);
    timer->handler = NULL, znM_put(&timer->ts->timers, timer);
}

ZN_API int zn_starttimer(zn_Timer *timer, zn_Time delayms) {
    zn_TimerState *ts = timer->ts;
    zn_Time        key;
    unsigned       i, p;
    if (delayms >= ZN_FOREVER) return ZN_EPARAM;
    if (timer->index != ZN_TIMER_NOINDEX) zn_canceltimer(timer);
    if (ts->size == ts->used && !znT_resize(ts, ts->size * 2)) return ZN_ERROR;
    i = ts->used++;
    timer->start = zn_time();
    key = timer->emit = timer->start + delayms;
    while (p = (i - 1) >> 1, i > 0 && ts->keys[p] > key) {
        ts->keys[i] = ts->keys[p];
        (ts->nodes[i] = ts->nodes[p])->index = i;
        i = p;
    }
    ts->keys[i] = key, (ts->nodes[i] = timer)->index = i;
    if (i == 0) ts->nexttime = key;
    return ZN_OK;
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    zn_TimerState *ts = timer->ts;
    unsigned       i = timer->index, p, k;
    zn_Time        key;
    if (i == ZN_TIMER_NOINDEX) return;
    timer->index = ZN_TIMER_NOINDEX;
    if (ts->used == 0 || timer == ts->nodes[--ts->used]) {
        if (ts->used == 0) ts->nexttime = ZN_FOREVER;
        return;
    }
    timer = ts->nodes[ts->used];
    key = ts->keys[ts->used];
    while (p = (i - 1) >> 1, i > 0 && ts->keys[p] > key) {
        ts->keys[i] = ts->keys[p];
        (ts->nodes[i] = ts->nodes[p])->index = i;
        i = p;
    }
    while ((k = i * 2 + 1) < ts->used) {
        unsigned c = k + 1;
        if (c < ts->used && ts->keys[c] < ts->keys[k]) k = c;
        if (key <= ts->keys[k]) break;
        ts->keys[i] = ts->keys[k];
        (ts->nodes[i] = ts->nodes[k])->index = i;
        i = k;
    }
    ts->keys[i] = key, (ts->nodes[i] = timer)->index = i;
    ts->nexttime = ts->keys[0];
}

#endif /* zn_use_timer */

#ifdef zn_use_api /* need: state, backend, addr */
#undef zn_use_api

#include <limits.h>

ZN_API unsigned zn_retain(zn_State *S) { return ++S->waitings; }
ZN_API unsigned zn_release(zn_State *S) { return --S->waitings; }
ZN_API void    *zn_getuserdata(zn_State *S) { return S->userdata; }
ZN_API void     zn_setuserdata(zn_State *S, void *ud) { S->userdata = ud; }

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *h, void *ud) {
    if (S->status > ZN_STATUS_READY) return NULL;
    return znT_new(&S->ts, h, ud);
}

/* clang-format off */
ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)
{ *info = tcp->peer_info; }

ZN_API int zn_closeudp(zn_Udp *udp)
{ return znS_valid(udp) ? znB_closeudp(udp) : ZN_OK; }
/* clang-format on */

ZN_API int zn_closeaccept(zn_Accept *acc) { return znB_closeaccept(acc); }
ZN_API int zn_closetcp(zn_Tcp *tcp) { return znB_closetcp(tcp); }

static zn_Time znS_clampms(zn_Time timeout) {
    /* poll APIs take an int: clamp so 64-bit time cannot wrap the wait */
    if (timeout < ZN_FOREVER && timeout > (zn_Time)INT_MAX) return INT_MAX;
    return timeout;
}

static int znS_poll(zn_State *S, zn_Time budget) {
    zn_Time current, timeout;
    int     has_more;
    S->status = ZN_STATUS_IN_RUN;
    znT_update(&S->ts, current = zn_time());
    timeout = znT_gettimeout(&S->ts, current);
    if (budget < timeout) timeout = budget;
    has_more = znB_poll(S, znS_clampms(timeout));
    znT_update(&S->ts, zn_time());
    if (S->status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_READY; /* trigger real close */
        return zn_close(S), 0;
    }
    S->status = ZN_STATUS_READY;
    return has_more || znT_hastimers(&S->ts) || S->waitings > 0;
}

ZN_API const char *zn_strerror(int err) {
    const char *msg = "Unknown error";
    switch (err) {
#define X(name, str) \
case ZN_##name: msg = str; break;
        ZN_ERRORS(X)
#undef X
    }
    return msg;
}

ZN_API zn_State *zn_newstate(void) {
    zn_State *S = (zn_State *)malloc(sizeof(zn_State));
    if (S == NULL) return NULL;
    memset(S, 0, sizeof(*S));
    S->ts.nexttime = ZN_FOREVER;
    znL_init(&S->active_accepts);
    znL_init(&S->active_tcps);
    znL_init(&S->active_udps);
    znM_init(&S->ts.timers, sizeof(zn_Timer));
    znM_init(&S->posts, sizeof(zn_Post));
    znM_init(&S->accepts, sizeof(zn_Accept));
    znM_init(&S->tcps, sizeof(zn_Tcp));
    znM_init(&S->udps, sizeof(zn_Udp));
    if (!znB_init(S)) return free(S), (zn_State *)0;
    return S;
}

ZN_API void zn_close(zn_State *S) {
    int status = S->status;
    if (status == ZN_STATUS_IN_RUN || status == ZN_STATUS_CLOSING_IN_RUN)
        S->status = ZN_STATUS_CLOSING_IN_RUN;
    else {
        /* 0. doesn't allow create new objects */
        S->status = ZN_STATUS_CLOSING;
        /* 1. cancel all operations */
        znT_clear(&S->ts);
        znL_apply(zn_Accept, &S->active_accepts, zn_delaccept(cur));
        znL_apply(zn_Tcp, &S->active_tcps, zn_deltcp(cur));
        znL_apply(zn_Udp, &S->active_udps, zn_deludp(cur));
        znB_close(S);
        /* 2. delete all remaining objects */
        znM_free(&S->posts);
        znM_free(&S->accepts);
        znM_free(&S->tcps);
        znM_free(&S->udps);
        free(S);
    }
}

ZN_API int zn_run(zn_State *S, int mode) {
    int err;
    switch (mode) {
    case ZN_RUN_CHECK: return znS_poll(S, 0);
    case ZN_RUN_ONCE: return znS_poll(S, ZN_FOREVER);
    case ZN_RUN_LOOP:
        while ((err = znS_poll(S, ZN_FOREVER)) > 0) continue;
        return err;
    }
    return -ZN_ERROR;
}

ZN_API zn_Accept *zn_newaccept(zn_State *S, int type) {
    znS_getobject(zn_Accept, S, accept);
    accept->socket = ZN_INVALID_SOCKET;
#ifdef ZN_HAVE_INFO
    accept->info.type = ZN_SOCK_ACCEPT, accept->info.head = accept;
#endif /* ZN_HAVE_INFO */
    accept->type = type;
    return znB_initaccept(accept), accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    if (accept == NULL) return;
    zn_closeaccept(accept);
    if (accept->accept_handler == NULL) znS_putobject(accept);
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port) {
    zn_SockAddr laddr;
    if (accept == NULL) return ZN_EPARAM;
    if (znS_valid(accept)) return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;
    if (!znP_parseaddr(&laddr, addr, port)) return ZN_EPARAM;
    return znB_listen(accept, &laddr);
}

ZN_API int zn_accept(zn_Accept *accept, zn_AcceptHandler *cb, void *ud) {
    int r;
    if (accept == NULL || cb == NULL) return ZN_EPARAM;
    if (!znS_valid(accept)) return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;
    if ((r = znB_accept(accept)) == ZN_OK) {
        accept->accept_handler = cb, accept->accept_ud = ud;
        zn_retain(accept->S);
    }
    return r;
}

ZN_API zn_Tcp *zn_newtcp(zn_State *S) {
    znS_getobject(zn_Tcp, S, tcp);
    tcp->socket = ZN_INVALID_SOCKET;
#ifdef ZN_HAVE_INFO
    tcp->info.type = ZN_SOCK_TCP, tcp->info.head = tcp;
#endif /* ZN_HAVE_INFO */
    return znB_inittcp(tcp), tcp;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    if (tcp == NULL) return;
    zn_closetcp(tcp);
    /* a pending completion still owns its delete */
    if (!tcp->connect_handler && !tcp->recv_handler && !tcp->send_handler)
        znS_putobject(tcp);
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, int type,
                      zn_ConnectHandler *cb, void *ud) {
    zn_SockAddr raddr;
    int         r;
    if (tcp == NULL || cb == NULL) return ZN_EPARAM;
    if (tcp->connect_handler != NULL) return ZN_EBUSY;
    if (znS_valid(tcp)) return ZN_ESTATE;
    if (!znP_parseaddr(&raddr, addr, port)) return ZN_EPARAM;
    if ((r = znB_connect(tcp, &raddr, type)) != ZN_OK) return r;
    assert(strlen(addr) < ZN_MAX_ADDRLEN);
    strcpy(tcp->peer_info.addr, addr);
    tcp->peer_info.port = port;
    tcp->connect_handler = cb, tcp->connect_ud = ud;
    return zn_retain(tcp->S), r;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len,
                   zn_SendHandler *cb, void *ud) {
    int r;
    if (tcp == NULL || cb == NULL || len == 0) return ZN_EPARAM;
    if (!znS_valid(tcp)) return ZN_ESTATE;
    if (tcp->send_handler != NULL) return ZN_EBUSY;
    tcp->send_buffer.buf = (char *)buff;
    tcp->send_buffer.len = len;
    if ((r = znB_send(tcp)) != ZN_OK)
        memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer));
    else {
        tcp->send_handler = cb, tcp->send_ud = ud;
        zn_retain(tcp->S);
    }
    return r;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb,
                   void *ud) {
    int r;
    if (tcp == NULL || cb == NULL || len == 0) return ZN_EPARAM;
    if (!znS_valid(tcp)) return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    tcp->recv_buffer.buf = buff;
    tcp->recv_buffer.len = len;
    if ((r = znB_recv(tcp)) != ZN_OK)
        memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer));
    else {
        tcp->recv_handler = cb, tcp->recv_ud = ud;
        zn_retain(tcp->S);
    }
    return r;
}

ZN_API zn_Udp *zn_newudp(zn_State *S, const char *addr, unsigned port) {
    zn_SockAddr laddr;
    znS_getobject(zn_Udp, S, udp);
    if (!znP_parseaddr(&laddr, addr, port))
        return znS_putobject(udp), (zn_Udp *)0;
    udp->socket = ZN_INVALID_SOCKET;
    if (znB_initudp(udp, &laddr) != ZN_OK)
        return znS_putobject(udp), (zn_Udp *)0;
#ifdef ZN_HAVE_INFO
    udp->info.type = ZN_SOCK_UDP, udp->info.head = udp;
#endif /* ZN_HAVE_INFO */
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    if (udp == NULL) return;
    zn_closeudp(udp);
    if (udp->recv_handler == NULL) znS_putobject(udp);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len,
                     const char *addr, unsigned port) {
    zn_SockAddr raddr;
    if (udp == NULL || len == 0 || len > ZN_MAX_UDPSEND) return ZN_EPARAM;
    if (!znS_valid(udp)) return ZN_ESTATE;
    if (!znP_parseaddr(&raddr, addr, port)) return ZN_EPARAM;
    return znB_sendto(udp, buff, len, &raddr);
}

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len,
                       zn_RecvFromHandler *cb, void *ud) {
    int r;
    if (udp == NULL || len == 0 || cb == NULL) return ZN_EPARAM;
    if (!znS_valid(udp)) return ZN_ESTATE;
    if (udp->recv_handler) return ZN_EBUSY;
    udp->recv_buffer.buf = buff, udp->recv_buffer.len = len;
    if ((r = znB_recvfrom(udp)) != ZN_OK)
        memset(&udp->recv_buffer, 0, sizeof(udp->recv_buffer));
    else {
        udp->recv_handler = cb, udp->recv_ud = ud;
        zn_retain(udp->S);
    }
    return r;
}

#endif /* zn_use_api */

#ifdef zn_use_platform_win32
#undef zn_use_platform_win32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
/* clang-format off */
/* MSWSock.h needs WinSock2.h and Windows.h first */
#include <Windows.h>
#include <WinSock2.h>
#include <WS2Tcpip.h>
#include <MSWSock.h>
/* clang-format on */

#ifdef _MSC_VER
# pragma warning(disable : 4996) /* deprecated stuffs */
# pragma warning(disable : 4127) /* for do {} while(0) */
# pragma comment(lib, "ws2_32")
#endif /* _MSC_VER */

#define ZN_INVALID_SOCKET INVALID_SOCKET

/* clang-format off */
static int znP_pton(int af, const char *src, void *dst)
{ return InetPtonA(af, src, dst) == 1; }

static const char *znP_ntop(int af, const void *src, char *dst, socklen_t sz)
{ return InetNtopA(af, src, dst, (DWORD)sz); }
/* clang-format on */

static int znP_setnodelay(SOCKET socket) {
    BOOL        bEnable = 1;
    const char *p = (const char *)&bEnable;
    return !setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, p, sizeof(BOOL));
}

static int znP_updateacceptinfo(SOCKET server, SOCKET client) {
    return !setsockopt(client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (const char *)&server, sizeof(server));
}

static int znP_setexclusiveaddruse(SOCKET socket) {
    BOOL        exclusive = TRUE;
    const char *p = (const char *)&exclusive;
    return !setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, p,
                       sizeof(BOOL));
}

static int znP_error(int syserr) {
    switch (syserr) {
    case WSAECONNABORTED:
    case WSAECONNREFUSED:
    case WSAECONNRESET: return ZN_EHANGUP;
    default: return ZN_ERROR;
    }
}

ZN_API zn_Time zn_time(void) {
    static LARGE_INTEGER counterFreq;
    static LARGE_INTEGER startTime;
    LARGE_INTEGER        current;
    if (counterFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&counterFreq);
        QueryPerformanceCounter(&startTime);
        return assert(counterFreq.HighPart == 0), 0;
    }
    QueryPerformanceCounter(&current);
    return (zn_Time)((current.QuadPart - startTime.QuadPart) * 1000
                     / counterFreq.LowPart);
}

#endif /* zn_use_platform_win32 */

#ifdef zn_use_platform_posix
#undef zn_use_platform_posix

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef zn_use_backend_epoll
# include <sys/epoll.h>
# include <sys/eventfd.h>
#endif

#ifdef zn_use_backend_kqueue
# include <sys/event.h>
#endif

#ifdef __MACH__
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#endif /* __MACH__ */

#define ZN_INVALID_SOCKET (-1)

#ifdef MSG_NOSIGNAL
# define ZN_NOSIGNAL MSG_NOSIGNAL
#else
# define ZN_NOSIGNAL 0
#endif /* MSG_NOSIGNAL */

/* strict feature macros hide SO_NOSIGPIPE on Darwin; its ABI value is stable */
#if defined(SO_NOSIGPIPE)
# define ZN_SO_NOSIGPIPE SO_NOSIGPIPE
#elif defined(__APPLE__)
# define ZN_SO_NOSIGPIPE 0x1022 /* SO_NOSIGPIPE */
#endif

#define znP_pton inet_pton
#define znP_ntop inet_ntop

typedef enum zn_SocketType {
    ZN_SOCK_ACCEPT,
    ZN_SOCK_TCP,
    ZN_SOCK_UDP
} zn_SocketType;

typedef struct zn_SocketInfo {
    zn_SocketType type; /* which object head points at */
    void         *head; /* zn_Accept, zn_Tcp or zn_Udp */
} zn_SocketInfo;
typedef struct zn_DataBuffer {
    size_t len; /* caller length, in/out */
    char  *buf; /* borrowed, never owned */
} zn_DataBuffer;

/* clang-format off */
static int znP_setnonblock(int socket)
{ return fcntl(socket, F_SETFL, fcntl(socket, F_GETFL) | O_NONBLOCK) == 0; }
/* clang-format on */

static int znP_setnodelay(int socket) {
    int         enable = 1;
    const void *p = &enable;
    return !setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, p, sizeof(int));
}

static int znP_setreuseaddr(int socket) {
    int         reuse_addr = 1;
    const void *p = &reuse_addr;
    return !setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, p, sizeof(int));
}

static int znP_setnosigpipe(int socket) {
#ifdef ZN_SO_NOSIGPIPE
    int         no_sigpipe = 1;
    const void *p = &no_sigpipe;
    return !setsockopt(socket, SOL_SOCKET, ZN_SO_NOSIGPIPE, p, sizeof(int));
#else
    return (void)socket, 0;
#endif /* ZN_SO_NOSIGPIPE */
}

static int znP_error(int syserr) {
    switch (syserr) {
    case EPIPE:
    case ECONNABORTED:
    case ECONNREFUSED:
    case ECONNRESET: return ZN_EHANGUP;
    default: return ZN_ERROR;
    }
}

ZN_API zn_Time zn_time(void) {
    zn_Time r;
#ifdef __MACH__
    static mach_timebase_info_data_t time_info;

    static uint64_t start;
    uint64_t        now = mach_absolute_time();
    if (!time_info.numer)
        return (start = now), (void)mach_timebase_info(&time_info), 0;
    r = (zn_Time)((now - start) * time_info.numer / time_info.denom / 1000000);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    r = (zn_Time)ts.tv_sec * 1000 + (zn_Time)ts.tv_nsec / 1000000;
#endif
    return r;
}

#endif /* zn_use_platform_posix */

#ifdef zn_use_addr /* need: platform */
#undef zn_use_addr

#define znP_family(sa) ((sa)->addr.sa_family)
#define znP_isv6(sa)   (znP_family(sa) == AF_INET6)
#define znP_size(sa)   (znP_isv6(sa) ? sizeof((sa)->ipv6) : sizeof((sa)->ipv4))
#define znP_ipproto(t) ((t) == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP)

typedef union zn_SockAddr {
    struct sockaddr     addr; /* generic view for syscalls */
    struct sockaddr_in  ipv4; /* AF_INET */
    struct sockaddr_in6 ipv6; /* AF_INET6 */
} zn_SockAddr;

static int znP_parseaddr(zn_SockAddr *sa, const char *addr, unsigned port) {
    memset(sa, 0, sizeof(*sa));
    if (port > 0xFFFFu) return 0;
    if (strchr(addr, ':') != 0) {
        sa->ipv6.sin6_family = AF_INET6;
        sa->ipv6.sin6_port = htons((unsigned short)port);
        if (znP_pton(AF_INET6, addr, &sa->ipv6.sin6_addr) != 1) return 0;
    } else {
        sa->ipv4.sin_family = AF_INET;
        sa->ipv4.sin_port = htons((unsigned short)port);
        if (znP_pton(AF_INET, addr, &sa->ipv4.sin_addr) != 1) return 0;
    }
    return znP_family(sa);
}

static void znP_setinfo(const zn_SockAddr *src, zn_PeerInfo *peer_info) {
    memset(peer_info, 0, sizeof(*peer_info));
    if (znP_isv6(src)) {
        znP_ntop(znP_family(src), &src->ipv6.sin6_addr, peer_info->addr,
                 sizeof(peer_info->addr));
        peer_info->port = ntohs(src->ipv6.sin6_port);
    } else {
        znP_ntop(znP_family(src), &src->ipv4.sin_addr, peer_info->addr,
                 sizeof(peer_info->addr));
        peer_info->port = ntohs(src->ipv4.sin_port);
    }
}

#endif /* zn_use_addr */

#ifdef zn_use_state /* zn_State declares, need: mempool, timer */
#undef zn_use_state

typedef enum zn_Status {
    ZN_STATUS_IN_RUN = -1,       /* now in zn_run() */
    ZN_STATUS_READY = 0,         /* not close */
    ZN_STATUS_CLOSING = 1,       /* prepare close */
    ZN_STATUS_CLOSING_IN_RUN = 2 /* prepare close in run() */
} zn_Status;

typedef struct zn_Post   zn_Post;
typedef struct zn_Result zn_Result;

struct zn_State {
    zn_MemPool posts;                   /* zn_Post pool */
    zn_MemPool accepts;                 /* zn_Accept pool */
    zn_MemPool tcps;                    /* zn_Tcp pool */
    zn_MemPool udps;                    /* zn_Udp pool */
    znL_head(zn_Accept) active_accepts; /* every live accept */
    znL_head(zn_Tcp) active_tcps;       /* every live tcp */
    znL_head(zn_Udp) active_udps;       /* every live udp */
    void         *userdata;             /* app data, unused by znet */
    zn_TimerState ts;                   /* timer pool + heap */
    zn_Status     status;               /* lifecycle state; gates zn_new* */
    unsigned      waitings;             /* pending ops, run() waits */
#ifdef zn_use_postqueue
    pthread_mutex_t post_lock;    /* guards post_queue */
    znQ_head(zn_Post) post_queue; /* posts from other threads */
#endif
#ifdef zn_use_resultqueue
    znQ_head(zn_Result) result_queue; /* deferred completions */
#endif
#ifdef zn_use_backend_iocp
    HANDLE           iocp;                   /* completion port */
    CRITICAL_SECTION lock;                   /* guards the posts pool */
    OVERLAPPED_ENTRY entries[ZN_MAX_EVENTS]; /* poll batch buffer */
#elif defined(zn_use_backend_epoll)
    int                epoll;                 /* epoll fd */
    int                eventfd;               /* post signal */
    struct epoll_event events[ZN_MAX_EVENTS]; /* poll batch buffer */
#elif defined(zn_use_backend_kqueue)
    int           kqueue;                /* kqueue fd */
    int           sockpairs[2];          /* [0] wakes [1] for post */
    struct kevent events[ZN_MAX_EVENTS]; /* poll batch buffer */
#else /* select backend */
    int            sockpairs[2];                 /* [0] wakes [1] for post */
    int            nfds;                         /* highest fd for select */
    fd_set         readfds, writefds, exceptfds; /* interest, copied per poll */
    zn_SocketInfo *infos[FD_SETSIZE];            /* fd to poll tag */
#endif
};

#define znS_valid(obj) ((obj)->socket != ZN_INVALID_SOCKET)

#define znS_getobject(T, S, name)                  \
    T *name;                                       \
    do {                                           \
        if (S->status > ZN_STATUS_READY) return 0; \
        name = (T *)znM_get(&S->name##s);          \
        if (name == NULL) return 0;                \
        memset(name, 0, sizeof(T));                \
        znL_insert(&S->active_##name##s, name);    \
        name->S = S;                               \
    } while (0)

#define znS_putobject(name) \
    (znL_remove(name), znM_put(&(name)->S->name##s, name))

#endif /* zn_use_state */

#ifdef zn_use_postqueue /* need: state, list */
#undef zn_use_postqueue

static int znB_signal(zn_State *S);

struct zn_Post {
    zn_Post        *next;    /* queue link */
    zn_State       *S;       /* owning state */
    zn_PostHandler *handler; /* NULL: post is dropped */
    void           *ud;      /* handler userdata */
};

static void znS_initposts(zn_State *S) {
    pthread_mutex_init(&S->post_lock, 0);
    znQ_init(&S->post_queue);
}

static void znS_processposts(zn_State *S) {
    zn_Post *post;
    pthread_mutex_lock(&S->post_lock);
    post = znQ_first(&S->post_queue);
    znQ_init(&S->post_queue);
    pthread_mutex_unlock(&S->post_lock);
    znQ_apply(zn_Post, post, if (cur->handler) cur->handler(cur->ud, cur->S);
              znM_put(&S->posts, cur));
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *post;
    int      r = ZN_ERROR;
    pthread_mutex_lock(&S->post_lock);
    post = (zn_Post *)znM_get(&S->posts);
    if (post != NULL) {
        post->S = S;
        post->handler = cb, post->ud = ud;
        znQ_enqueue(&S->post_queue, post);
        r = znB_signal(S);
    }
    pthread_mutex_unlock(&S->post_lock);
    return r;
}

#endif /* zn_use_postqueue */

#ifdef zn_use_resultqueue /* need: state, list */
#undef zn_use_resultqueue

#define ZN_MAX_RESULT_LOOPS 100

static void znR_dispatch(zn_Result *result);
static void znR_init(zn_State *S) { znQ_init(&S->result_queue); }

struct zn_Result {
    zn_Result *next; /* queue link */
    zn_Tcp    *tcp;  /* which tcp completed */
    int        err;  /* ZN_* result */
};

/* clang-format off */
static void znR_add(zn_State *S, int err, zn_Result *result)
{ result->err = err, znQ_enqueue(&S->result_queue, result); }
/* clang-format on */

static int znR_process(zn_State *S, int max) {
    int count = 0;
    while (!znQ_empty(&S->result_queue) && count++ < max) {
        zn_Result *results = znQ_first(&S->result_queue);
        znQ_init(&S->result_queue);
        znQ_apply(zn_Result, results, znR_dispatch(cur));
    }
    return !znQ_empty(&S->result_queue);
}

#endif /* zn_use_resultqueue */

/* backends */

#ifdef zn_use_backend_iocp
#undef zn_use_backend_iocp

static BOOL znP_started = FALSE;

typedef BOOL(WINAPI *LPGETQUEUEDCOMPLETIONSTATUSEX)(
        _In_ HANDLE              CompletionPort,
        _Out_ LPOVERLAPPED_ENTRY lpCompletionPortEntries, _In_ ULONG ulCount,
        _Out_ PULONG ulNumEntriesRemoved, _In_ DWORD dwMilliseconds,
        _In_ BOOL fAlertable);

static LPGETQUEUEDCOMPLETIONSTATUSEX pGetQueuedCompletionStatusEx;

ZN_API const char *zn_engine(void) { return "IOCP"; }

typedef enum zn_RequestType {
    ZN_TACCEPT,
    ZN_TRECV,
    ZN_TSEND,
    ZN_TCONNECT,
    ZN_TRECVFROM,
    ZN_TSENDTO
} zn_RequestType;

typedef struct zn_Request {
    OVERLAPPED     overlapped; /* must be first */
    zn_RequestType type;       /* routes the completion */
} zn_Request;

struct zn_Post {
    zn_State       *S;       /* owning state */
    zn_PostHandler *handler; /* NULL: post is dropped */
    void           *ud;      /* handler userdata */
};

struct zn_Tcp {
    znL_entry(zn_Tcp);                  /* active-list link */
    zn_State          *S;               /* owning state */
    void              *connect_ud;      /* connect cb userdata */
    zn_ConnectHandler *connect_handler; /* armed connect */
    void              *send_ud;         /* send cb userdata */
    zn_SendHandler    *send_handler;    /* armed send */
    void              *recv_ud;         /* recv cb userdata */
    zn_RecvHandler    *recv_handler;    /* armed recv */
    zn_Request         connect_request; /* overlapped for connect */
    zn_Request         send_request;    /* overlapped for send */
    zn_Request         recv_request;    /* overlapped for recv */
    SOCKET             socket;          /* INVALID once closed */
    zn_PeerInfo        peer_info;       /* peer address and port */
    WSABUF             send_buffer;     /* points at caller buf */
    WSABUF             recv_buffer;     /* points at caller buf */
};

struct zn_Accept {
    znL_entry(zn_Accept);             /* active-list link */
    zn_State         *S;              /* owning state */
    void             *accept_ud;      /* handler userdata */
    zn_AcceptHandler *accept_handler; /* armed until done */
    zn_Request        accept_request; /* AcceptEx overlapped */
    SOCKET            socket;         /* listener */
    SOCKET            client;         /* accepted fd, not yet tcp */
    int               family;         /* v4 or v6 */
    int               type;           /* stream or dgram */
    DWORD             recv_length;    /* addrs length from AcceptEx */
    char              recv_buffer[(sizeof(zn_SockAddr) + 16) * 2]; /* 2 addrs */
};

struct zn_Udp {
    znL_entry(zn_Udp);                 /* active-list link */
    zn_State           *S;             /* owning state */
    void               *recv_ud;       /* handler userdata */
    zn_RecvFromHandler *recv_handler;  /* armed recv */
    zn_Request          recv_request;  /* overlapped for recv */
    SOCKET              socket;        /* INVALID once closed */
    WSABUF              recv_buffer;   /* points at caller buf */
    zn_SockAddr         recv_from;     /* sender, filled on recv */
    INT                 recv_from_len; /* in: capacity, out: len */
};

ZN_API void zn_initialize(void) {
    if (!znP_started) {
        HMODULE kernel32;
        WORD    version = MAKEWORD(2, 2);
        WSADATA d;
        if (WSAStartup(version, &d) != 0) {
            assert(!"can not initialize Windows sockets!");
            abort();
        }
        if ((kernel32 = GetModuleHandleA("KERNEL32.DLL"))) {
            union {
                LPGETQUEUEDCOMPLETIONSTATUSEX f; /* the callable view */
                FARPROC                       v; /* GetProcAddress result */
            } u;
            u.v = GetProcAddress(kernel32, "GetQueuedCompletionStatusEx");
            pGetQueuedCompletionStatusEx = u.f;
        }
        znP_started = TRUE;
    }
}

/* clang-format off */
ZN_API void zn_deinitialize(void)
{ if (znP_started) WSACleanup(), znP_started = FALSE; }
/* clang-format on */

/* tcp */

static int znP_getextension(SOCKET socket, GUID *gid, void *fn) {
    DWORD dwSize = 0;
    return !WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, gid,
                     sizeof(*gid), fn, sizeof(void(PASCAL *)(void)), &dwSize,
                     NULL, NULL);
}

static int znB_tcpfromfamily(zn_Tcp *tcp, int family, int type) {
    SOCKET      fd;
    zn_SockAddr laddr;
    fd = WSASocket(family, type, znP_ipproto(type), NULL, 0,
                   WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    memset(&laddr, 0, sizeof(laddr));
    znP_family(&laddr) = (ADDRESS_FAMILY)family;
    if (bind(fd, &laddr.addr, znP_size(&laddr)) != 0)
        return closesocket(fd), ZN_EBIND;
    if (!CreateIoCompletionPort((HANDLE)fd, tcp->S->iocp, (ULONG_PTR)tcp, 1))
        return closesocket(fd), ZN_EPOLL;
    return (tcp->socket = fd), ZN_OK;
}

static void znB_inittcp(zn_Tcp *tcp) {
    tcp->connect_request.type = ZN_TCONNECT;
    tcp->send_request.type = ZN_TSEND;
    tcp->recv_request.type = ZN_TRECV;
}

static int znB_closetcp(zn_Tcp *tcp) {
    int r = ZN_OK;
    if (znS_valid(tcp)) {
        if (closesocket(tcp->socket) != 0) r = ZN_ERROR;
        tcp->socket = INVALID_SOCKET;
    }
    return r;
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    static LPFN_CONNECTEX lpConnectEx = NULL;

    int         err = znB_tcpfromfamily(tcp, znP_family(addr), type);
    static GUID gid = WSAID_CONNECTEX;
    DWORD       dwLength = 0;
    char        buf[1];
    if (err != ZN_OK) return err;
    /* leave the object reusable, not holding a socket */
    if (!lpConnectEx && !znP_getextension(tcp->socket, &gid, &lpConnectEx))
        return zn_closetcp(tcp), ZN_ECONNECT;
    if (!lpConnectEx(tcp->socket, &addr->addr, znP_size(addr), buf, 0,
                     &dwLength, &tcp->connect_request.overlapped)
        && WSAGetLastError() != ERROR_IO_PENDING)
        return zn_closetcp(tcp), ZN_ECONNECT;
    return ZN_OK;
}

static int znB_send(zn_Tcp *tcp) {
    DWORD dwTemp1 = 0;
    int   err, r;
    r = WSASend(tcp->socket, &tcp->send_buffer, 1, &dwTemp1, 0,
                &tcp->send_request.overlapped, NULL);
    if (r != 0 && (err = WSAGetLastError()) != WSA_IO_PENDING)
        return zn_closetcp(tcp), znP_error(err);
    return ZN_OK;
}

static int znB_recv(zn_Tcp *tcp) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
    int   err, r;
    r = WSARecv(tcp->socket, &tcp->recv_buffer, 1, &dwRecv, &dwFlag,
                &tcp->recv_request.overlapped, NULL);
    if (r != 0 && (err = WSAGetLastError()) != WSA_IO_PENDING)
        return zn_closetcp(tcp), znP_error(err);
    return ZN_OK;
}

static int znB_onconnect(zn_Tcp *tcp, BOOL bSuccess) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler), tcp->connect_handler = NULL;
    if (!znS_valid(tcp)) return znS_putobject(tcp), ZN_OK;
    if (!bSuccess)
        return zn_closetcp(tcp), cb(tcp->connect_ud, tcp, ZN_ERROR), ZN_OK;
    znP_setnodelay(tcp->socket);
    return cb(tcp->connect_ud, tcp, ZN_OK), ZN_OK;
}

static int znB_onsend(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_SendHandler *cb = tcp->send_handler;
    int             err = bSuccess ? ZN_OK : ZN_ERROR;
    assert(tcp->send_handler), tcp->send_handler = NULL;
    if (!znS_valid(tcp)) { /* closed during the operation */
        if (tcp->recv_handler == NULL) znS_putobject(tcp);
        return ZN_OK;
    }
    if (err) zn_closetcp(tcp);
    return cb(tcp->send_ud, tcp, err, dwBytes), ZN_OK;
}

static int znB_onrecv(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvHandler *cb = tcp->recv_handler;
    int             err = ZN_OK;
    assert(tcp->recv_handler), tcp->recv_handler = NULL;
    if (!znS_valid(tcp)) { /* closed during the operation */
        if (tcp->send_handler == NULL) znS_putobject(tcp);
        return ZN_OK;
    }
    if (dwBytes == 0)
        err = ZN_ECLOSED;
    else if (!bSuccess)
        err = ZN_EHANGUP;
    if (err) zn_closetcp(tcp);
    return cb(tcp->recv_ud, tcp, err, dwBytes), ZN_OK;
}

/* accept */

static void znB_initaccept(zn_Accept *accept) {
    accept->accept_request.type = ZN_TACCEPT;
    accept->client = INVALID_SOCKET;
}

static int znB_closeaccept(zn_Accept *accept) {
    int r = ZN_OK;
    if (znS_valid(accept)) {
        if (closesocket(accept->socket) != 0) r = ZN_ERROR;
        accept->socket = INVALID_SOCKET;
    }
    if (accept->client != INVALID_SOCKET) {
        if (closesocket(accept->client) != 0) r = ZN_ERROR;
        accept->client = INVALID_SOCKET;
    }
    return r;
}

static int znB_listen(zn_Accept *accept, zn_SockAddr *addr) {
    SOCKET fd;
    accept->family = znP_family(addr);
    fd = WSASocket(accept->family, accept->type, znP_ipproto(accept->type),
                   NULL, 0, WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    znP_setexclusiveaddruse(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0)
        return closesocket(fd), ZN_EBIND;
    if (accept->type == SOCK_STREAM && listen(fd, SOMAXCONN) != 0)
        return closesocket(fd), ZN_ERROR;
    if (!CreateIoCompletionPort((HANDLE)fd, accept->S->iocp, (ULONG_PTR)accept,
                                1))
        return closesocket(fd), ZN_EPOLL;
    return (accept->socket = fd), ZN_OK;
}

static int znB_accept(zn_Accept *accept) {
    static GUID          gid = WSAID_ACCEPTEX;
    static LPFN_ACCEPTEX lpAcceptEx = NULL;
    SOCKET               fd;
    if (!lpAcceptEx && !znP_getextension(accept->socket, &gid, &lpAcceptEx))
        return ZN_ERROR;
    fd = WSASocket(accept->family, accept->type, znP_ipproto(accept->type),
                   NULL, 0, WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    if (!lpAcceptEx(accept->socket, fd, accept->recv_buffer, 0,
                    sizeof(zn_SockAddr) + 16, sizeof(zn_SockAddr) + 16,
                    &accept->recv_length, &accept->accept_request.overlapped)
        && WSAGetLastError() != ERROR_IO_PENDING)
        return closesocket(fd), ZN_ERROR;
    return (accept->client = fd), ZN_OK;
}

static void znB_failaccept(zn_Accept *accept, zn_AcceptHandler *cb) {
    if (accept->client != INVALID_SOCKET)
        closesocket(accept->client), accept->client = INVALID_SOCKET;
    cb(accept->accept_ud, accept, ZN_ERROR, NULL);
}

static int znB_onaccept(zn_Accept *accept, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID                      gid = WSAID_GETACCEPTEXSOCKADDRS;
    struct sockaddr                 *paddr1 = NULL, *paddr2 = NULL;
    zn_AcceptHandler                *cb = accept->accept_handler;
    int                              tmp1 = 0, tmp2 = 0;
    zn_Tcp                          *tcp;
    accept->accept_handler = NULL;
    if (!znS_valid(accept)) return znS_putobject(accept), ZN_OK;
    assert(cb != NULL); /* one completion per armed accept */
    if (!bSuccess) return znB_failaccept(accept, cb), ZN_OK;
    znP_updateacceptinfo(accept->socket, accept->client);
    if (accept->type == SOCK_STREAM) znP_setnodelay(accept->client);
    if (!lpGetAcceptExSockaddrs
        && !znP_getextension(accept->client, &gid, &lpGetAcceptExSockaddrs))
        return znB_failaccept(accept, cb), ZN_OK;
    lpGetAcceptExSockaddrs(accept->recv_buffer, accept->recv_length,
                           sizeof(zn_SockAddr) + 16, sizeof(zn_SockAddr) + 16,
                           &paddr1, &tmp1, &paddr2, &tmp2);
    tcp = zn_newtcp(accept->S);
    if (tcp == NULL) /* out of memory, or the state is closing */
        return znB_failaccept(accept, cb), ZN_OK;
    tcp->socket = accept->client;
    accept->client = INVALID_SOCKET;
    if (!CreateIoCompletionPort((HANDLE)tcp->socket, tcp->S->iocp,
                                (ULONG_PTR)tcp, 1)) {
        zn_deltcp(tcp);
        return cb(accept->accept_ud, accept, ZN_ERROR, NULL), ZN_OK;
    }
    znP_setinfo((zn_SockAddr *)paddr2, &tcp->peer_info);
    return cb(accept->accept_ud, accept, ZN_OK, tcp), ZN_OK;
}

/* udp */

static int znB_closeudp(zn_Udp *udp) {
    int r = ZN_OK;
    if (closesocket(udp->socket) != 0) r = ZN_ERROR;
    return (udp->socket = ZN_INVALID_SOCKET), r;
}

static int znB_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    SOCKET fd;
    udp->recv_request.type = ZN_TRECVFROM;
    fd = WSASocket(znP_family(addr), SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    if (bind(fd, &addr->addr, znP_size(addr)) != 0)
        return closesocket(fd), ZN_EBIND;
    if (!CreateIoCompletionPort((HANDLE)fd, udp->S->iocp, (ULONG_PTR)udp, 1))
        return closesocket(fd), ZN_EPOLL;
    return (udp->socket = fd), ZN_OK;
}

static int znB_sendto(zn_Udp *udp, const char *buff, unsigned len,
                      zn_SockAddr *addr) {
    int r = sendto(udp->socket, buff, (int)len, 0, &addr->addr, znP_size(addr));
    if (r == SOCKET_ERROR) return znP_error((int)WSAGetLastError());
    return ZN_OK;
}

static int znB_recvfrom(zn_Udp *udp) {
    DWORD dwRecv = 0, dwFlag = 0;
    int   r;
    memset(&udp->recv_from, 0, sizeof(udp->recv_from));
    udp->recv_from_len = sizeof(udp->recv_from);
    r = WSARecvFrom(udp->socket, &udp->recv_buffer, 1, &dwRecv, &dwFlag,
                    &udp->recv_from.addr, &udp->recv_from_len,
                    &udp->recv_request.overlapped, NULL);
    if (r == 0 || WSAGetLastError() == WSA_IO_PENDING) return ZN_OK;
    return ZN_ERROR;
}

static int znB_onrecvfrom(zn_Udp *udp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvFromHandler *cb = udp->recv_handler;
    zn_PeerInfo         info;
    assert(udp->recv_handler);
    udp->recv_handler = NULL;
    if (!znS_valid(udp)) return znS_putobject(udp), ZN_OK;
    if (!bSuccess)
        return cb(udp->recv_ud, udp, ZN_ERROR, dwBytes, NULL, 0), ZN_OK;
    /* a zero-byte datagram is a successful receive too */
    znP_setinfo(&udp->recv_from, &info);
    return cb(udp->recv_ud, udp, ZN_OK, dwBytes, info.addr, info.port), ZN_OK;
}

/* poll */

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *post;
    EnterCriticalSection(&S->lock);
    post = (zn_Post *)znM_get(&S->posts);
    LeaveCriticalSection(&S->lock);
    if (post == NULL) return ZN_ERROR;
    post->S = S;
    post->handler = cb, post->ud = ud;
    if (!PostQueuedCompletionStatus(S->iocp, 0, 0, (LPOVERLAPPED)post)) {
        EnterCriticalSection(&S->lock);
        znM_put(&S->posts, post);
        LeaveCriticalSection(&S->lock);
        return ZN_ERROR;
    }
    return ZN_OK;
}

static int znB_init(zn_State *S) {
    S->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)0,
                                     1);
    InitializeCriticalSection(&S->lock);
    return S->iocp != NULL;
}

static void znB_close(zn_State *S) {
    /* the completions own the remaining deletes */
    while (S->waitings != 0) {
        assert(S->waitings > 0);
        zn_run(S, ZN_RUN_ONCE);
    }
    CloseHandle(S->iocp);
    DeleteCriticalSection(&S->lock);
}

static void znB_dispatch(zn_State *S, BOOL bRet, LPOVERLAPPED_ENTRY pEntry) {
    zn_Request *req = (zn_Request *)pEntry->lpOverlapped;
    ULONG_PTR   upComKey = pEntry->lpCompletionKey;
    DWORD       dwBytes = pEntry->dwNumberOfBytesTransferred;
    zn_release(S);
    switch (req->type) {
    case ZN_TACCEPT: znB_onaccept((zn_Accept *)upComKey, bRet); break;
    case ZN_TRECV: znB_onrecv((zn_Tcp *)upComKey, bRet, dwBytes); break;
    case ZN_TSEND: znB_onsend((zn_Tcp *)upComKey, bRet, dwBytes); break;
    case ZN_TCONNECT: znB_onconnect((zn_Tcp *)upComKey, bRet); break;
    case ZN_TRECVFROM: znB_onrecvfrom((zn_Udp *)upComKey, bRet, dwBytes); break;
    case ZN_TSENDTO: /* do not have this operation */
    default:;
    }
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    BOOL  bRet;
    DWORD ms = timeout >= ZN_FOREVER ? INFINITE : (DWORD)timeout;
    if (pGetQueuedCompletionStatusEx) {
        ULONG i, count;
        bRet = pGetQueuedCompletionStatusEx(S->iocp, S->entries, ZN_MAX_EVENTS,
                                            &count, ms, FALSE);
        if (!bRet) return 0; /* time out */
        for (i = 0; i < count; ++i) {
            DWORD    transfer;
            zn_Post *post;
            if (S->entries[i].lpCompletionKey != 0) {
                bRet = GetOverlappedResult(S->iocp, S->entries[i].lpOverlapped,
                                           &transfer, FALSE);
                znB_dispatch(S, bRet, &S->entries[i]);
            } else {
                post = (zn_Post *)S->entries[i].lpOverlapped;
                if (post->handler) post->handler(post->ud, post->S);
                znM_put(&S->posts, post);
            }
        }
    } else {
        OVERLAPPED_ENTRY entry;
        bRet = GetQueuedCompletionStatus(
                S->iocp, &entry.dwNumberOfBytesTransferred,
                &entry.lpCompletionKey, &entry.lpOverlapped, ms);
        if (!bRet && !entry.lpOverlapped) /* time out */
            return 0;
        znB_dispatch(S, bRet, &entry);
    }
    return 0;
}

#endif /* zn_use_backend_iocp */

#ifdef zn_use_backend_select
#undef zn_use_backend_select

static void znB_inittcp(zn_Tcp *tcp) { (void)tcp; }
static void znB_initaccept(zn_Accept *accept) { (void)accept; }
ZN_API void zn_initialize(void) {}
ZN_API void zn_deinitialize(void) {}

ZN_API const char *zn_engine(void) { return "select"; }

static int znB_signal(zn_State *S) {
    char c = 0;
    return send(S->sockpairs[0], &c, 1, 0) == 1 ? ZN_OK : ZN_ERROR;
}

static void znF_regread(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->readfds);
    FD_SET(fd, &S->exceptfds);
}

static void znF_regwrite(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->writefds);
    FD_SET(fd, &S->exceptfds);
}

static void znF_unregread(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    FD_CLR(fd, &S->readfds);
    FD_CLR(fd, &S->exceptfds);
}

static void znF_unregwrite(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    FD_CLR(fd, &S->writefds);
    FD_CLR(fd, &S->exceptfds);
}

static void znF_unregister(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    S->infos[fd] = NULL;
    FD_CLR(fd, &S->readfds);
    FD_CLR(fd, &S->writefds);
    FD_CLR(fd, &S->exceptfds);
}

struct zn_Tcp {
    znL_entry(zn_Tcp);                  /* active-list link */
    zn_State          *S;               /* owning state */
    void              *connect_ud;      /* connect cb userdata */
    zn_ConnectHandler *connect_handler; /* armed connect */
    void              *send_ud;         /* send cb userdata */
    zn_SendHandler    *send_handler;    /* armed send */
    void              *recv_ud;         /* recv cb userdata */
    zn_RecvHandler    *recv_handler;    /* armed recv */
    int                socket;          /* INVALID once closed */
    zn_SocketInfo      info;            /* poll tag: type + owner */
    zn_PeerInfo        peer_info;       /* peer address and port */
    zn_DataBuffer      send_buffer;     /* caller buf + len */
    zn_DataBuffer      recv_buffer;     /* caller buf + len */
};

struct zn_Accept {
    znL_entry(zn_Accept);             /* active-list link */
    zn_State         *S;              /* owning state */
    void             *accept_ud;      /* handler userdata */
    zn_AcceptHandler *accept_handler; /* armed until done */
    int               socket;         /* listener */
    int               type;           /* stream or dgram */
    zn_SocketInfo     info;           /* poll tag: type + owner */
};

struct zn_Udp {
    znL_entry(zn_Udp);                /* active-list link */
    zn_State           *S;            /* owning state */
    void               *recv_ud;      /* handler userdata */
    zn_RecvFromHandler *recv_handler; /* armed recv */
    int                 socket;       /* INVALID once closed */
    zn_SocketInfo       info;         /* poll tag: type + owner */
    zn_DataBuffer       recv_buffer;  /* caller buf + len */
};

static void znB_disarmaccept(zn_Accept *accept) {
    accept->accept_handler = NULL, zn_release(accept->S);
    znF_unregread(accept->S, accept->socket);
}

static void znB_disarmudp(zn_Udp *udp) {
    udp->recv_handler = NULL, zn_release(udp->S);
    znF_unregread(udp->S, udp->socket);
    memset(&udp->recv_buffer, 0, sizeof(udp->recv_buffer));
}

/* tcp */

/* clang-format off */
static int znB_send(zn_Tcp *tcp)
{ return znF_regwrite(tcp->S, tcp->socket, &tcp->info), ZN_OK; }

static int znB_recv(zn_Tcp *tcp)
{ return znF_regread(tcp->S, tcp->socket, &tcp->info), ZN_OK; }
/* clang-format on */

static zn_Tcp *znB_tcpfromfd(zn_State *S, int fd, zn_SockAddr *raddr) {
    zn_Tcp *tcp;
    znP_setnosigpipe(fd), znP_setnonblock(fd), znP_setnodelay(fd);
    if ((tcp = zn_newtcp(S)) == NULL) return close(fd), (zn_Tcp *)0;
    /* the caller closes anything larger */
    assert(fd < FD_SETSIZE), tcp->socket = fd;
    return znP_setinfo(raddr, &tcp->peer_info), tcp;
}

static int znB_closetcp(zn_Tcp *tcp) {
    int r = ZN_OK;
    if (tcp->connect_handler) zn_release(tcp->S);
    if (tcp->send_handler) zn_release(tcp->S);
    if (tcp->recv_handler) zn_release(tcp->S);
    tcp->connect_handler = NULL;
    tcp->send_handler = NULL;
    tcp->recv_handler = NULL;
    if (znS_valid(tcp)) {
        znF_unregister(tcp->S, tcp->socket);
        if (close(tcp->socket) != 0) r = ZN_ERROR;
        tcp->socket = ZN_INVALID_SOCKET;
    }
    return r;
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    int r, fd = socket(znP_family(addr), type, 0);
    if (fd < 0) return ZN_ESOCKET;
    if (fd >= FD_SETSIZE) return close(fd), ZN_ESOCKET;
    if (tcp->S->nfds < fd) tcp->S->nfds = fd;
    znP_setnonblock(fd), znP_setnosigpipe(fd);
    r = connect(fd, &addr->addr, znP_size(addr));
    if (r != 0 && errno != EINPROGRESS) return close(fd), ZN_ECONNECT;
    tcp->socket = fd;
    return znF_regwrite(tcp->S, fd, &tcp->info), ZN_OK;
}

static int znB_onconnect(zn_Tcp *tcp, int err) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    socklen_t          len = (socklen_t)sizeof(err);
    tcp->connect_handler = NULL;
    if (!znS_valid(tcp)) return ZN_OK;
    zn_release(tcp->S);
    znF_unregwrite(tcp->S, tcp->socket);
    /* a refused connect is also write-ready: SO_ERROR holds the verdict */
    if (getsockopt(tcp->socket, SOL_SOCKET, SO_ERROR, (void *)&err, &len))
        err = 0;
    if (err) return zn_closetcp(tcp), cb(tcp->connect_ud, tcp, ZN_ERROR), ZN_OK;
    znP_setnodelay(tcp->socket);
    return cb(tcp->connect_ud, tcp, ZN_OK), ZN_OK;
}

static void znB_finishsend(zn_Tcp *tcp) {
    znF_unregwrite(tcp->S, tcp->socket);
    tcp->send_handler = NULL, zn_release(tcp->S);
    memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer));
}

static void znB_finishrecv(zn_Tcp *tcp) {
    znF_unregread(tcp->S, tcp->socket);
    tcp->recv_handler = NULL, zn_release(tcp->S);
    memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer));
}

static int znB_onsend(zn_Tcp *tcp, int err) {
    zn_SendHandler *cb = tcp->send_handler;
    zn_DataBuffer   b = tcp->send_buffer;
    int             bytes, syserr = 0;
    if (cb == NULL) return ZN_OK; /* closed by an earlier callback */
    if (err) {
        znB_finishsend(tcp), zn_closetcp(tcp);
        return cb(tcp->send_ud, tcp, ZN_ERROR, 0), ZN_OK;
    }
    bytes = send(tcp->socket, b.buf, b.len, ZN_NOSIGNAL);
    if (bytes < 0) syserr = errno;
    /* no room yet: park like the reactive backends, next report retries */
    if (syserr == EINTR || syserr == EAGAIN || syserr == EWOULDBLOCK)
        return ZN_OK;
    znB_finishsend(tcp);
    if (bytes < 0) {
        zn_closetcp(tcp);
        return cb(tcp->send_ud, tcp, znP_error(syserr), 0), ZN_OK;
    }
    return cb(tcp->send_ud, tcp, ZN_OK, bytes), ZN_OK;
}

static int znB_onrecv(zn_Tcp *tcp, int err) {
    zn_RecvHandler *cb = tcp->recv_handler;
    zn_DataBuffer   b = tcp->recv_buffer;
    int             bytes, syserr = 0, code;
    if (cb == NULL) return ZN_OK; /* closed by an earlier callback */
    if (err) {
        znB_finishrecv(tcp), zn_closetcp(tcp);
        return cb(tcp->recv_ud, tcp, ZN_ERROR, 0), ZN_OK;
    }
    bytes = recv(tcp->socket, b.buf, b.len, 0);
    if (bytes < 0) syserr = errno;
    /* no data yet: park like the reactive backends, next report retries */
    if (syserr == EINTR || syserr == EAGAIN || syserr == EWOULDBLOCK)
        return ZN_OK;
    znB_finishrecv(tcp);
    if (bytes > 0) return cb(tcp->recv_ud, tcp, ZN_OK, bytes), ZN_OK;
    zn_closetcp(tcp);
    code = bytes == 0 ? ZN_ECLOSED : znP_error(syserr);
    return cb(tcp->recv_ud, tcp, code, 0), ZN_OK;
}

/* accept */

/* clang-format off */
static int znB_accept(zn_Accept *accept)
{ return znF_regread(accept->S, accept->socket, &accept->info), ZN_OK; }
/* clang-format on */

static int znB_closeaccept(zn_Accept *accept) {
    int r = ZN_OK;
    if (accept->accept_handler) zn_release(accept->S);
    accept->accept_handler = NULL;
    if (znS_valid(accept)) {
        znF_unregister(accept->S, accept->socket);
        if (close(accept->socket) != 0) r = ZN_ERROR;
        accept->socket = -1;
    }
    return r;
}

static int znB_listen(zn_Accept *accept, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), accept->type, znP_ipproto(accept->type));
    if (fd < 0) return ZN_ESOCKET;
    if (accept->S->nfds < fd) accept->S->nfds = fd;
    znP_setnonblock(fd), znP_setreuseaddr(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    if (accept->type == SOCK_STREAM && listen(fd, SOMAXCONN) != 0)
        return close(fd), ZN_ERROR;
    return (accept->socket = fd), ZN_OK;
}

static int znB_onaccept(zn_Accept *a, int err) {
    zn_AcceptHandler *cb = a->accept_handler;
    zn_SockAddr       raddr;
    socklen_t         size = sizeof(raddr);
    zn_Tcp           *tcp;
    int               fd, syserr, again;
    if (cb == NULL) return ZN_OK; /* closed by an earlier callback */
    if (err)
        return znB_disarmaccept(a), cb(a->accept_ud, a, ZN_ERROR, NULL), ZN_OK;
    fd = accept(a->socket, &raddr.addr, &size);
    if (fd < 0) {
        syserr = errno;
        /* nothing was lost; the next report retries */
        if (syserr == EINTR || syserr == ECONNABORTED) return ZN_OK;
        /* EAGAIN: the report and the queue disagreed; surface it once */
        znB_disarmaccept(a);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(a->accept_ud, a, again ? ZN_ERROR : znP_error(syserr), NULL);
        return ZN_OK;
    }
    /* select can never track this connection */
    if (fd >= FD_SETSIZE) return close(fd), ZN_OK;
    znB_disarmaccept(a);
    tcp = znB_tcpfromfd(a->S, fd, &raddr);
    if (tcp == NULL) return cb(a->accept_ud, a, ZN_ERROR, NULL), ZN_OK;
    if (a->S->nfds < fd) a->S->nfds = fd;
    return cb(a->accept_ud, a, ZN_OK, tcp), ZN_OK;
}

/* udp */

static int znB_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;
    znP_setnonblock(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    return (udp->socket = fd), ZN_OK;
}

static int znB_closeudp(zn_Udp *udp) {
    int r = ZN_OK;
    znF_unregister(udp->S, udp->socket);
    if (udp->recv_handler) zn_release(udp->S);
    udp->recv_handler = NULL;
    if (close(udp->socket) != 0) r = ZN_ERROR;
    return (udp->socket = ZN_INVALID_SOCKET), r;
}

static int znB_sendto(zn_Udp *udp, const char *buff, unsigned len,
                      zn_SockAddr *addr) {
    int r = sendto(udp->socket, buff, len, 0, &addr->addr, znP_size(addr));
    return r < 0 ? znP_error(errno) : ZN_OK;
}

static int znB_recvfrom(zn_Udp *udp) {
    znF_regread(udp->S, udp->socket, &udp->info);
    if (udp->S->nfds < udp->socket) udp->S->nfds = udp->socket;
    return ZN_OK;
}

static int znB_onrecvfrom(zn_Udp *udp, int err) {
    zn_DataBuffer       b = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    zn_SockAddr         raddr;
    zn_PeerInfo         info;
    socklen_t           size;
    int                 bytes, syserr, again;
    if (cb == NULL) return ZN_OK; /* closed by an earlier callback */
    if (err) {
        znB_disarmudp(udp), cb(udp->recv_ud, udp, ZN_ERROR, 0, NULL, 0);
        return ZN_OK;
    }
    memset(&raddr, 0, size = sizeof(raddr));
    bytes = recvfrom(udp->socket, b.buf, b.len, 0, &raddr.addr, &size);
    if (bytes < 0) {
        syserr = errno;
        if (syserr == EINTR) return ZN_OK; /* nothing was lost; next retries */
        znB_disarmudp(udp);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(udp->recv_ud, udp, again ? ZN_ERROR : znP_error(syserr), 0, NULL, 0);
        return ZN_OK;
    }
    znP_setinfo(&raddr, &info);
    znB_disarmudp(udp);
    return cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port), ZN_OK;
}

/* poll */

static int znB_dispatch(zn_State *S, int fd, int setno) {
    zn_SocketInfo *info;
    zn_Tcp        *tcp;
    int            err = setno == 2;
    if (fd < 0 || fd >= FD_SETSIZE) return ZN_OK;
    if (fd == S->sockpairs[1]) { /* post */
        char buff[ZN_PAGESIZE];
        while (recv(fd, buff, ZN_PAGESIZE, 0) > 0) continue;
        return znS_processposts(S), ZN_OK;
    }
    info = S->infos[fd];
    if (info == NULL) return ZN_OK; /* closed by an earlier callback */
    switch (info->type) {
    case ZN_SOCK_ACCEPT: znB_onaccept((zn_Accept *)info->head, err); break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler) {
            znB_onconnect(tcp, err);
            break;
        }
        if (tcp->send_handler && setno != 0) znB_onsend(tcp, err);
        if (tcp->recv_handler && setno != 1) znB_onrecv(tcp, err);
        break;
    case ZN_SOCK_UDP: znB_onrecvfrom((zn_Udp *)info->head, err); break;
    }
    return ZN_OK;
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    fd_set         readfds = S->readfds;
    fd_set         writefds = S->writefds;
    fd_set         exceptfds = S->exceptfds;
    struct timeval tv, *ptv = NULL;
    int            i, r;
    if (timeout < ZN_FOREVER) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = timeout % 1000 * 1000;
        ptv = &tv;
    }
    r = select(S->nfds + 1, &readfds, &writefds, &exceptfds, ptv);
    /* an interrupted select leaves the sets undefined */
    if (r < 0) return 0;
    for (i = 0; i <= S->nfds; ++i) {
        if (FD_ISSET(i, &readfds)) znB_dispatch(S, i, 0);
        if (FD_ISSET(i, &writefds)) znB_dispatch(S, i, 1);
        if (FD_ISSET(i, &exceptfds)) znB_dispatch(S, i, 2);
    }
    return 0;
}

static int znB_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0) return 0;
    if (S->sockpairs[1] >= FD_SETSIZE)
        return close(S->sockpairs[0]), close(S->sockpairs[1]), 0;
    znS_initposts(S);
    FD_ZERO(&S->readfds);
    FD_ZERO(&S->writefds);
    FD_ZERO(&S->exceptfds);
    FD_SET(S->sockpairs[1], &S->readfds);
    if (S->nfds < S->sockpairs[1]) S->nfds = S->sockpairs[1];
    return znP_setnonblock(S->sockpairs[1]), 1;
}

static void znB_close(zn_State *S) {
    znS_processposts(S);
    close(S->sockpairs[0]), close(S->sockpairs[1]);
}

#endif /* zn_use_backend_select */

#ifdef zn_use_backend_reactive /* reactive model supporting edge triggering */
#undef zn_use_backend_reactive

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info);
static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info);
static int znF_unregister(zn_State *S, int fd);

ZN_API void zn_initialize(void) {}
ZN_API void zn_deinitialize(void) {}

static int  znB_accept(zn_Accept *accept) { return (void)accept, ZN_OK; }
static int  znB_recvfrom(zn_Udp *udp) { return (void)udp, ZN_OK; }
static void znB_initaccept(zn_Accept *accept) { (void)accept; }

struct zn_Tcp {
    znL_entry(zn_Tcp);                  /* active-list link */
    zn_State          *S;               /* owning state */
    void              *connect_ud;      /* connect cb userdata */
    zn_ConnectHandler *connect_handler; /* armed connect */
    void              *send_ud;         /* send cb userdata */
    zn_SendHandler    *send_handler;    /* armed send */
    void              *recv_ud;         /* recv cb userdata */
    zn_RecvHandler    *recv_handler;    /* armed recv */
    int                socket;          /* INVALID once closed */
    unsigned           can_read : 1;    /* 0 = parked on EAGAIN */
    unsigned           can_write : 1;   /* 0 = parked on EAGAIN */
    zn_SocketInfo      info;            /* poll tag: type + owner */
    zn_Result          send_result;     /* embedded completion */
    zn_Result          recv_result;     /* embedded completion */
    zn_PeerInfo        peer_info;       /* peer address and port */
    zn_DataBuffer      send_buffer;     /* caller buf + len */
    zn_DataBuffer      recv_buffer;     /* caller buf + len */
};

struct zn_Accept {
    znL_entry(zn_Accept);             /* active-list link */
    zn_State         *S;              /* owning state */
    void             *accept_ud;      /* handler userdata */
    zn_AcceptHandler *accept_handler; /* armed until done */
    int               socket;         /* listener */
    int               type;           /* stream or dgram */
    zn_SocketInfo     info;           /* poll tag: type + owner */
};

struct zn_Udp {
    znL_entry(zn_Udp);                /* active-list link */
    zn_State           *S;            /* owning state */
    void               *recv_ud;      /* handler userdata */
    zn_RecvFromHandler *recv_handler; /* armed recv */
    int                 socket;       /* INVALID once closed */
    zn_SocketInfo       info;         /* poll tag: type + owner */
    zn_DataBuffer       recv_buffer;  /* caller buf + len */
};

/* clang-format off */
static void znB_disarmaccept(zn_Accept *accept)
{ accept->accept_handler = NULL, zn_release(accept->S); }
/* clang-format on */

static void znB_disarmudp(zn_Udp *udp) {
    udp->recv_handler = NULL, zn_release(udp->S);
    memset(&udp->recv_buffer, 0, sizeof(udp->recv_buffer));
}

/* tcp */

static zn_Tcp *znB_tcpfromfd(zn_State *S, int fd, zn_SockAddr *raddr) {
    zn_Tcp *tcp = zn_newtcp(S);
    if (tcp == NULL) return close(fd), (zn_Tcp *)0;
    tcp->socket = fd;
    znP_setnonblock(fd), znP_setnosigpipe(fd), znP_setnodelay(fd);
    tcp->can_read = tcp->can_write = 1;
    if (!znF_regall(tcp->S, fd, &tcp->info)) return zn_deltcp(tcp), (zn_Tcp *)0;
    return znP_setinfo(raddr, &tcp->peer_info), tcp;
}

static void znB_inittcp(zn_Tcp *tcp) {
    tcp->can_read = tcp->can_write = 1;
    tcp->send_result.tcp = tcp, tcp->recv_result.tcp = tcp;
}

static int znB_closetcp(zn_Tcp *tcp) {
    int r = ZN_OK;
    if (tcp->connect_handler) zn_release(tcp->S);
    tcp->connect_handler = NULL;
    /* parked: owns its retain, never gets a result */
    if (tcp->send_handler != NULL && !tcp->can_write) {
        zn_release(tcp->S), tcp->send_handler = NULL;
        memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer));
    }
    if (tcp->recv_handler != NULL && !tcp->can_read) {
        zn_release(tcp->S), tcp->recv_handler = NULL;
        memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer));
    }
    if (znS_valid(tcp)) {
        znF_unregister(tcp->S, tcp->socket);
        if (close(tcp->socket) != 0) r = ZN_ERROR;
        tcp->socket = ZN_INVALID_SOCKET;
    }
    return r;
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    int r, fd;
    /* a handler left behind by close still owns a result slot here */
    if (tcp->send_handler || tcp->recv_handler) return ZN_EBUSY;
    if ((fd = socket(znP_family(addr), type, 0)) < 0) return ZN_ESOCKET;
    znP_setnonblock(fd), znP_setnosigpipe(fd); /* before the first send() */
    r = connect(fd, &addr->addr, znP_size(addr));
    if (r != 0 && errno != EINPROGRESS) return close(fd), ZN_ECONNECT;
    if (!znF_regall(tcp->S, fd, &tcp->info)) return close(fd), ZN_EPOLL;
    return (tcp->socket = fd), ZN_OK;
}

static int znB_send(zn_Tcp *tcp) {
    zn_DataBuffer *b = &tcp->send_buffer;
    int            bytes;
    if (!tcp->can_write) return ZN_OK;
    if ((bytes = send(tcp->socket, b->buf, b->len, ZN_NOSIGNAL)) >= 0)
        b->len = bytes, znR_add(tcp->S, ZN_OK, &tcp->send_result);
    else if (errno != EAGAIN && errno != EWOULDBLOCK)
        znR_add(tcp->S, znP_error(errno), &tcp->send_result);
    else
        tcp->can_write = 0;
    return ZN_OK;
}

static int znB_recv(zn_Tcp *tcp) {
    zn_DataBuffer *b = &tcp->recv_buffer;
    int            bytes;
    if (!tcp->can_read) return ZN_OK;
    if ((bytes = recv(tcp->socket, b->buf, b->len, 0)) > 0)
        b->len = bytes, znR_add(tcp->S, ZN_OK, &tcp->recv_result);
    else if (bytes == 0)
        znR_add(tcp->S, ZN_ECLOSED, &tcp->recv_result);
    else if (errno != EAGAIN && errno != EWOULDBLOCK)
        znR_add(tcp->S, znP_error(errno), &tcp->recv_result);
    else
        tcp->can_read = 0;
    return ZN_OK;
}

static int znB_retrysend(zn_Tcp *tcp) {
    zn_SendHandler *cb = tcp->send_handler;
    zn_DataBuffer   b = tcp->send_buffer;
    if (cb == NULL) return (tcp->can_write = 1), ZN_OK; /* keep the edge */
    if (tcp->can_write) return ZN_OK;  /* result already queued */
    if (!znS_valid(tcp)) return ZN_OK; /* close released it */
    tcp->send_handler = NULL, tcp->can_write = 1;
    zn_release(tcp->S);
    return zn_send(tcp, b.buf, b.len, cb, tcp->send_ud), ZN_OK;
}

static int znB_retryrecv(zn_Tcp *tcp) {
    zn_RecvHandler *cb = tcp->recv_handler;
    zn_DataBuffer   b = tcp->recv_buffer;
    if (cb == NULL) return (tcp->can_read = 1), ZN_OK; /* keep the edge */
    if (tcp->can_read) return ZN_OK;   /* result already queued */
    if (!znS_valid(tcp)) return ZN_OK; /* close released it */
    tcp->recv_handler = NULL, tcp->can_read = 1;
    zn_release(tcp->S);
    return zn_recv(tcp, b.buf, b.len, cb, tcp->recv_ud), ZN_OK;
}

static void znR_dispatch(zn_Result *result) {
    zn_Tcp *tcp = result->tcp;
    zn_release(tcp->S);
    if (result == &tcp->send_result) {
        zn_DataBuffer   b = tcp->send_buffer;
        zn_SendHandler *cb = tcp->send_handler;
        assert(tcp->send_handler != NULL);
        tcp->send_handler = NULL;
        memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer));
        if (!znS_valid(tcp)) { /* closed: suppress the callback */
            if (tcp->recv_handler == NULL) znS_putobject(tcp);
        } else if (result->err == ZN_OK)
            cb(tcp->send_ud, tcp, ZN_OK, b.len);
        else {
            zn_closetcp(tcp);
            cb(tcp->send_ud, tcp, result->err, 0);
        }
    } else {
        zn_DataBuffer   b = tcp->recv_buffer;
        zn_RecvHandler *cb = tcp->recv_handler;
        assert(tcp->recv_handler != NULL);
        tcp->recv_handler = NULL;
        memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer));
        if (!znS_valid(tcp)) { /* closed: suppress the callback */
            if (tcp->send_handler == NULL) znS_putobject(tcp);
        } else if (result->err == ZN_OK)
            cb(tcp->recv_ud, tcp, ZN_OK, b.len);
        else {
            zn_closetcp(tcp);
            cb(tcp->recv_ud, tcp, result->err, 0);
        }
    }
}

/* accept */

static int znB_closeaccept(zn_Accept *accept) {
    int r = ZN_OK;
    if (accept->accept_handler) zn_release(accept->S);
    accept->accept_handler = NULL;
    if (znS_valid(accept)) {
        znF_unregister(accept->S, accept->socket);
        if (close(accept->socket) != 0) r = ZN_ERROR;
        accept->socket = -1;
    }
    return r;
}

static int znB_listen(zn_Accept *accept, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), accept->type, znP_ipproto(accept->type));
    if (fd < 0) return ZN_ESOCKET;
    if (!znF_regread(accept->S, fd, &accept->info)) return close(fd), ZN_EPOLL;
    /* accept() must never block the loop: readiness can go stale */
    znP_setnonblock(fd), znP_setreuseaddr(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    if (accept->type == SOCK_STREAM && listen(fd, SOMAXCONN) != 0)
        return close(fd), ZN_ERROR;
    return (accept->socket = fd), ZN_OK;
}

/* udp */

static int znB_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;
    znP_setnonblock(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    if (!znF_regread(udp->S, fd, &udp->info)) return close(fd), ZN_EPOLL;
    return (udp->socket = fd), ZN_OK;
}

static int znB_closeudp(zn_Udp *udp) {
    int r = ZN_OK;
    znF_unregister(udp->S, udp->socket);
    if (udp->recv_handler) zn_release(udp->S);
    udp->recv_handler = NULL;
    if (close(udp->socket) != 0) r = ZN_ERROR;
    return (udp->socket = ZN_INVALID_SOCKET), r;
}

static int znB_sendto(zn_Udp *udp, const char *buff, unsigned len,
                      zn_SockAddr *addr) {
    int r = sendto(udp->socket, buff, len, 0, &addr->addr, znP_size(addr));
    return r < 0 ? znP_error(errno) : ZN_OK;
}

#endif /* zn_use_backend_reactive */

#ifdef zn_use_backend_epoll
#undef zn_use_backend_epoll

ZN_API const char *zn_engine(void) { return "epoll"; }

/* clang-format off */
static int znB_signal(zn_State *S)
{ return eventfd_write(S->eventfd, (eventfd_t)1) == 0 ? ZN_OK : ZN_ERROR; }
/* clang-format on */

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    event.events = EPOLLIN, event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    event.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_unregister(zn_State *S, int fd) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    return epoll_ctl(S->epoll, EPOLL_CTL_DEL, fd, &event) == 0;
}

static int znB_onconnect(zn_Tcp *tcp, int eventmask) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    tcp->connect_handler = NULL;
    if (!znS_valid(tcp)) return ZN_OK;
    zn_release(tcp->S);
    if ((eventmask & (EPOLLERR | EPOLLHUP)) != 0)
        return zn_closetcp(tcp), cb(tcp->connect_ud, tcp, ZN_ERROR), ZN_OK;
    if ((eventmask & EPOLLOUT) == 0) return ZN_OK;
    znP_setnodelay(tcp->socket);
    cb(tcp->connect_ud, tcp, ZN_OK);
    /* also wakes a send/recv armed before the connect finished */
    return znB_retryrecv(tcp), znB_retrysend(tcp), ZN_OK;
}

static int znB_onevent(zn_Tcp *tcp, int eventmask) {
    int err = (eventmask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    /* err names no direction: only the parked syscall reports it */
    if (err || (eventmask & EPOLLIN) != 0) znB_retryrecv(tcp);
    if (err || (eventmask & EPOLLOUT) != 0) znB_retrysend(tcp);
    return ZN_OK;
}

static int znB_onaccept(zn_Accept *a, int eventmask) {
    zn_AcceptHandler *cb = a->accept_handler;
    zn_SockAddr       raddr;
    socklen_t         size = sizeof(raddr);
    int               fd, syserr, again;
    zn_Tcp           *tcp;
    if (cb == NULL || !znS_valid(a)) return ZN_OK; /* not armed / closed */
    if ((eventmask & (EPOLLERR | EPOLLHUP)) != 0)
        return znB_disarmaccept(a), cb(a->accept_ud, a, ZN_ERROR, NULL), ZN_OK;
    if ((eventmask & EPOLLIN) == 0) return ZN_OK;
    fd = accept(a->socket, &raddr.addr, &size);
    if (fd < 0) {
        syserr = errno;
        /* nothing was lost; the next report retries */
        if (syserr == EINTR || syserr == ECONNABORTED) return ZN_OK;
        /* EAGAIN: the report and the queue disagreed; surface it once */
        znB_disarmaccept(a);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(a->accept_ud, a, again ? ZN_ERROR : znP_error(syserr), NULL);
        return ZN_OK;
    }
    znB_disarmaccept(a);
    tcp = znB_tcpfromfd(a->S, fd, &raddr);
    return cb(a->accept_ud, a, tcp ? ZN_OK : ZN_ERROR, tcp), ZN_OK;
}

static int znB_onrecvfrom(zn_Udp *udp, int eventmask) {
    zn_DataBuffer       b = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    int                 bytes, syserr, again;
    zn_SockAddr         raddr;
    zn_PeerInfo         info;
    socklen_t           size;
    if (cb == NULL || !znS_valid(udp)) return ZN_OK; /* not armed */
    if ((eventmask & (EPOLLERR | EPOLLHUP)) != 0) {
        znB_disarmudp(udp);
        return cb(udp->recv_ud, udp, ZN_ERROR, 0, NULL, 0), ZN_OK;
    }
    if ((eventmask & EPOLLIN) == 0) return ZN_OK;
    memset(&raddr, 0, size = sizeof(raddr));
    bytes = recvfrom(udp->socket, b.buf, b.len, 0, &raddr.addr, &size);
    if (bytes < 0) {
        syserr = errno;
        if (syserr == EINTR) return ZN_OK; /* nothing was lost; next retries */
        znB_disarmudp(udp);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(udp->recv_ud, udp, again ? ZN_ERROR : znP_error(syserr), 0, NULL, 0);
        return ZN_OK;
    }
    znP_setinfo(&raddr, &info);
    znB_disarmudp(udp);
    return cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port), ZN_OK;
}

static int znB_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_SocketInfo *info = (zn_SocketInfo *)evt->data.ptr;
    int            eventmask = evt->events;
    eventfd_t      value;
    zn_Tcp        *tcp;
    if (info == NULL) /* post */
        return eventfd_read(S->eventfd, &value), znS_processposts(S), ZN_OK;
    switch (info->type) {
    case ZN_SOCK_ACCEPT:
        znB_onaccept((zn_Accept *)info->head, eventmask);
        break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler)
            znB_onconnect(tcp, eventmask);
        else if (tcp->send_handler || tcp->recv_handler)
            znB_onevent(tcp, eventmask);
        break;
    case ZN_SOCK_UDP: znB_onrecvfrom((zn_Udp *)info->head, eventmask); break;
    }
    return ZN_OK;
}

static int znB_timeoutms(zn_Time timeout) {
    if (timeout >= ZN_FOREVER) return -1;
    /* epoll takes an int: a plain cast wraps past INT_MAX into "forever" */
    return timeout > (zn_Time)INT_MAX ? INT_MAX : (int)timeout;
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    int i, r, ms = znB_timeoutms(timeout);
    if (znR_process(S, ZN_MAX_RESULT_LOOPS)) return 1;
    r = epoll_wait(S->epoll, S->events, ZN_MAX_EVENTS, ms);
    if (r < 0 && errno != EINTR) return 0; /* error out */
    for (i = 0; i < r; ++i) znB_dispatch(S, &S->events[i]);
    return znR_process(S, ZN_MAX_RESULT_LOOPS);
}

static int znB_init(zn_State *S) {
    if ((S->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0) return 0;
    S->epoll = epoll_create(ZN_MAX_EVENTS);
    if (S->epoll < 0 || !znF_regread(S, S->eventfd, NULL)) {
        if (S->epoll >= 0) close(S->epoll);
        return close(S->eventfd), 0;
    }
    return znS_initposts(S), znR_init(S), 1;
}

static void znB_close(zn_State *S) {
    znS_processposts(S), znR_process(S, INT_MAX);
    assert(znQ_empty(&S->result_queue));
    close(S->eventfd), close(S->epoll);
}

#endif /* zn_use_backend_epoll */

#ifdef zn_use_backend_kqueue
#undef zn_use_backend_kqueue

/* clang-format off */
static int znB_signal(zn_State *S)
{ char c = 0; return send(S->sockpairs[0], &c, 1, 0) == 1 ? ZN_OK : ZN_ERROR; }
/* clang-format on */

ZN_API const char *zn_engine(void) { return "kqueue"; }

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, info);
    return kevent(S->kqueue, &kev, 1, NULL, 0, NULL) == 0;
}

static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev[2];
    int           v = EV_CLEAR | EV_ADD | EV_ENABLE;
    EV_SET(&kev[0], fd, EVFILT_READ, v, 0, 0, info);
    EV_SET(&kev[1], fd, EVFILT_WRITE, v, 0, 0, info);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znF_unregister(zn_State *S, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znB_onconnect(zn_Tcp *tcp, int filter, int flags) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    tcp->connect_handler = NULL;
    if (!znS_valid(tcp)) return ZN_OK;
    zn_release(tcp->S);
    if ((flags & (EV_ERROR | EV_EOF)) != 0)
        return zn_closetcp(tcp), cb(tcp->connect_ud, tcp, ZN_ERROR), ZN_OK;
    if (filter != EVFILT_WRITE) return ZN_OK;
    znP_setnodelay(tcp->socket);
    cb(tcp->connect_ud, tcp, ZN_OK);
    /* also wakes a send/recv armed before the connect finished */
    return znB_retryrecv(tcp), znB_retrysend(tcp), ZN_OK;
}

static int znB_onevent(zn_Tcp *tcp, int filter, int flags) {
    int err = (flags & (EV_EOF | EV_ERROR)) != 0;
    /* eof/error comes on whichever filter changed: only the syscall tells */
    if (err || filter == EVFILT_READ) znB_retryrecv(tcp);
    if (err || filter == EVFILT_WRITE) znB_retrysend(tcp);
    return ZN_OK;
}

static int znB_onaccept(zn_Accept *a, int filter, int flags) {
    zn_AcceptHandler *cb = a->accept_handler;
    zn_SockAddr       raddr;
    socklen_t         size = sizeof(raddr);
    zn_Tcp           *tcp;
    int               fd, syserr, again;
    if (cb == NULL || !znS_valid(a)) return ZN_OK; /* not armed / closed */
    if ((flags & (EV_EOF | EV_ERROR)) != 0)
        return znB_disarmaccept(a), cb(a->accept_ud, a, ZN_ERROR, NULL), ZN_OK;
    if (filter != EVFILT_READ) return ZN_OK;
    fd = accept(a->socket, &raddr.addr, &size);
    if (fd < 0) {
        syserr = errno;
        /* nothing was lost; the next report retries */
        if (syserr == EINTR || syserr == ECONNABORTED) return ZN_OK;
        /* EAGAIN: the report and the queue disagreed; surface it once */
        znB_disarmaccept(a);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(a->accept_ud, a, again ? ZN_ERROR : znP_error(syserr), NULL);
        return ZN_OK;
    }
    znB_disarmaccept(a);
    tcp = znB_tcpfromfd(a->S, fd, &raddr);
    return cb(a->accept_ud, a, tcp ? ZN_OK : ZN_ERROR, tcp), ZN_OK;
}

static int znB_onrecvfrom(zn_Udp *udp, int filter, int flags) {
    zn_DataBuffer       b = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    zn_SockAddr         raddr;
    zn_PeerInfo         info;
    socklen_t           size;
    int                 bytes, syserr, again;
    if (cb == NULL) return ZN_OK; /* not armed / closed */
    if ((flags & (EV_EOF | EV_ERROR)) != 0) {
        znB_disarmudp(udp);
        return cb(udp->recv_ud, udp, ZN_ERROR, 0, NULL, 0), ZN_OK;
    }
    if (filter != EVFILT_READ) return ZN_OK;
    memset(&raddr, 0, size = sizeof(raddr));
    bytes = recvfrom(udp->socket, b.buf, b.len, 0, &raddr.addr, &size);
    if (bytes < 0) {
        syserr = errno;
        if (syserr == EINTR) return ZN_OK; /* nothing was lost; next retries */
        znB_disarmudp(udp);
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        cb(udp->recv_ud, udp, again ? ZN_ERROR : znP_error(syserr), 0, NULL, 0);
        return ZN_OK;
    }
    znP_setinfo(&raddr, &info);
    znB_disarmudp(udp);
    return cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port), ZN_OK;
}

static int znB_dispatch(zn_State *S, struct kevent *evt) {
    zn_SocketInfo *info = (zn_SocketInfo *)evt->udata;
    zn_Tcp        *tcp;
    if ((int)evt->ident == S->sockpairs[1]) { /* post */
        char buff[ZN_PAGESIZE];
        while (recv(evt->ident, buff, ZN_PAGESIZE, 0) > 0) continue;
        return znS_processposts(S), ZN_OK;
    }
    switch (info->type) {
    case ZN_SOCK_ACCEPT:
        znB_onaccept((zn_Accept *)info->head, evt->filter, evt->flags);
        break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler)
            znB_onconnect(tcp, evt->filter, evt->flags);
        else if (tcp->send_handler || tcp->recv_handler)
            znB_onevent(tcp, evt->filter, evt->flags);
        break;
    case ZN_SOCK_UDP:
        znB_onrecvfrom((zn_Udp *)info->head, evt->filter, evt->flags);
        break;
    }
    return ZN_OK;
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    struct timespec ms, *pms = NULL;
    int             i, r;
    if (timeout < ZN_FOREVER) {
        ms.tv_sec = timeout / 1000;
        ms.tv_nsec = (timeout % 1000) * 1000000;
        pms = &ms;
    }
    if (znR_process(S, ZN_MAX_RESULT_LOOPS)) return 1;
    r = kevent(S->kqueue, NULL, 0, S->events, ZN_MAX_EVENTS, pms);
    if (r < 0) return 0; /* error out */
    for (i = 0; i < r; ++i) znB_dispatch(S, &S->events[i]);
    return znR_process(S, ZN_MAX_RESULT_LOOPS);
}

static int znB_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0) return 0;
    S->kqueue = kqueue();
    if (S->kqueue < 0 || !znF_regread(S, S->sockpairs[1], NULL)) {
        if (S->kqueue >= 0) close(S->kqueue);
        return close(S->sockpairs[0]), close(S->sockpairs[1]), 0;
    }
    znS_initposts(S), znR_init(S);
    return znP_setnonblock(S->sockpairs[1]), 1;
}

static void znB_close(zn_State *S) {
    znS_processposts(S);
    znR_process(S, INT_MAX);
    assert(znQ_first(&S->result_queue) == NULL);
    close(S->sockpairs[0]), close(S->sockpairs[1]);
    close(S->kqueue);
}

#endif /* zn_use_backend_kqueue */

/* unixcc: flags+='-Wall -Wextra -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * win32cc: flags+='-Wall -Wextra -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll'
 * linuxcc: libs+='-pthread -lrt' output='znet.so'
 * maccc: libs+='-pthread' output='znet.so' */
