/* znet - C version of zsummerX */
#ifndef znet_h
#define znet_h


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
#endif

#ifdef ZN_STATIC_API
# ifndef ZN_IMPLEMENTATION
#  define ZN_IMPLEMENTATION
# endif
# define ZN_API ZN_STATIC
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

#ifndef zn_Time
# ifdef ZN_USE_64BIT_TIMER
typedef unsigned long long zn_Time;
# else
typedef unsigned zn_Time;
# endif
# define zn_Time zn_Time
#endif /* zn_Time */

#ifndef ZN_MAX_EVENTS
# define ZN_MAX_EVENTS   1024
#endif

#define ZN_MAX_ADDRLEN   50
#define ZN_MAX_TIMERPOOL 512
#define ZN_MAX_TIMERHEAP 512
#define ZN_MAX_PAGESIZE  4096

#define ZN_TIMER_NOINDEX (~(unsigned)0)
#define ZN_FOREVER       ((~(zn_Time)0)-100)
#define ZN_MAX_SIZET     ((~(size_t)0)-100)


ZN_NS_BEGIN

#define ZN_ERRORS(X)                                      \
    X(OK,       "No error")                               \
    X(ERROR,    "Operation failed")                       \
    X(ECLOSE,   "socket closed")                          \
    X(ECLOSED,  "Remote socket closed")                   \
    X(EHANGUP,  "Remote socket hang up")                  \
    X(ESOCKET,  "Socket creation error")                  \
    X(ECONNECT, "Connect error")                          \
    X(EBIND,    "Local address bind error")               \
    X(EPARAM,   "Parameter error")                        \
    X(EPOLL,    "Register to poll error")                 \
    X(ESTATE,   "State error")                            \
    X(EBUSY,    "Another operation performed")            \

typedef enum zn_Error {
#define X(name, msg) ZN_##name,
    ZN_ERRORS(X)
#undef  X
    ZN_ERROR_COUNT
} zn_Error;

typedef struct zn_State  zn_State;
typedef struct zn_Accept zn_Accept;
typedef struct zn_Tcp    zn_Tcp;
typedef struct zn_Udp    zn_Udp;
typedef struct zn_Timer  zn_Timer;

typedef struct zn_PeerInfo {
    char     addr[ZN_MAX_ADDRLEN];
    unsigned port;
} zn_PeerInfo;

typedef zn_Time zn_TimerHandler (void *ud, zn_Timer *timer, zn_Time delayed);

typedef void zn_PostHandler     (void *ud, zn_State *S);
typedef void zn_AcceptHandler   (void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp);
typedef void zn_ConnectHandler  (void *ud, zn_Tcp *tcp, unsigned err);
typedef void zn_SendHandler     (void *ud, zn_Tcp *tcp, unsigned err, unsigned count);
typedef void zn_RecvHandler     (void *ud, zn_Tcp *tcp, unsigned err, unsigned count);
typedef void zn_RecvFromHandler (void *ud, zn_Udp *udp, unsigned err, unsigned count,
                                 const char *addr, unsigned port);


/* znet state routines */

ZN_API void zn_initialize   (void);
ZN_API void zn_deinitialize (void);

ZN_API const char *zn_strerror (int err);
ZN_API const char *zn_engine   (void);

ZN_API zn_State *zn_newstate (void);
ZN_API void      zn_close    (zn_State *S);

ZN_API void *zn_getuserdata (zn_State *S);
ZN_API void  zn_setuserdata (zn_State *S, void *ud);

ZN_API unsigned zn_retain  (zn_State *S);
ZN_API unsigned zn_release (zn_State *S);

ZN_API int zn_run (zn_State *S, int mode);
#define ZN_RUN_ONCE  0
#define ZN_RUN_CHECK 1
#define ZN_RUN_LOOP  2

ZN_API int zn_post (zn_State *S, zn_PostHandler *cb, void *ud);


/* znet timer routines */

ZN_API zn_Time zn_time (void);

ZN_API zn_Timer *zn_newtimer (zn_State *S, zn_TimerHandler *cb, void *ud);
ZN_API void      zn_deltimer (zn_Timer *timer);

ZN_API int  zn_starttimer  (zn_Timer *timer, zn_Time delayms);
ZN_API void zn_canceltimer (zn_Timer *timer);


/* znet accept routines */

ZN_API zn_Accept* zn_newaccept   (zn_State *S, int packet);
ZN_API int        zn_closeaccept (zn_Accept *accept);
ZN_API void       zn_delaccept   (zn_Accept *accept);

ZN_API int zn_listen (zn_Accept *accept, const char *addr, unsigned port);
ZN_API int zn_accept (zn_Accept *accept, zn_AcceptHandler *cb, void *ud);


/* znet tcp socket routines */

ZN_API zn_Tcp* zn_newtcp   (zn_State *S);
ZN_API int     zn_closetcp (zn_Tcp *tcp);
ZN_API void    zn_deltcp   (zn_Tcp *tcp);

ZN_API void zn_getpeerinfo (zn_Tcp *tcp, zn_PeerInfo *info);

ZN_API int zn_connect (zn_Tcp *tcp, const char *addr, unsigned port,
                       int packet,
                       zn_ConnectHandler *cb, void *ud);

ZN_API int zn_send (zn_Tcp *tcp, const char *buff, unsigned len,
                    zn_SendHandler *cb, void *ud);
ZN_API int zn_recv (zn_Tcp *tcp,       char *buff, unsigned len,
                    zn_RecvHandler *cb, void *ud);


/* znet udp socket routines */

ZN_API zn_Udp* zn_newudp   (zn_State *S, const char *addr, unsigned port);
ZN_API int     zn_closeudp (zn_Udp *udp);
ZN_API void    zn_deludp   (zn_Udp *udp);

ZN_API int zn_sendto   (zn_Udp *udp, const char *buff, unsigned len,
                        const char *addr, unsigned port);
ZN_API int zn_recvfrom (zn_Udp *udp,       char *buff, unsigned len,
                        zn_RecvFromHandler *cb, void *ud);

ZN_NS_END


#endif /* znet_h */


/* implementations */

#if defined(ZN_IMPLEMENTATION) && !defined(znet_implemented)
#define znet_implemented

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||\
    defined(__DragonFly__) || defined(__MACOSX__) ||\
    (defined(__APPLE__) && defined(__MACH__))
# define zn_have_kqueue
#endif

#if !defined(_WIN32)
# define zn_have_info /* all objects have zn_SocketInfo field */
#endif

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
# elif defined(zn_have_kqueue) && !defined(ZN_USE_SELECT)
#   define zn_use_resultqueue
#   define zn_use_backend_reactive
#   define zn_use_backend_kqueue
# else
#   define zn_use_backend_select
# endif
#endif
#define zn_use_addr
#define zn_use_state
#include "znet.h"

#define zn_use_api
#include "znet.h"

ZN_NS_END

#endif /* ZN_IMPLEMENTATION */


#ifndef zn_list_h
#define zn_list_h

#define zn_rawset(l, r)   (*(void**)&(l) = (void*)(r))
#define zn_reverse(T, h)                                  do { \
    T *ret_ = NULL, *next_;                                    \
    while (h) next_ = h->next, h->next = ret_,                 \
              ret_ = h, h = next_;                             \
    (h) = ret_;                                              } while (0)

#define znL_entry(T)      T *next; T *prev
#define znL_head(T)       struct T##_hlist { znL_entry(T); }
#define znL_init(h)       (zn_rawset((h)->prev, h), zn_rawset((h)->next, h))
#define znL_empty(h)      ((void*)(h)->prev == (void*)&(h))

#define znL_insert(h,n)   (zn_rawset((n)->next, h),            \
                           (n)->prev = (h)->prev,              \
                           (h)->prev->next = (n),              \
                           (h)->prev = (n))                    \

#define znL_remove(n)     ((n)->prev->next = (n)->next,        \
                           (n)->next->prev = (n)->prev)        \

#define znL_apply(T, h, stmt)                             do { \
    T *cur, *next_ = (h)->next;                                \
    while (cur = next_, next_ = next_->next, cur != (T*)(h))   \
    { stmt; }                                                } while (0)

#define znQ_entry(T)      T *next
#define znQ_head(T)       struct T##_qlist { T *first, **plast; }
#define znQ_init(h)       ((h)->first = NULL, (h)->plast = &(h)->first)
#define znQ_first(h)      ((h)->first)
#define znQ_empty(h)      ((h)->first == NULL)

#define znQ_enqueue(h, n) ((n)->next = NULL,                   \
                          *(h)->plast = (n),                   \
                           (h)->plast = &(n)->next)            \

#define znQ_dequeue(h, pn)                                do { \
    if (((pn) = (h)->first) != NULL) {                         \
        (h)->first = (h)->first->next;                         \
        if ((h)->plast == &(pn)->next)                         \
            (h)->plast = &(h)->first; }                      } while (0)

#define znQ_apply(T, h, stmt)                             do { \
    T *cur = (h), *next_;                                      \
    while (cur) { next_ = cur->next; stmt; cur = next_; }    } while (0)

#endif /* zn_list_h */


#ifdef zn_use_mempool /* memory pool */
#undef zn_use_mempool

#include <assert.h>
#include <stdlib.h>

typedef struct zn_MemPool {
    void  *pages;
    void  *freed;
    size_t size;
} zn_MemPool;

static void znM_putobject(zn_MemPool *pool, void *obj)
{ *(void**)obj = pool->freed; pool->freed = obj; }

static void znM_initpool(zn_MemPool *pool, size_t size) {
    size_t sp = sizeof(void*);
    pool->pages = NULL;
    pool->freed = NULL;
    pool->size = size;
    assert(((sp - 1) & sp) == 0);
    assert(size >= sp && size % sp == 0);
    assert(ZN_MAX_PAGESIZE / size > 2);
}

static void znM_freepool(zn_MemPool *pool) {
    const size_t offset = ZN_MAX_PAGESIZE - sizeof(void*);
    while (pool->pages != NULL) {
        void *next = *(void**)((char*)pool->pages + offset);
        free(pool->pages);
        pool->pages = next;
    }
    znM_initpool(pool, pool->size);
}

static void *znM_getobject(zn_MemPool *pool) {
    void *obj = pool->freed;
    if (obj == NULL) {
        const size_t offset = ZN_MAX_PAGESIZE - sizeof(void*);
        void *end, *newpage = malloc(ZN_MAX_PAGESIZE);
        if (newpage == NULL) return NULL;
        *(void**)((char*)newpage + offset) = pool->pages;
        pool->pages = newpage;
        end = (char*)newpage + (offset/pool->size-1)*pool->size;
        while (end != newpage) {
            *(void**)end = pool->freed;
            pool->freed = (void**)end;
            end = (char*)end - pool->size;
        }
        return end;
    }
    pool->freed = *(void**)obj;
    return obj;
}

#endif /* zn_use_mempool */


#ifdef zn_use_timer /* need: mempool */
#undef zn_use_timer

#include <string.h>

typedef struct zn_TimerState {
    zn_MemPool timers;                                         \
    zn_Timer **heap;
    zn_Time nexttime;
    unsigned used;
    unsigned size;
} zn_TimerState;

struct zn_Timer {
    union { zn_Timer *next; void *ud; } u;
    zn_TimerHandler *handler;
    zn_TimerState   *ts;
    unsigned index;
    zn_Time  start;
    zn_Time  emit;
};

static int znT_hastimers(zn_TimerState *ts)
{ return ts->used != 0; }

static void znT_cleartimers(zn_TimerState *ts) {
    znM_freepool(&ts->timers);
    free(ts->heap);
    memset(ts, 0, sizeof(*ts));
    ts->nexttime = ZN_FOREVER;
}

static void znT_updatetimers(zn_TimerState *ts, zn_Time current) {
    if (ts->nexttime > current) return;
    while (ts->used && ts->heap[0]->emit <= current) {
        zn_Timer *timer = ts->heap[0];
        zn_canceltimer(timer);
        if (timer->handler) {
            int ret = timer->handler(timer->u.ud,
                    timer, current - timer->start);
            if (ret > 0) zn_starttimer(timer, ret);
        }
    }
    ts->nexttime = ts->used == 0 ? ZN_FOREVER : ts->heap[0]->emit;
}

static zn_Time znT_gettimeout(zn_TimerState *ts, zn_Time current) {
    zn_Time emit = ts->nexttime;
    if (emit < current) return 0;
    return emit - current;
}

static int znT_resizeheap(zn_TimerState *ts, size_t size) {
    zn_Timer **heap;
    size_t realsize = ZN_MAX_TIMERHEAP;
    while (realsize < size && realsize < ZN_MAX_SIZET/sizeof(zn_Timer*)/2)
        realsize <<= 1;
    if (realsize < size) return 0;
    heap = (zn_Timer**)realloc(ts->heap, realsize*sizeof(zn_Timer*));
    if (heap == NULL) return 0;
    ts->heap = heap;
    ts->size = (unsigned)realsize;
    return 1;
}

static zn_Timer *znT_newtimer(zn_TimerState *ts, zn_TimerHandler *cb, void *ud) {
    zn_Timer *timer = (zn_Timer*)znM_getobject(&ts->timers);
    if (timer == NULL) return NULL;
    timer->u.ud = ud;
    timer->handler = cb;
    timer->ts = ts;
    timer->index = ZN_TIMER_NOINDEX;
    return timer;
}

ZN_API void zn_deltimer(zn_Timer *timer) {
    zn_canceltimer(timer);
    timer->handler = NULL;
    znM_putobject(&timer->ts->timers, timer);
}

ZN_API int zn_starttimer(zn_Timer *timer, zn_Time delayms) {
    zn_TimerState *ts = timer->ts;
    unsigned i, p;
    if (timer->index != ZN_TIMER_NOINDEX)
        zn_canceltimer(timer);
    if (ts->size == ts->used
            && !znT_resizeheap(ts, ts->size * 2))
        return ZN_ERROR;
    i = ts->used++;
    timer->start = zn_time();
    timer->emit  = timer->start + delayms;
    while (p = (i-1)>>1, i > 0 && ts->heap[p]->emit > timer->emit)
        (ts->heap[i] = ts->heap[p])->index = i, i = p;
    (ts->heap[i] = timer)->index = i;
    if (i == 0) ts->nexttime = timer->emit;
    return ZN_OK;
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    zn_TimerState *ts = timer->ts;
    unsigned i = timer->index, p, k;
    if (i == ZN_TIMER_NOINDEX) return;
    timer->index = ZN_TIMER_NOINDEX;
    if (ts->used == 0 || timer == ts->heap[--ts->used])
        return;
    timer = ts->heap[ts->used];
    while (p = (i-1)>>1, i > 0 && ts->heap[p]->emit > timer->emit)
        (ts->heap[i] = ts->heap[p])->index = i, i = p;
    while ((k = i<<1|1) < ts->used
            && timer->emit > ts->heap[
                k += k+1 < ts->used && ts->heap[k]->emit > ts->heap[k+1]->emit
            ]->emit)
        (ts->heap[i] = ts->heap[k])->index = i, i = k;
    (ts->heap[i] = timer)->index = i;
}

#endif /* zn_use_timer */


#ifdef zn_use_api /* need: state, backend, addr */
#undef zn_use_api

ZN_API unsigned zn_retain   (zn_State *S) { return ++S->waitings; }
ZN_API unsigned zn_release  (zn_State *S) { return --S->waitings; }
ZN_API void *zn_getuserdata (zn_State *S) { return S->userdata; }
ZN_API void  zn_setuserdata (zn_State *S, void *ud) { S->userdata = ud; }

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *h, void *ud)
{ return znT_newtimer(&S->ts, h, ud); }

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)
{ *info = tcp->peer_info; }

ZN_API int zn_closeaccept(zn_Accept *accept) { return znP_closeaccept(accept);  }
ZN_API int zn_closetcp(zn_Tcp *tcp) { return znP_closetcp(tcp);  }
ZN_API int zn_closeudp(zn_Udp *udp) { return znP_closeudp(udp);  }

static int znS_poll(zn_State *S, int checkonly) {
    zn_Time current, timeout = 0;
    int has_more;

    S->status = ZN_STATUS_IN_RUN;
    znT_updatetimers(&S->ts, current = zn_time());
    if (!checkonly) timeout = znT_gettimeout(&S->ts, current);
    has_more = znP_poll(S, timeout);
    znT_updatetimers(&S->ts, zn_time());

    if (S->status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_READY; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->status = ZN_STATUS_READY;
    return has_more || znT_hastimers(&S->ts) || S->waitings > 0;
}

ZN_API const char *zn_strerror(int err) {
    const char *msg = "Unknown error";
    switch (err) {
#define X(name, str) case ZN_##name: msg = str; break;
        ZN_ERRORS(X)
#undef  X
    }
    return msg;
}

ZN_API zn_State *zn_newstate(void) {
    zn_State *S = (zn_State*)malloc(sizeof(zn_State));
    if (S == NULL) return NULL;
    memset(S, 0, sizeof(*S));
    S->ts.nexttime = ZN_FOREVER;
    znL_init(&S->active_accepts);
    znL_init(&S->active_tcps);
    znL_init(&S->active_udps);
    znM_initpool(&S->ts.timers,  sizeof(zn_Timer));
    znM_initpool(&S->posts,   sizeof(zn_Post));
    znM_initpool(&S->accepts, sizeof(zn_Accept));
    znM_initpool(&S->tcps,    sizeof(zn_Tcp));
    znM_initpool(&S->udps,    sizeof(zn_Udp));
    if (!znP_init(S)) {
        free(S);
        return NULL;
    }
    return S;
}

ZN_API void zn_close(zn_State *S) {
    int status = S->status;
    if (status == ZN_STATUS_IN_RUN || status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_CLOSING_IN_RUN;
        return;
    }
    /* 0. doesn't allow create new objects */
    S->status = ZN_STATUS_CLOSING;
    /* 1. cancel all operations */
    znT_cleartimers(&S->ts);
    znL_apply(zn_Accept, &S->active_accepts, zn_delaccept(cur));
    znL_apply(zn_Tcp,    &S->active_tcps,    zn_deltcp(cur));
    znL_apply(zn_Udp,    &S->active_udps,    zn_deludp(cur));
    znP_close(S);
    /* 2. delete all remaining objects */
    znM_freepool(&S->posts);
    znM_freepool(&S->accepts);
    znM_freepool(&S->tcps);
    znM_freepool(&S->udps);
    free(S);
}

ZN_API int zn_run(zn_State *S, int mode) {
    int err;
    switch (mode) {
    case ZN_RUN_CHECK:
        return znS_poll(S, 1);
    case ZN_RUN_ONCE:
        return znS_poll(S, 0);
    case ZN_RUN_LOOP:
        while ((err = znS_poll(S, 0)) > 0)
            ;
        return err;
    }
    return -ZN_ERROR;
}

ZN_API zn_Accept* zn_newaccept(zn_State *S, int packet) {
    ZN_GETOBJECT(S, zn_Accept, accept);
    accept->socket = ZN_INVALID_SOCKET;
#ifdef zn_have_info
    accept->info.type = ZN_SOCK_ACCEPT;
    accept->info.head = accept;
#endif /* zn_have_info */
    accept->type = packet ? SOCK_DGRAM : SOCK_STREAM;
    znP_initaccept(accept);
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    if (accept == NULL) return;
    zn_closeaccept(accept);
    if (accept->accept_handler == NULL)
        ZN_PUTOBJECT(accept);
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port) {
    zn_SockAddr local_addr;
    if (accept == NULL)                      return ZN_EPARAM;
    if (accept->socket != ZN_INVALID_SOCKET) return ZN_ESTATE;
    if (accept->accept_handler != NULL)      return ZN_EBUSY;
    if (!znU_parseaddr(&local_addr, addr, port)) return ZN_EPARAM;
    return znP_listen(accept, &local_addr);
}

ZN_API int zn_accept(zn_Accept *accept, zn_AcceptHandler *cb, void *ud) {
    int ret;
    if (accept == NULL || cb == NULL)        return ZN_EPARAM;
    if (accept->socket == ZN_INVALID_SOCKET) return ZN_ESTATE;
    if ((ret = znP_accept(accept)) == ZN_OK) {
        accept->accept_handler = cb;
        accept->accept_ud = ud;
        zn_retain(accept->S);
    }
    return ret;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    ZN_GETOBJECT(S, zn_Tcp, tcp);
    tcp->socket = ZN_INVALID_SOCKET;
#ifdef zn_have_info
    tcp->info.type = ZN_SOCK_TCP;
    tcp->info.head = tcp;
#endif /* zn_have_info */
    znP_inittcp(tcp);
    return tcp;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    if (tcp == NULL) return;
    zn_closetcp(tcp);
    /* if a request is on-the-go, then the IOCP will give callback, we
     * delete object on that callback. notice if you do not submit a
     * request, IOCP will not queued the callback, so delete is safe.
     * same as zn_delaccept() and zn_deludp() */
    if (tcp->connect_handler == NULL
            && tcp->recv_handler == NULL
            && tcp->send_handler == NULL)
        ZN_PUTOBJECT(tcp);
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, int packet, zn_ConnectHandler *cb, void *ud) {
    zn_SockAddr remote_addr;
    int         ret;
    if (tcp == NULL || cb == NULL)        return ZN_EPARAM;
    if (tcp->socket != ZN_INVALID_SOCKET) return ZN_ESTATE;
    if (tcp->connect_handler != NULL)     return ZN_EBUSY;
    if (znU_parseaddr(&remote_addr, addr, port) == 0) return ZN_EPARAM;

    if ((ret = znP_connect(tcp, &remote_addr, packet ?
                    SOCK_DGRAM : SOCK_STREAM)) != ZN_OK)
        return ret;
    assert(strlen(addr) < ZN_MAX_ADDRLEN);
    strcpy(tcp->peer_info.addr, addr);
    tcp->peer_info.port = port;
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    zn_retain(tcp->S);
    return ret;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len, zn_SendHandler *cb, void *ud) {
    int ret;
    if (tcp == NULL || cb == NULL || len == 0) return ZN_EPARAM;
    if (tcp->socket == ZN_INVALID_SOCKET)      return ZN_ESTATE;
    if (tcp->send_handler != NULL)             return ZN_EBUSY;
    tcp->send_buffer.buf = (char*)buff;
    tcp->send_buffer.len = len;
    if ((ret = znP_send(tcp)) != ZN_OK) {
        tcp->send_buffer.buf = NULL;
        tcp->send_buffer.len = 0;
    } else {
        tcp->send_handler = cb;
        tcp->send_ud = ud;
        zn_retain(tcp->S);
    }
    return ret;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    int ret;
    if (tcp == NULL || cb == NULL || len == 0) return ZN_EPARAM;
    if (tcp->socket == ZN_INVALID_SOCKET)      return ZN_ESTATE;
    if (tcp->recv_handler != NULL)             return ZN_EBUSY;
    tcp->recv_buffer.buf = buff;
    tcp->recv_buffer.len = len;
    if ((ret = znP_recv(tcp)) != ZN_OK) {
        tcp->recv_buffer.buf = NULL;
        tcp->recv_buffer.len = 0;
    } else {
        tcp->recv_handler = cb;
        tcp->recv_ud = ud;
        zn_retain(tcp->S);
    }
    return ret;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    zn_SockAddr local_addr;
    ZN_GETOBJECT(S, zn_Udp, udp);
    if (znU_parseaddr(&local_addr, addr, port) == 0) {
        ZN_PUTOBJECT(udp);
        return NULL;
    }
    udp->socket = ZN_INVALID_SOCKET;
    if (znP_initudp(udp, &local_addr) != ZN_OK) {
        ZN_PUTOBJECT(udp);
        return NULL;
    }
#ifdef zn_have_info
    udp->info.type = ZN_SOCK_UDP;
    udp->info.head = udp;
#endif /* zn_have_info */
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    if (udp == NULL) return;
    znP_closeudp(udp);
    if (udp->recv_handler == NULL)
        ZN_PUTOBJECT(udp);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned port) {
    zn_SockAddr remote_addr;
    if (udp == NULL || len == 0 || len >1200) return ZN_EPARAM;
    if (udp->socket == ZN_INVALID_SOCKET)     return ZN_ESTATE;
    if (znU_parseaddr(&remote_addr, addr, port) == 0) return ZN_EPARAM;
    return znP_sendto(udp, buff, len, &remote_addr);
}

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len, zn_RecvFromHandler *cb, void *ud) {
    int ret;
    if (udp == NULL || len == 0 || cb == NULL) return ZN_EPARAM;
    if (udp->socket == ZN_INVALID_SOCKET)      return ZN_ESTATE;
    if (udp->recv_handler)                     return ZN_EBUSY;

    udp->recv_buffer.buf = buff;
    udp->recv_buffer.len = len;
    if ((ret = znP_recvfrom(udp)) != ZN_OK) {
        udp->recv_buffer.buf = NULL;
        udp->recv_buffer.len = 0;
    } else {
        udp->recv_handler = cb;
        udp->recv_ud = ud;
        zn_retain(udp->S);
    }
    return ret;
}

#endif /* zn_use_api */


#ifdef zn_use_platform_win32
#undef zn_use_platform_win32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <WS2Tcpip.h>
#include <MSWSock.h>

#ifdef _MSC_VER
# pragma warning(disable: 4996) /* deprecated stuffs */
# pragma warning(disable: 4127) /* for do {} while(0) */
# pragma comment(lib, "ws2_32")
#endif /* _MSC_VER */

#define ZN_INVALID_SOCKET INVALID_SOCKET

static int znU_pton(int af, const char *src, void *dst) {
    struct sockaddr_storage ss;
    int size = sizeof(ss);
    char src_copy[INET6_ADDRSTRLEN+1];
    ZeroMemory(&ss, sizeof(ss));
    strncpy(src_copy, src, INET6_ADDRSTRLEN+1);
    src_copy[INET6_ADDRSTRLEN] = 0;
    if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr*)&ss, &size) == 0) {
        switch(af) {
        case AF_INET:
            *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
            return 1;
        case AF_INET6:
            *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
            return 1;
        }
    }
    return 0;
}

static const char *znU_ntop(int af, const void *src, char *dst, socklen_t size) {
    struct sockaddr_storage ss;
    unsigned long s = size;
    ZeroMemory(&ss, sizeof(ss));
    ss.ss_family = af;
    switch(af) {
    case AF_INET:
        ((struct sockaddr_in *)&ss)->sin_addr = *(struct in_addr *)src;
        break;
    case AF_INET6:
        ((struct sockaddr_in6 *)&ss)->sin6_addr = *(struct in6_addr *)src;
        break;
    default:
        return NULL;
    }
    return WSAAddressToString(
            (struct sockaddr*)&ss, sizeof(ss), NULL, dst, &s) == 0 ? dst : NULL;
}

static int znU_set_nodelay(SOCKET socket) {
    BOOL bEnable = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
            (const char*)&bEnable, sizeof(bEnable)) == 0;
}

static int znU_update_acceptinfo(SOCKET server, SOCKET client) {
    return setsockopt(client, SOL_SOCKET,
            SO_UPDATE_ACCEPT_CONTEXT,
            (const char*)&server, sizeof(server)) == 0;
}

static int znU_set_reuseaddr(SOCKET socket) {
    BOOL bReuseAddr = TRUE;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            (const char*)&bReuseAddr, sizeof(BOOL)) == 0;
}

static int znU_error(int syserr) {
    switch (syserr) {
    case WSAECONNABORTED:
    case WSAECONNREFUSED:
    case WSAECONNRESET:
        return ZN_EHANGUP;
    }
    return ZN_ERROR;
}

ZN_API zn_Time zn_time(void) {
    static LARGE_INTEGER counterFreq;
    static LARGE_INTEGER startTime;
    LARGE_INTEGER current;
    if (counterFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&counterFreq);
        QueryPerformanceCounter(&startTime);
        assert(counterFreq.HighPart == 0);
        return 0;
    }
    QueryPerformanceCounter(&current);
    return (zn_Time)((current.QuadPart - startTime.QuadPart) * 1000
            / counterFreq.LowPart);
}

#endif /* zn_use_platform_win32 */


#ifdef zn_use_platform_posix
#undef zn_use_platform_posix

#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>

#ifdef zn_use_backend_epoll
# include <sys/eventfd.h>
# include <sys/epoll.h>
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
#endif

#define znU_pton inet_pton
#define znU_ntop inet_ntop

typedef enum zn_SocketType {
    ZN_SOCK_ACCEPT,
    ZN_SOCK_TCP,
    ZN_SOCK_UDP,
} zn_SocketType;

typedef struct zn_SocketInfo {
    int type;
    void *head;
} zn_SocketInfo;

typedef struct zn_DataBuffer {
    size_t len;
    char  *buf;
} zn_DataBuffer;

static int znU_set_nodelay(int socket) {
    int enable = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
            (const void*)&enable, sizeof(enable)) == 0;
}

static int znU_set_nonblock(int socket) {
    return fcntl(socket, F_SETFL,
            fcntl(socket, F_GETFL)|O_NONBLOCK) == 0;
}

static int znU_set_reuseaddr(int socket) {
    int reuse_addr = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            (const void*)&reuse_addr, sizeof(reuse_addr)) == 0;
}

static int znU_set_nosigpipe(int socket) {
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    return setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
            (char*)&no_sigpipe, sizeof(no_sigpipe)) == 0;
#else
    (void)socket;
    return 0;
#endif /* SO_NOSIGPIPE */
}

static int znU_error(int syserr) {
    switch (syserr) {
    case EPIPE:
    case ECONNABORTED:
    case ECONNREFUSED:
    case ECONNRESET:
        return ZN_EHANGUP;
    }
    return ZN_ERROR;
}

ZN_API zn_Time zn_time(void) {
#ifdef __MACH__
    static mach_timebase_info_data_t time_info;
    static uint64_t start;
    uint64_t now = mach_absolute_time();
    if (!time_info.numer) {
        start = now;
        (void)mach_timebase_info(&time_info);
        return 0;
    }
    return (zn_Time)((now - start) * time_info.numer / time_info.denom / 1000000);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (zn_Time)(ts.tv_sec*1000+ts.tv_nsec/1000000);
#endif
}

#endif /* zn_use_platform_posix */


#ifdef zn_use_addr /* need: platform */
#undef zn_use_addr

#define znU_family(sa) ((sa)->addr.sa_family)
#define znU_isv6(sa)   (znU_family(sa) == AF_INET6)
#define znU_size(sa)   (znU_isv6(sa)? sizeof((sa)->ipv6) : sizeof((sa)->ipv4))

typedef union zn_SockAddr {
    struct sockaddr     addr;
    struct sockaddr_in  ipv4;
    struct sockaddr_in6 ipv6;
} zn_SockAddr;

static int znU_ipproto(int type)
{ return type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP; }

static int znU_parseaddr(zn_SockAddr *ret, const char *addr, unsigned port) {
    memset(ret, 0, sizeof(*ret));
    if (strchr(addr, ':') != 0) {
        ret->ipv6.sin6_family = AF_INET6;
        ret->ipv6.sin6_port = htons(port);
        if (znU_pton(AF_INET6, addr, &ret->ipv6.sin6_addr) != 1)
            return 0;
    } else {
        ret->ipv4.sin_family = AF_INET;
        ret->ipv4.sin_port = htons(port);
        if (znU_pton(AF_INET, addr, &ret->ipv4.sin_addr) != 1)
            return 0;
    }
    return znU_family(ret);
}

static void znU_setinfo(const zn_SockAddr *src, zn_PeerInfo *peer_info) {
    memset(peer_info, 0, sizeof(*peer_info));
    if (znU_isv6(src)) {
        znU_ntop(znU_family(src), &src->ipv6.sin6_addr,
                peer_info->addr, sizeof(peer_info->addr));
        peer_info->port = ntohs(src->ipv6.sin6_port);
    } else {
        znU_ntop(znU_family(src), &src->ipv4.sin_addr,
                peer_info->addr, sizeof(peer_info->addr));
        peer_info->port = ntohs(src->ipv4.sin_port);
    }
}

#endif /* zn_use_addr */


#ifdef zn_use_state /* zn_State declares, need: mempool, timer */
#undef zn_use_state

typedef enum zn_Status {
    ZN_STATUS_IN_RUN  = -1,       /* now in zn_run() */
    ZN_STATUS_READY   =  0,       /* not close */
    ZN_STATUS_CLOSING =  1,       /* prepare close */
    ZN_STATUS_CLOSING_IN_RUN = 2  /* prepare close in run() */
} zn_Status;

typedef struct zn_Post   zn_Post;
typedef struct zn_Result zn_Result;

struct zn_State {
    zn_MemPool posts;
    zn_MemPool accepts;
    zn_MemPool tcps;
    zn_MemPool udps;
    znL_head(zn_Accept) active_accepts;
    znL_head(zn_Tcp)    active_tcps;
    znL_head(zn_Udp)    active_udps;
    void      *userdata;
    zn_TimerState ts;
    zn_Status  status;
    unsigned   waitings;
#ifdef zn_use_postqueue
    pthread_mutex_t post_lock;
    znQ_head(zn_Post) post_queue;
#endif
#ifdef zn_use_resultqueue
    znQ_head(zn_Result) result_queue;
#endif
#ifdef zn_use_backend_iocp
    HANDLE iocp;
    CRITICAL_SECTION lock;
    OVERLAPPED_ENTRY entries[ZN_MAX_EVENTS];
#elif defined(zn_use_backend_epoll)
    int epoll;
    int eventfd;
    struct epoll_event events[ZN_MAX_EVENTS];
#elif defined(zn_use_backend_kqueue)
    int kqueue;
    int sockpairs[2];
    struct kevent events[ZN_MAX_EVENTS];
#else /* select backend */
    int sockpairs[2];
    int nfds;
    fd_set readfds, writefds, exceptfds;
    zn_SocketInfo *infos[FD_SETSIZE];
#endif
};

# define ZN_GETOBJECT(S, T, name)                T* name; do { \
    if (S->status > ZN_STATUS_READY)                           \
        return 0;                                              \
    name = (T*)znM_getobject(&S->name##s);                     \
    if (name == NULL) return 0;                                \
    memset(name, 0, sizeof(T));                                \
    znL_insert(&S->active_##name##s, name);                    \
    name->S = S;                                             } while (0)

# define ZN_PUTOBJECT(name)                               do { \
    zn_State *NS = name->S;                                    \
    znL_remove(name);                                          \
    znM_putobject(&NS->name##s, name);                       } while (0)

#endif /* zn_use_state */


#ifdef zn_use_postqueue /* need: state, list */
#undef zn_use_postqueue

static int znP_signal(zn_State *S);

struct zn_Post {
    zn_Post  *next;
    zn_State *S;
    zn_PostHandler *handler;
    void *ud;
};

static void znT_init(zn_State *S) {
    pthread_mutex_init(&S->post_lock, 0);
    znQ_init(&S->post_queue);
}

static void znT_process(zn_State *S) {
    zn_Post *post;
    pthread_mutex_lock(&S->post_lock);
    post = znQ_first(&S->post_queue);
    znQ_init(&S->post_queue);
    pthread_mutex_unlock(&S->post_lock);
    znQ_apply(zn_Post, post,
        if (cur->handler) cur->handler(cur->ud, cur->S);
        znM_putobject(&S->posts, cur));
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *post;
    int ret = ZN_ERROR;
    pthread_mutex_lock(&S->post_lock);
    post = (zn_Post*)znM_getobject(&S->posts);
    if (post != NULL) {
        post->S = S;
        post->handler = cb;
        post->ud = ud;
        znQ_enqueue(&S->post_queue, post);
        ret = znP_signal(S);
    }
    pthread_mutex_unlock(&S->post_lock);
    return ret;
}

#endif /* zn_use_postqueue */


#ifdef zn_use_resultqueue /* need: state, list */
#undef zn_use_resultqueue

#define ZN_MAX_RESULT_LOOPS 100

static void zn_onresult(zn_Result *result);
static void znR_init(zn_State *S) { znQ_init(&S->result_queue); }

struct zn_Result {
    zn_Result *next;
    zn_Tcp    *tcp;
    int        err;
};

static void znR_add(zn_State *S, int err, zn_Result *result)
{ result->err = err; znQ_enqueue(&S->result_queue, result); }

static int znR_process(zn_State *S, int all) {
    int count = 0;
    while (!znQ_empty(&S->result_queue)
            && (all || ++count <= ZN_MAX_RESULT_LOOPS)) {
        zn_Result *results = znQ_first(&S->result_queue);
        znQ_init(&S->result_queue);
        znQ_apply(zn_Result, results, zn_onresult(cur));
    }
    return !znQ_empty(&S->result_queue);
}

#endif /* zn_use_resultqueue */


/* backends */

#ifdef zn_use_backend_iocp
#undef zn_use_backend_iocp

static BOOL zn_initialized = FALSE;

typedef BOOL (WINAPI *LPGETQUEUEDCOMPLETIONSTATUSEX) (
  _In_  HANDLE             CompletionPort,
  _Out_ LPOVERLAPPED_ENTRY lpCompletionPortEntries,
  _In_  ULONG              ulCount,
  _Out_ PULONG             ulNumEntriesRemoved,
  _In_  DWORD              dwMilliseconds,
  _In_  BOOL               fAlertable
);

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
    OVERLAPPED overlapped; /* must be first */
    zn_RequestType type;
} zn_Request;

struct zn_Post {
    zn_State *S;
    zn_PostHandler *handler;
    void *ud;
};

struct zn_Tcp {
    znL_entry(zn_Tcp);
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    zn_Request connect_request;
    zn_Request send_request;
    zn_Request recv_request;
    SOCKET socket;
    zn_PeerInfo peer_info;
    WSABUF send_buffer;
    WSABUF recv_buffer;
};

struct zn_Accept {
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    zn_Request accept_request;
    SOCKET socket;
    SOCKET client;
    int   family;
    int   type;
    DWORD recv_length;
    char recv_buffer[(sizeof(zn_SockAddr)+16)*2];
};

struct zn_Udp {
    znL_entry(zn_Udp);
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
    zn_Request recv_request;
    SOCKET socket;
    WSABUF recv_buffer;
    zn_SockAddr recv_from;
    INT         recv_from_len;
};

ZN_API void zn_initialize(void) {
    if (!zn_initialized) {
        HMODULE kernel32;
        WORD version = MAKEWORD(2, 2);
        WSADATA d;
        if (WSAStartup(version, &d) != 0) {
            assert(!"can not initialize Windows sockets!");
            abort();
        }
        kernel32 = GetModuleHandleA("KERNEL32.DLL");
        if (kernel32) {
            union {
                LPGETQUEUEDCOMPLETIONSTATUSEX f;
                FARPROC                       v;
            } u;
            u.v = GetProcAddress(kernel32, "GetQueuedCompletionStatusEx");
            pGetQueuedCompletionStatusEx = u.f;
        }
        zn_initialized = TRUE;
    }
}

ZN_API void zn_deinitialize(void) {
    if (zn_initialized) {
        WSACleanup();
        zn_initialized = FALSE;
    }
}

/* tcp */

static int zn_getextension(SOCKET socket, GUID* gid, void *fn) {
    DWORD dwSize = 0;
    return WSAIoctl(socket,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            gid, sizeof(*gid),
            fn, sizeof(void(PASCAL*)(void)),
            &dwSize, NULL, NULL) == 0;
}

static int zn_tcpfromfamily(zn_Tcp *tcp, int family, int type) {
    SOCKET      socket;
    zn_SockAddr local_addr;

    if ((socket = WSASocket(family, type, znU_ipproto(type),
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    memset(&local_addr, 0, sizeof(local_addr));
    znU_family(&local_addr) = family;
    if (bind(socket, &local_addr.addr, znU_size(&local_addr)) != 0) {
        closesocket(socket);
        return ZN_EBIND;
    }

    if (CreateIoCompletionPort((HANDLE)socket, tcp->S->iocp,
                (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    tcp->socket = socket;
    return ZN_OK;
}

static void znP_inittcp(zn_Tcp *tcp) {
    tcp->connect_request.type = ZN_TCONNECT;
    tcp->send_request.type = ZN_TSEND;
    tcp->recv_request.type = ZN_TRECV;
}

static int znP_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    if (tcp->socket != INVALID_SOCKET) {
        if (closesocket(tcp->socket) != 0)
            ret = ZN_ERROR;
        tcp->socket = INVALID_SOCKET;
    }
    return ret;
}

static int znP_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    static LPFN_CONNECTEX lpConnectEx = NULL;
    static GUID gid = WSAID_CONNECTEX;
    DWORD dwLength = 0;
    char buf[1];
    int err = zn_tcpfromfamily(tcp, znU_family(addr), type);
    if (err != ZN_OK) return err;

    if (!lpConnectEx && !zn_getextension(tcp->socket, &gid, &lpConnectEx))
        return ZN_ECONNECT;

    if (!lpConnectEx(tcp->socket, &addr->addr, znU_size(addr),
                buf, 0, &dwLength, &tcp->connect_request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(tcp->socket);
        tcp->socket = INVALID_SOCKET;
        return ZN_ECONNECT;
    }

    return ZN_OK;
}

static int znP_send(zn_Tcp *tcp) {
    DWORD dwTemp1=0;
    int err;
    if (WSASend(tcp->socket, &tcp->send_buffer, 1,
                &dwTemp1, 0, &tcp->send_request.overlapped, NULL) == 0
            || (err = WSAGetLastError()) == WSA_IO_PENDING)
        return ZN_OK;
    zn_closetcp(tcp);
    return znU_error(err);
}

static int znP_recv(zn_Tcp *tcp) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
    int err;
    if (WSARecv(tcp->socket, &tcp->recv_buffer, 1,
                &dwRecv, &dwFlag, &tcp->recv_request.overlapped, NULL) == 0
            || (err = WSAGetLastError()) == WSA_IO_PENDING)
        return ZN_OK;
    zn_closetcp(tcp);
    return znU_error(err);
}

static void zn_onconnect(zn_Tcp *tcp, BOOL bSuccess) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    tcp->connect_handler = NULL;
    if (tcp->socket == INVALID_SOCKET) {
        /* cb(tcp->connect_ud, tcp, ZN_ECLOSE); */
        ZN_PUTOBJECT(tcp);
        return;
    }
    if (bSuccess)
        znU_set_nodelay(tcp->socket);
    else
        zn_closetcp(tcp);
    cb(tcp->connect_ud, tcp, bSuccess ? ZN_OK : ZN_ERROR);
}

static void zn_onsend(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_SendHandler *cb = tcp->send_handler;
    assert(tcp->send_handler);
    tcp->send_handler = NULL;
    if (tcp->socket == INVALID_SOCKET) {
        assert(tcp->socket == INVALID_SOCKET);
        /* cb(tcp->send_ud, tcp, ZN_ECLOSE, dwBytes); */
        if (tcp->recv_handler == NULL)
            ZN_PUTOBJECT(tcp);
        return;
    }
    if (!bSuccess) zn_closetcp(tcp);
    cb(tcp->send_ud, tcp, bSuccess ? ZN_OK : ZN_ERROR, dwBytes);
}

static void zn_onrecv(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvHandler *cb = tcp->recv_handler;
    assert(tcp->recv_handler);
    tcp->recv_handler = NULL;
    if (tcp->socket == INVALID_SOCKET) {
        /* cb(tcp->recv_ud, tcp, ZN_ECLOSE, dwBytes); */
        if (tcp->send_handler == NULL)
            ZN_PUTOBJECT(tcp);
        return;
    }
    if (dwBytes == 0 || tcp->socket == INVALID_SOCKET) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ECLOSED, dwBytes);
    } else if (!bSuccess) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_EHANGUP, dwBytes);
    } else
        cb(tcp->recv_ud, tcp, ZN_OK, dwBytes);
}

/* accept */

static void znP_initaccept(zn_Accept *accept) {
    accept->client = INVALID_SOCKET;
    accept->accept_request.type = ZN_TACCEPT;
}

static int znP_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    if (accept->socket != INVALID_SOCKET) {
        if (closesocket(accept->socket) != 0)
            ret = ZN_ERROR;
        accept->socket = INVALID_SOCKET;
    }
    if (accept->client != INVALID_SOCKET) {
        if (closesocket(accept->client) != 0)
            ret = ZN_ERROR;
        accept->client = INVALID_SOCKET;
    }
    return ret;
}

static int znP_listen(zn_Accept *accept, zn_SockAddr *addr) {
    SOCKET socket;
    accept->family = znU_family(addr);
    if ((socket = WSASocket(accept->family,
                    accept->type, znU_ipproto(accept->type),
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    znU_set_reuseaddr(accept->socket);
    if (bind(socket, &addr->addr, znU_size(addr)) != 0) {
        closesocket(socket);
        return ZN_EBIND;
    }

    if (accept->type == SOCK_STREAM && listen(socket, SOMAXCONN) != 0) {
        closesocket(socket);
        return ZN_ERROR;
    }

    if (CreateIoCompletionPort((HANDLE)socket, accept->S->iocp,
                (ULONG_PTR)accept, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    accept->socket = socket;
    return ZN_OK;
}

static int znP_accept(zn_Accept *accept) {
    static GUID gid = WSAID_ACCEPTEX;
    static LPFN_ACCEPTEX lpAcceptEx = NULL;
    SOCKET socket;

    if (!lpAcceptEx && !zn_getextension(accept->socket, &gid, &lpAcceptEx))
        return ZN_ERROR;

    if ((socket = WSASocket(accept->family,
                    accept->type, znU_ipproto(accept->type),
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    if (!lpAcceptEx(accept->socket, socket,
                accept->recv_buffer, 0,
                sizeof(zn_SockAddr)+16, sizeof(zn_SockAddr)+16,
                &accept->recv_length, &accept->accept_request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(socket);
        return ZN_ERROR;
    }

    accept->client = socket;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *accept, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID gid = WSAID_GETACCEPTEXSOCKADDRS;
    struct sockaddr *paddr1 = NULL, *paddr2 = NULL;
    zn_AcceptHandler *cb = accept->accept_handler;
    int tmp1 = 0, tmp2 = 0;
    zn_Tcp *tcp;

    accept->accept_handler = NULL;
    if (accept->socket == INVALID_SOCKET) {
        ZN_PUTOBJECT(accept);
        return;
    }

    if (!bSuccess) {
        /* zn_closeaccept(accept); */
        cb(accept->accept_ud, accept, ZN_ERROR, NULL);
        return;
    }

    znU_update_acceptinfo(accept->socket, accept->client);
    if (accept->type == SOCK_STREAM) znU_set_nodelay(accept->client);

    if (!lpGetAcceptExSockaddrs && !zn_getextension(accept->client,
                &gid, &lpGetAcceptExSockaddrs))
        return;

    lpGetAcceptExSockaddrs(accept->recv_buffer,
            accept->recv_length,
            sizeof(zn_SockAddr)+16,
            sizeof(zn_SockAddr)+16,
            &paddr1, &tmp1, &paddr2, &tmp2);

    tcp = zn_newtcp(accept->S);
    tcp->socket = accept->client;
    accept->client = INVALID_SOCKET;
    if (CreateIoCompletionPort((HANDLE)tcp->socket,
                tcp->S->iocp, (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(tcp->socket);
        ZN_PUTOBJECT(tcp);
        return;
    }

    znU_setinfo((zn_SockAddr*)&paddr2, &tcp->peer_info);
    cb(accept->accept_ud, accept, ZN_OK, tcp);
}

/* udp */

static int znP_closeudp(zn_Udp *udp)
{ return closesocket(udp->socket) == 0 ? ZN_OK : ZN_ERROR; }

static int znP_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    SOCKET socket;
    udp->recv_request.type = ZN_TRECVFROM;
    socket = WSASocket(znU_family(addr), SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
            WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
        return ZN_ESOCKET;

    if (bind(socket, &addr->addr, znU_size(addr)) != 0) {
        closesocket(socket);
        return ZN_EBIND;
    }

    if (CreateIoCompletionPort((HANDLE)socket, udp->S->iocp,
                (ULONG_PTR)udp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    udp->socket = socket;
    return ZN_OK;
}

static int znP_sendto(zn_Udp *udp, const char *buff, unsigned len, zn_SockAddr *addr) {
    sendto(udp->socket, buff, len, 0, &addr->addr, znU_size(addr));
    return ZN_OK;
}

static int znP_recvfrom(zn_Udp *udp) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
    memset(&udp->recv_from, 0, sizeof(udp->recv_from));
    udp->recv_from_len = sizeof(udp->recv_from);
    if ((WSARecvFrom(udp->socket, &udp->recv_buffer, 1, &dwRecv, &dwFlag,
                &udp->recv_from.addr, &udp->recv_from_len,
                &udp->recv_request.overlapped, NULL) == 0)
            || WSAGetLastError() == WSA_IO_PENDING)
        return ZN_OK;
    return ZN_ERROR;
}

static void zn_onrecvfrom(zn_Udp *udp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvFromHandler *cb = udp->recv_handler;
    assert(udp->recv_handler);
    udp->recv_handler = NULL;
    if (udp->socket == INVALID_SOCKET) {
        ZN_PUTOBJECT(udp);
        return;
    }
    if (bSuccess && dwBytes > 0) {
        zn_PeerInfo info;
        znU_setinfo(&udp->recv_from, &info);
        cb(udp->recv_ud, udp, ZN_OK, dwBytes, info.addr, info.port);
    }
    else cb(udp->recv_ud, udp, ZN_ERROR, dwBytes, NULL, 0);
}

/* poll */

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *post;
    EnterCriticalSection(&S->lock);
    post = (zn_Post*)znM_getobject(&S->posts);
    LeaveCriticalSection(&S->lock);
    if (post == NULL) return ZN_ERROR;
    post->S = S;
    post->handler = cb;
    post->ud = ud;
    if (!PostQueuedCompletionStatus(S->iocp, 0, 0, (LPOVERLAPPED)post)) {
        EnterCriticalSection(&S->lock);
        znM_putobject(&S->posts, post);
        LeaveCriticalSection(&S->lock);
        return ZN_ERROR;
    }
    return ZN_OK;
}

static int znP_init(zn_State *S) {
    S->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
            NULL, (ULONG_PTR)0, 1);
    InitializeCriticalSection(&S->lock);
    return S->iocp != NULL;
}

static void znP_close(zn_State *S) {
    /* use IOCP to wait all closed but not deleted object,
     * and delete them. */
    while (S->waitings != 0) {
        assert(S->waitings > 0);
        zn_run(S, ZN_RUN_ONCE);
    }
    CloseHandle(S->iocp);
    DeleteCriticalSection(&S->lock);
}

static void znP_dispatch(zn_State *S, BOOL bRet, LPOVERLAPPED_ENTRY pEntry) {
    zn_Request *req = (zn_Request*)pEntry->lpOverlapped;
    ULONG_PTR upComKey = pEntry->lpCompletionKey;
    DWORD dwBytes = pEntry->dwNumberOfBytesTransferred;
    zn_release(S);
    switch (req->type) {
    case ZN_TACCEPT:   zn_onaccept((zn_Accept*)upComKey, bRet); break;
    case ZN_TRECV:     zn_onrecv((zn_Tcp*)upComKey, bRet, dwBytes); break;
    case ZN_TSEND:     zn_onsend((zn_Tcp*)upComKey, bRet, dwBytes); break;
    case ZN_TCONNECT:  zn_onconnect((zn_Tcp*)upComKey, bRet); break;
    case ZN_TRECVFROM: zn_onrecvfrom((zn_Udp*)upComKey, bRet, dwBytes); break;
    case ZN_TSENDTO: /* do not have this operation */
    default: ;
    }
}

static int znP_poll(zn_State *S, zn_Time timeout) {
    BOOL bRet;
    DWORD ms = timeout >= ZN_FOREVER ? INFINITE : timeout;
    if (pGetQueuedCompletionStatusEx) {
        ULONG i, count;
        bRet = pGetQueuedCompletionStatusEx(S->iocp,
                S->entries, ZN_MAX_EVENTS, &count, ms, FALSE);
        if (!bRet) return 0; /* time out */
        for (i = 0; i < count; ++i) {
            DWORD transfer;
            BOOL result;
            if (S->entries[i].lpCompletionKey == 0) {
                zn_Post *post = (zn_Post*)S->entries[i].lpOverlapped;
                if (post->handler)
                    post->handler(post->ud, post->S);
                znM_putobject(&S->posts, post);
                continue;
            }
            result = GetOverlappedResult(S->iocp,
                    S->entries[i].lpOverlapped, &transfer, FALSE);
            znP_dispatch(S, result, &S->entries[i]);
        }
    } else {
        OVERLAPPED_ENTRY entry;
        bRet = GetQueuedCompletionStatus(S->iocp,
                &entry.dwNumberOfBytesTransferred,
                &entry.lpCompletionKey,
                &entry.lpOverlapped, ms);
        if (!bRet && !entry.lpOverlapped) /* time out */
            return 0;
        znP_dispatch(S, bRet, &entry);
    }
    return 0;
}

#endif /* zn_use_backend_iocp */


#ifdef zn_use_backend_select
#undef zn_use_backend_select

static void znP_inittcp(zn_Tcp *tcp) { (void)tcp; }
static void znP_initaccept(zn_Accept *accept) { (void)accept; }
ZN_API void zn_initialize(void) { }
ZN_API void zn_deinitialize(void) { }

ZN_API const char *zn_engine(void) { return "select"; }

static int znP_signal(zn_State *S)
{ char c = 0; return send(S->sockpairs[0], &c, 1, 0) == 1 ? ZN_OK : ZN_ERROR; }

static void znF_register_in(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->readfds);
    FD_SET(fd, &S->exceptfds);
}

static void znF_register_out(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->writefds);
    FD_SET(fd, &S->exceptfds);
}

static void znF_unregister_in(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    FD_CLR(fd, &S->readfds);
    FD_CLR(fd, &S->exceptfds);
}

static void znF_unregister_out(zn_State *S, int fd) {
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
    znL_entry(zn_Tcp);
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    int socket;
    zn_SocketInfo info;
    zn_PeerInfo peer_info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

struct zn_Accept {
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    int socket;
    int type;
    zn_SocketInfo info;
};

struct zn_Udp {
    znL_entry(zn_Udp);
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
    int socket;
    zn_SocketInfo info;
    zn_DataBuffer recv_buffer;
};

/* tcp */

static int znP_send(zn_Tcp *tcp)
{ znF_register_out(tcp->S, tcp->socket, &tcp->info); return ZN_OK; }

static int znP_recv(zn_Tcp *tcp)
{ znF_register_in(tcp->S, tcp->socket, &tcp->info); return ZN_OK; }

static zn_Tcp *zn_tcpfromfd(zn_State *S, int fd, zn_SockAddr *remote_addr) {
    zn_Tcp *tcp;
    if (fd >= FD_SETSIZE) {
        close(fd);
        return NULL;
    }
    znU_set_nosigpipe(fd);
    znU_set_nonblock(fd);
    znU_set_nodelay(fd);
    tcp = zn_newtcp(S);
    if (tcp == NULL) {
        close(fd);
        return NULL;
    }
    tcp->socket = fd;
    znU_setinfo(remote_addr, &tcp->peer_info);
    return tcp;
}

static int znP_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    if (tcp->connect_handler) zn_release(tcp->S);
    if (tcp->send_handler)    zn_release(tcp->S);
    if (tcp->recv_handler)    zn_release(tcp->S);
    tcp->connect_handler = NULL;
    tcp->send_handler    = NULL;
    tcp->recv_handler    = NULL;
    if (tcp->socket != -1) {
        znF_unregister(tcp->S, tcp->socket);
        if (close(tcp->socket) != 0)
            ret = ZN_ERROR;
        tcp->socket = -1;
    }
    return ret;
}

static int znP_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    int ret, fd = socket(znU_family(addr), type, 0);
    if (fd < 0) return ZN_ESOCKET;

    if (fd >= FD_SETSIZE) {
        close(fd);
        return ZN_ESOCKET;
    }

    if (tcp->S->nfds < fd)
        tcp->S->nfds = fd;
    znU_set_nonblock(fd);

    ret = connect(fd, &addr->addr, znU_size(addr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(fd);
        return ZN_ECONNECT;
    }

    tcp->socket = fd;
    znF_register_out(tcp->S, fd, &tcp->info);
    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int err) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    tcp->connect_handler = NULL;
    if (tcp->socket == -1) return;
    zn_release(tcp->S);
    znF_unregister_out(tcp->S, tcp->socket);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    znU_set_nosigpipe(tcp->socket);
    znU_set_nodelay(tcp->socket);
    cb(tcp->connect_ud, tcp, ZN_OK);
}

static void zn_onsend(zn_Tcp *tcp, int err) {
    zn_SendHandler *cb = tcp->send_handler;
    zn_DataBuffer buff = tcp->send_buffer;
    int bytes;
    tcp->send_handler = NULL;
    tcp->send_buffer.buf = NULL;
    tcp->send_buffer.len = 0;
    if (tcp->socket == -1) return;
    zn_release(tcp->S);
    znF_unregister_out(tcp->S, tcp->socket);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->send_ud, tcp, ZN_ERROR, 0);
        return;
    }

    if ((bytes = send(tcp->socket, buff.buf, buff.len, ZN_NOSIGNAL)) >= 0)
        cb(tcp->send_ud, tcp, ZN_OK, bytes);
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        int err = znU_error(errno);
        zn_closetcp(tcp);
        cb(tcp->send_ud, tcp, err, 0);
    }
}

static void zn_onrecv(zn_Tcp *tcp, int err) {
    zn_RecvHandler *cb = tcp->recv_handler;
    zn_DataBuffer buff = tcp->recv_buffer;
    int bytes;
    tcp->recv_handler = NULL;
    tcp->recv_buffer.buf = NULL;
    tcp->recv_buffer.len = 0;
    if (tcp->socket == -1) return;
    zn_release(tcp->S);
    znF_unregister_in(tcp->S, tcp->socket);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ERROR, 0);
        return;
    }

    if ((bytes = recv(tcp->socket, buff.buf, buff.len, 0)) > 0)
        cb(tcp->recv_ud, tcp, ZN_OK, bytes);
    else if (bytes == 0) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ECLOSED, bytes);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        int err = znU_error(errno);
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, err, 0);
    }
}

/* accept */

static int znP_accept(zn_Accept *accept)
{ znF_register_in(accept->S, accept->socket, &accept->info); return ZN_OK; }

static int znP_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    if (accept->accept_handler) zn_release(accept->S);
    accept->accept_handler = NULL;
    if (accept->socket != -1) {
        znF_unregister(accept->S, accept->socket);
        if (close(accept->socket) != 0)
            ret = ZN_ERROR;
        accept->socket = -1;
    }
    return ret;
}

static int znP_listen(zn_Accept *accept, zn_SockAddr *addr) {
    int fd = socket(znU_family(addr), accept->type, znU_ipproto(accept->type));
    if (fd < 0) return ZN_ESOCKET;
    if (accept->S->nfds < fd) accept->S->nfds = fd;

    znU_set_nonblock(fd);
    znU_set_reuseaddr(fd);
    if (bind(fd, &addr->addr, znU_size(addr)) != 0) {
        close(fd);
        return ZN_EBIND;
    }

    if (accept->type == SOCK_STREAM && listen(fd, SOMAXCONN) != 0) {
        close(fd);
        return ZN_ERROR;
    }

    accept->socket = fd;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *a, int err) {
    zn_AcceptHandler *cb = a->accept_handler;
    if (a->accept_handler == NULL) return;
    a->accept_handler = NULL;
    zn_release(a->S);
    znF_unregister_in(a->S, a->socket);

    while (!err) {
        zn_SockAddr remote_addr;
        socklen_t addr_size = sizeof(remote_addr);
        int ret = accept(a->socket, &remote_addr.addr, &addr_size);
        if (ret >= FD_SETSIZE)
            close(ret);
        else if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (tcp == NULL) return;
            if (a->S->nfds < ret) a->S->nfds = ret;
            cb(a->accept_ud, a, ZN_OK, tcp);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK)
            break;
        return;
    }

    zn_closeaccept(a);
    cb(a->accept_ud, a, ZN_ERROR, NULL);
}

/* udp */

static int znP_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znU_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;

    if (bind(fd, &addr->addr, znU_size(addr)) != 0) {
        close(fd);
        return ZN_EBIND;
    }

    udp->socket = fd;
    return ZN_OK;
}

static int znP_closeudp(zn_Udp *udp) {
    znF_unregister(udp->S, udp->socket);
    if (udp->recv_handler) zn_release(udp->S);
    udp->recv_handler = NULL;
    return close(udp->socket) == 0;
}

static int znP_sendto(zn_Udp *udp, const char *buff, unsigned len, zn_SockAddr *addr) {
    sendto(udp->socket, buff, len, 0, &addr->addr, znU_size(addr));
    return ZN_OK;
}

static int znP_recvfrom(zn_Udp *udp) {
    znF_register_in(udp->S, udp->socket, &udp->info);
    if (udp->S->nfds < udp->socket) udp->S->nfds = udp->socket;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, int err) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    assert(udp->recv_handler != NULL);
    udp->recv_handler = NULL;
    udp->recv_buffer.buf = NULL;
    udp->recv_buffer.len = 0;
    zn_release(udp->S);
    znF_unregister_in(udp->S, udp->socket);

    if (err)
        cb(udp->recv_ud, udp, ZN_ERROR, 0, NULL, 0);
    else {
        zn_SockAddr remote_addr;
        socklen_t addr_size;
        int bytes;
        memset(&remote_addr, 0, addr_size = sizeof(remote_addr));
        bytes = recvfrom(udp->socket, buff.buf, buff.len, 0,
                &remote_addr.addr, &addr_size);
        if (bytes >= 0) {
            zn_PeerInfo info;
            znU_setinfo(&remote_addr, &info);
            cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK)
            cb(udp->recv_ud, udp, znU_error(errno), 0, NULL, 0);
    }
}

/* poll */

static void znP_dispatch(zn_State *S, int fd, int setno) {
    zn_SocketInfo *info;
    zn_Tcp *tcp;
    int err = setno == 2;
    if (fd < 0 || fd >= FD_SETSIZE) return;
    if (fd == S->sockpairs[1]) { /* post */
        char buff[8192];
        while (recv(fd, buff, 8192, 0) > 0)
            ;
        znT_process(S);
        return;
    }
    info = S->infos[fd];
    switch (info->type) {
    case ZN_SOCK_ACCEPT:
        zn_onaccept((zn_Accept*)info->head, err);
        break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp*)info->head;
        if (tcp->connect_handler) {
            zn_onconnect(tcp, err);
            break;
        }
        if (tcp->send_handler && setno != 0)
            zn_onsend(tcp, err);
        if (tcp->recv_handler && setno != 1)
            zn_onrecv(tcp, err);
        break;
    case ZN_SOCK_UDP:
        zn_onrecvfrom((zn_Udp*)info->head, err);
        break;
    default: ;
    }
}

static int znP_poll(zn_State *S, zn_Time timeout) {
    int i, ret;
    fd_set readfds = S->readfds;
    fd_set writefds = S->writefds;
    fd_set exceptfds = S->exceptfds;
    struct timeval tv, *ptv = NULL;
    if (timeout < ZN_FOREVER) {
        tv.tv_sec = timeout/1000;
        tv.tv_usec = timeout%1000*1000;
        ptv = &tv;
    }
    ret = select(S->nfds+1, &readfds, &writefds, &exceptfds, ptv);
    if (ret < 0 && errno != EINTR) /* error out */
        return 0;
    for (i = 0; i <= S->nfds; ++i) {
        if (FD_ISSET(i, &readfds))
            znP_dispatch(S, i, 0);
        if (FD_ISSET(i, &writefds))
            znP_dispatch(S, i, 1);
        if (FD_ISSET(i, &exceptfds))
            znP_dispatch(S, i, 2);
    }
    return 0;
}

static int znP_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0)
        return 0;
    if (S->sockpairs[1] >= FD_SETSIZE) {
        close(S->sockpairs[0]);
        close(S->sockpairs[1]);
        return 0;
    }
    znT_init(S);
    FD_ZERO(&S->readfds);
    FD_ZERO(&S->writefds);
    FD_ZERO(&S->exceptfds);
    FD_SET(S->sockpairs[1], &S->readfds);
    znU_set_nonblock(S->sockpairs[1]);
    return 1;
}

static void znP_close(zn_State *S) {
    znT_process(S);
    close(S->sockpairs[0]);
    close(S->sockpairs[1]);
}

#endif /* zn_use_backend_select */


#ifdef zn_use_backend_reactive /* reactive model supporting edge triggering */
#undef zn_use_backend_reactive

static int znF_register_in(zn_State *S, int fd, zn_SocketInfo *info);
static int znF_register_all(zn_State *S, int fd, zn_SocketInfo *info);
static int znF_unregister(zn_State *S, int fd);

ZN_API void zn_initialize(void) { }
ZN_API void zn_deinitialize(void) { }
static int znP_accept(zn_Accept *accept) { (void)accept; return ZN_OK; }
static int znP_recvfrom(zn_Udp *udp) { (void)udp; return ZN_OK; }
static void znP_initaccept(zn_Accept *accept) { (void)accept; }

struct zn_Tcp {
    znL_entry(zn_Tcp);
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    int socket;
    unsigned can_read  : 1;
    unsigned can_write : 1;
    zn_SocketInfo info;
    zn_Result send_result;
    zn_Result recv_result;
    zn_PeerInfo peer_info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

struct zn_Accept {
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    int socket;
    int type;
    zn_SocketInfo info;
};

struct zn_Udp {
    znL_entry(zn_Udp);
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
    int socket;
    zn_SocketInfo info;
    zn_DataBuffer recv_buffer;
};

/* tcp */

static zn_Tcp *zn_tcpfromfd(zn_State *S, int fd, zn_SockAddr *remote_addr) {
    zn_Tcp *tcp = zn_newtcp(S);
    if (tcp == NULL) {
        close(fd);
        return NULL;
    }

    tcp->socket = fd;

    znU_set_nonblock(tcp->socket);
    tcp->can_read = tcp->can_write = 1;
    if (!znF_register_all(tcp->S, tcp->socket, &tcp->info)) {
        zn_deltcp(tcp);
        return NULL;
    }

    znU_set_nosigpipe(tcp->socket);
    znU_set_nodelay(tcp->socket);
    znU_setinfo(remote_addr, &tcp->peer_info);
    return tcp;
}

static void znP_inittcp(zn_Tcp *tcp) {
    tcp->can_read = tcp->can_write = 1;
    tcp->send_result.tcp = tcp;
    tcp->recv_result.tcp = tcp;
}

static int znP_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    if (tcp->connect_handler) zn_release(tcp->S);
    tcp->connect_handler = NULL;
    if (tcp->socket != -1) {
        znF_unregister(tcp->S, tcp->socket);
        /* We can not delete object directly when it's result plug in
         * result queue. So in this case we wait onresult() delete it,
         * same with the logic used in IOCP backend.
         * when recving/sending, the recv/send handler have value, but
         * result queue has value depends whether tcp's can_read or
         * can_write. So if we can not read/write, that means no
         * result in queue, so plug it to result list to wait delete.
         * when no recving/sending, plug it to result queue to close
         * it, not delete it immediately to collect memory bugs. */
        if (close(tcp->socket) != 0)
            ret = ZN_ERROR;
        if (tcp->recv_handler != NULL && !tcp->can_read
                && (tcp->send_handler == NULL || !tcp->can_write))
            znR_add(tcp->S, ZN_ECLOSE, &tcp->recv_result);
        tcp->socket = -1;
    }
    return ret;
}

static int znP_connect(zn_Tcp *tcp, zn_SockAddr *addr, int type) {
    int ret, fd = socket(znU_family(addr), type, 0);
    if (fd < 0) return ZN_ESOCKET;
    znU_set_nonblock(fd);
    ret = connect(fd, &addr->addr, znU_size(addr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(fd);
        return ZN_ECONNECT;
    }
    if (!znF_register_all(tcp->S, fd, &tcp->info)) {
        close(fd);
        return ZN_EPOLL;
    }
    tcp->socket = fd;
    return ZN_OK;
}

static int znP_send(zn_Tcp *tcp) {
    if (tcp->can_write) {
        int bytes = send(tcp->socket,
                tcp->send_buffer.buf, tcp->send_buffer.len, ZN_NOSIGNAL);
        if (bytes >= 0) {
            tcp->send_buffer.len = bytes;
            znR_add(tcp->S, ZN_OK, &tcp->send_result);
        } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, znU_error(errno), &tcp->send_result);
        else
            tcp->can_write = 0;
    }
    return ZN_OK;
}

static int znP_recv(zn_Tcp *tcp) {
    if (tcp->can_read) {
        int bytes = recv(tcp->socket,
                tcp->recv_buffer.buf, tcp->recv_buffer.len, 0);
        if (bytes > 0) {
            tcp->recv_buffer.len = bytes;
            znR_add(tcp->S, ZN_OK, &tcp->recv_result);
        } else if (bytes == 0)
            znR_add(tcp->S, ZN_ECLOSED, &tcp->recv_result);
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, znU_error(errno), &tcp->recv_result);
        else
            tcp->can_read = 0;
    }
    return ZN_OK;
}

static void zn_onresult(zn_Result *result) {
    zn_Tcp *tcp = result->tcp;
    zn_release(tcp->S);
    if (result == &tcp->send_result) {
        zn_DataBuffer buff = tcp->send_buffer;
        zn_SendHandler *cb = tcp->send_handler;
        assert(tcp->send_handler != NULL);
        tcp->send_handler = NULL;
        tcp->send_buffer.buf = NULL;
        tcp->send_buffer.len = 0;
        if (tcp->socket == -1) {
            /* cb(tcp->send_ud, tcp, ZN_ECLOSE, 0); */
            if (tcp->recv_handler == NULL)
                ZN_PUTOBJECT(tcp);
        } else if (result->err == ZN_OK)
            cb(tcp->send_ud, tcp, ZN_OK, buff.len);
        else {
            zn_closetcp(tcp);
            cb(tcp->send_ud, tcp, result->err, 0);
        }
    } else {
        zn_DataBuffer buff = tcp->recv_buffer;
        zn_RecvHandler *cb = tcp->recv_handler;
        assert(tcp->recv_handler != NULL);
        tcp->recv_handler = NULL;
        tcp->recv_buffer.buf = NULL;
        tcp->recv_buffer.len = 0;
        if (tcp->socket == -1) {
            /* cb(tcp->recv_ud, tcp, ZN_ECLOSE, 0); */
            if (tcp->send_handler == NULL)
                ZN_PUTOBJECT(tcp);
        } else if (result->err == ZN_OK)
            cb(tcp->recv_ud, tcp, ZN_OK, buff.len);
        else {
            zn_closetcp(tcp);
            cb(tcp->recv_ud, tcp, result->err, 0);
        }
    }
}

/* accept */

static int znP_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    if (accept->accept_handler) zn_release(accept->S);
    accept->accept_handler = NULL;
    if (accept->socket != -1) {
        znF_unregister(accept->S, accept->socket);
        if (close(accept->socket) != 0)
            ret = ZN_ERROR;
        accept->socket = -1;
    }
    return ret;
}

static int znP_listen(zn_Accept *accept, zn_SockAddr *addr) {
    int fd = socket(znU_family(addr), accept->type, znU_ipproto(accept->type));
    if (fd < 0) return ZN_ESOCKET;
    if (!znF_register_in(accept->S, fd, &accept->info)) {
        close(fd);
        return ZN_EPOLL;
    }
    znU_set_reuseaddr(fd);
    if (bind(fd, &addr->addr, znU_size(addr)) != 0) {
        close(fd);
        return ZN_EBIND;
    }
    if (accept->type == SOCK_STREAM && listen(fd, SOMAXCONN) != 0) {
        close(fd);
        return ZN_ERROR;
    }
    accept->socket = fd;
    return ZN_OK;
}

/* udp */

static int znP_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znU_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;
    if (bind(fd, &addr->addr, znU_size(addr)) != 0) {
        close(fd);
        return ZN_EBIND;
    }
    if (!znF_register_in(udp->S, fd, &udp->info)) {
        close(fd);
        return ZN_EPOLL;
    }
    udp->socket = fd;
    return ZN_OK;
}

static int znP_closeudp(zn_Udp *udp) {
    znF_unregister(udp->S, udp->socket);
    if (udp->recv_handler) zn_release(udp->S);
    udp->recv_handler = NULL;
    return close(udp->socket) == 0;
}

static int znP_sendto(zn_Udp *udp, const char *buff, size_t len, zn_SockAddr *addr) {
    sendto(udp->socket, buff, len, 0, &addr->addr, znU_size(addr));
    return ZN_OK;
}

#endif /* zn_use_backend_reactive */


#ifdef zn_use_backend_epoll
#undef zn_use_backend_epoll

ZN_API const char *zn_engine(void) { return "epoll"; }

static int znP_signal(zn_State *S)
{ return eventfd_write(S->eventfd, (eventfd_t)1) != 0 ? ZN_OK : ZN_ERROR; }

static int znF_register_in(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_register_all(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    event.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP;
    event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_unregister(zn_State *S, int fd) {
    struct epoll_event event;
    return epoll_ctl(S->epoll, EPOLL_CTL_DEL, fd, &event) == 0;
}

static void zn_onconnect(zn_Tcp *tcp, int eventmask) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    tcp->connect_handler = NULL;
    if (tcp->socket == -1) return;
    zn_release(tcp->S);

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    if ((eventmask & EPOLLOUT) != 0) {
        tcp->can_read = tcp->can_write = 1;
        znU_set_nosigpipe(tcp->socket);
        znU_set_nodelay(tcp->socket);
        cb(tcp->connect_ud, tcp, ZN_OK);
    }
}

static void zn_onevent(zn_Tcp *tcp, int eventmask) {
    int can_read  = tcp->can_read;
    int can_write = tcp->can_write;
    if ((eventmask & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) != 0)
        tcp->can_read = tcp->can_write = 1;
    if ((eventmask & EPOLLIN) != 0 && !can_read) {
        zn_RecvHandler *cb = tcp->recv_handler;
        tcp->recv_handler = NULL;
        tcp->can_read = 1;
        if (cb == NULL || tcp->socket == -1) return;
        zn_release(tcp->S);
        zn_recv(tcp, tcp->recv_buffer.buf, tcp->recv_buffer.len,
                cb, tcp->recv_ud);
    }
    if ((eventmask & EPOLLOUT) != 0 && !can_write) {
        zn_SendHandler *cb = tcp->send_handler;
        tcp->send_handler = NULL;
        tcp->can_write = 1;
        if (cb == NULL || tcp->socket == -1) return;
        zn_release(tcp->S);
        zn_send(tcp, tcp->send_buffer.buf, tcp->send_buffer.len,
                cb, tcp->send_ud);
    }
}

static void zn_onaccept(zn_Accept *a, int eventmask) {
    zn_AcceptHandler *cb = a->accept_handler;
    a->accept_handler = NULL;
    if (cb == NULL || a->socket == -1) return;
    zn_release(a->S);

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        zn_closeaccept(a);
        cb(a->accept_ud, a, ZN_ERROR, NULL);
        return;
    }

    if ((eventmask & EPOLLIN) != 0) {
        zn_SockAddr remote_addr;
        socklen_t addr_size = sizeof(zn_SockAddr);
        int ret = accept(a->socket, &remote_addr.addr, &addr_size);
        if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (tcp != NULL) cb(a->accept_ud, a, ZN_OK, tcp);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            int err = znU_error(errno);
            zn_closeaccept(a);
            cb(a->accept_ud, a, err, NULL);
        }
    }
}

static void zn_onrecvfrom(zn_Udp *udp, int eventmask) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    if (cb == NULL || udp->socket == -1) return;
    udp->recv_handler = NULL;
    udp->recv_buffer.buf = NULL;
    udp->recv_buffer.len = 0;
    zn_release(udp->S);

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
        return;
    }

    if ((eventmask & EPOLLIN) != 0) {
        zn_SockAddr remote_addr;
        socklen_t addr_size;
        int bytes;
        memset(&remote_addr, 0, addr_size = sizeof(remote_addr));
        bytes = recvfrom(udp->socket, buff.buf, buff.len, 0,
                &remote_addr.addr, &addr_size);
        if (bytes >= 0) {
            zn_PeerInfo info;
            znU_setinfo(&remote_addr, &info);
            cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK)
            cb(udp->recv_ud, udp, znU_error(errno), 0, NULL, 0);
    }
}

static void znP_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_SocketInfo *info = (zn_SocketInfo*)evt->data.ptr;
    zn_Tcp *tcp;
    int eventmask = evt->events;
    if (info == NULL) { /* post */
        eventfd_t value;
        eventfd_read(S->eventfd, &value);
        znT_process(S);
        return;
    }
    switch (info->type) {
    case ZN_SOCK_ACCEPT:
        zn_onaccept((zn_Accept*)info->head, eventmask);
        break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp*)info->head;
        if (tcp->connect_handler)
            zn_onconnect(tcp, eventmask);
        else if (tcp->send_handler || tcp->recv_handler)
            zn_onevent(tcp, eventmask);
        break;
    case ZN_SOCK_UDP:
        zn_onrecvfrom((zn_Udp*)info->head, eventmask);
        break;
    default: ;
    }
}

static int znP_poll(zn_State *S, zn_Time timeout) {
    int i, ret;
    int ms = timeout >= ZN_FOREVER ? -1 : (int)timeout;
    if (znR_process(S, 0))
        return 1;
    ret = epoll_wait(S->epoll, S->events, ZN_MAX_EVENTS, ms);
    if (ret < 0 && errno != EINTR) /* error out */
        return 0;
    for (i = 0; i < ret; ++i)
        znP_dispatch(S, &S->events[i]);
    return znR_process(S, 0);
}

static int znP_init(zn_State *S) {
    if ((S->eventfd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK)) < 0)
        return 0;
    S->epoll = epoll_create(ZN_MAX_EVENTS);
    if (S->epoll < 0 || !znF_register_in(S, S->eventfd, NULL)) {
        if (S->epoll >= 0) close(S->epoll);
        close(S->eventfd);
        return 0;
    }
    znT_init(S);
    znR_init(S);
    return 1;
}

static void znP_close(zn_State *S) {
    znT_process(S);
    znR_process(S, 1);
    assert(znQ_empty(&S->result_queue));
    close(S->eventfd);
    close(S->epoll);
}

#endif /* zn_use_backend_epoll */


#ifdef zn_use_backend_kqueue
#undef zn_use_backend_kqueue

ZN_API const char *zn_engine(void) { return "kqueue"; }

static int znP_signal(zn_State *S)
{ char c = 0; return send(S->sockpairs[0], &c, 1, 0) == 1 ? ZN_OK : ZN_ERROR; }

static int znF_register_in(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, info);
    return kevent(S->kqueue, &kev, 1, NULL, 0, NULL) == 0;
}

static int znF_register_all(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, EV_CLEAR|EV_ADD|EV_ENABLE, 0, 0, info);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_CLEAR|EV_ADD|EV_ENABLE, 0, 0, info);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znF_unregister(zn_State *S, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static void zn_onconnect(zn_Tcp *tcp, int filter, int flags) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    tcp->connect_handler = NULL;
    if (tcp->socket == -1) return;
    zn_release(tcp->S);

    if ((flags & (EV_ERROR|EV_EOF)) != 0) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    if (filter == EVFILT_WRITE) {
        tcp->can_read = tcp->can_write = 1;
        znU_set_nosigpipe(tcp->socket);
        znU_set_nodelay(tcp->socket);
        cb(tcp->connect_ud, tcp, ZN_OK);
    }
}

static void zn_onevent(zn_Tcp *tcp, int filter, int flags) {
    int can_read  = tcp->can_read;
    int can_write = tcp->can_write;
    if ((flags & (EV_EOF|EV_ERROR)) != 0)
        tcp->can_read = tcp->can_write = 1;
    if (filter == EVFILT_READ && !can_read) {
        zn_RecvHandler *cb = tcp->recv_handler;
        tcp->recv_handler = NULL;
        tcp->can_read = 1;
        if (cb == NULL || tcp->socket == -1) return;
        zn_release(tcp->S);
        zn_recv(tcp, tcp->recv_buffer.buf, tcp->recv_buffer.len,
                cb, tcp->recv_ud);
    }
    if (filter == EVFILT_WRITE && !can_write) {
        zn_SendHandler *cb = tcp->send_handler;
        tcp->send_handler = NULL;
        tcp->can_write = 1;
        if (cb == NULL || tcp->socket == -1) return;
        zn_release(tcp->S);
        zn_send(tcp, tcp->send_buffer.buf, tcp->send_buffer.len,
                cb, tcp->send_ud);
    }
}

static void zn_onaccept(zn_Accept *a, int filter, int flags) {
    zn_AcceptHandler *cb = a->accept_handler;
    a->accept_handler = NULL;
    if (cb == NULL || a->socket == -1) return;
    zn_release(a->S);

    if ((flags & (EV_EOF|EV_ERROR)) != 0) {
        zn_closeaccept(a);
        cb(a->accept_ud, a, ZN_ERROR, NULL);
        return;
    }

    if (filter == EVFILT_READ) {
        zn_SockAddr remote_addr;
        socklen_t addr_size = sizeof(zn_SockAddr);
        int ret = accept(a->socket, &remote_addr.addr, &addr_size);
        if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (tcp != NULL) cb(a->accept_ud, a, ZN_OK, tcp);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            int err = znU_error(errno);
            zn_closeaccept(a);
            cb(a->accept_ud, a, err, NULL);
        }
    }
}

static void zn_onrecvfrom(zn_Udp *udp, int filter, int flags) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    if (cb == NULL) return;
    udp->recv_handler = NULL;
    udp->recv_buffer.buf = NULL;
    udp->recv_buffer.len = 0;
    zn_release(udp->S);

    if (flags & (EV_EOF|EV_ERROR)) {
        cb(udp->recv_ud, udp, ZN_ERROR, 0, NULL, 0);
        return;
    }

    if (filter == EVFILT_READ) {
        zn_SockAddr remote_addr;
        socklen_t addr_size;
        int bytes;
        memset(&remote_addr, 0, addr_size = sizeof(&remote_addr));
        bytes = recvfrom(udp->socket, buff.buf, buff.len, 0,
                &remote_addr.addr, &addr_size);
        if (bytes >= 0) {
            zn_PeerInfo info;
            znU_setinfo(&remote_addr, &info);
            cb(udp->recv_ud, udp, ZN_OK, bytes, info.addr, info.port);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK)
            cb(udp->recv_ud, udp, znU_error(errno), 0, NULL, 0);
    }
}

static void znP_dispatch(zn_State *S, struct kevent *evt) {
    zn_SocketInfo *info = (zn_SocketInfo*)evt->udata;
    zn_Tcp *tcp;
    if ((int)evt->ident == S->sockpairs[1]) { /* post */
        char buff[8192];
        while (recv(evt->ident, buff, 8192, 0) > 0)
            ;
        znT_process(S);
        return;
    }
    switch (info->type) {
    case ZN_SOCK_ACCEPT:
        zn_onaccept((zn_Accept*)info->head, evt->filter, evt->flags);
        break;
    case ZN_SOCK_TCP:
        tcp = (zn_Tcp*)info->head;
        if (tcp->connect_handler)
            zn_onconnect(tcp, evt->filter, evt->flags);
        else if (tcp->send_handler || tcp->recv_handler)
            zn_onevent(tcp, evt->filter, evt->flags);
        break;
    case ZN_SOCK_UDP:
        zn_onrecvfrom((zn_Udp*)info->head, evt->filter, evt->flags);
        break;
    default: ;
    }
}

static int znP_poll(zn_State *S, zn_Time timeout) {
    int i, ret;
    struct timespec ms, *pms = NULL;
    if (timeout < ZN_FOREVER) {
        ms.tv_sec = timeout / 1000;
        ms.tv_nsec = (timeout % 1000) * 1000000;
        pms = &ms;
    }
    if (znR_process(S, 0))
        return 1;
    ret = kevent(S->kqueue, NULL, 0, S->events, ZN_MAX_EVENTS, pms);
    if (ret < 0) /* error out */
        return 0;
    for (i = 0; i < ret; ++i)
        znP_dispatch(S, &S->events[i]);
    return znR_process(S, 0);
}

static int znP_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0)
        return 0;
    S->kqueue = kqueue();
    if (S->kqueue < 0 || !znF_register_in(S, S->sockpairs[1], NULL)) {
        if (S->kqueue >= 0) close(S->kqueue);
        close(S->sockpairs[0]);
        close(S->sockpairs[1]);
        return 0;
    }
    znT_init(S);
    znR_init(S);
    znU_set_nonblock(S->sockpairs[1]);
    return 1;
}

static void znP_close(zn_State *S) {
    znT_process(S);
    znR_process(S, 1);
    assert(znQ_first(&S->result_queue) == NULL);
    close(S->sockpairs[0]);
    close(S->sockpairs[1]);
    close(S->kqueue);
}

#endif /* zn_use_backend_kqueue */

/* unixcc: flags+='-Wall -Wextra -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * win32cc: flags+='-Wall -Wextra -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll'
 * linuxcc: libs+='-pthread -lrt' output='znet.so'
 * maccc: libs+='-pthread' output='znet.so' */

