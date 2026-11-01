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
typedef void zn_AcceptHandler(void *ud, zn_Accept *a, int err, zn_Tcp *tcp);
typedef void zn_ConnectHandler(void *ud, zn_Tcp *tcp, int err);
typedef void zn_SendHandler(void *ud, zn_Tcp *tcp, int err, unsigned count);
typedef void zn_RecvHandler(void *ud, zn_Tcp *tcp, int err, unsigned count);
typedef void zn_RecvFromHandler(void *ud, zn_Udp *udp, int err, unsigned count,
                                const char *addr, unsigned port);

/* znet state routines */

ZN_API void zn_initialize(void);
ZN_API void zn_deinitialize(void);

ZN_API const char *zn_strerror(int err);
ZN_API const char *zn_engine(void);

ZN_API zn_State *zn_newstate(void);
ZN_API void      zn_close(zn_State *S);

ZN_API int zn_lasterror(zn_State *S);

ZN_API void *zn_getuserdata(zn_State *S);
ZN_API void  zn_setuserdata(zn_State *S, void *ud);

ZN_API unsigned zn_retain(zn_State *S);
ZN_API unsigned zn_release(zn_State *S);

ZN_API int zn_run(zn_State *S, int mode);
#define ZN_RUN_ONCE  0 /* one blocking poll */
#define ZN_RUN_CHECK 1 /* poll, do not block */
#define ZN_RUN_LOOP  2 /* poll until idle */

ZN_API int zn_post(zn_State *S, zn_PostHandler *h, void *ud);

/* znet timer routines */

ZN_API zn_Time zn_time(void);

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *h, void *ud);
ZN_API void      zn_deltimer(zn_Timer *timer);

ZN_API int  zn_starttimer(zn_Timer *timer, zn_Time delayms);
ZN_API void zn_canceltimer(zn_Timer *timer);

/* znet accept routines */

ZN_API zn_Accept *zn_listen(zn_State *S, const char *addr, unsigned port);
ZN_API void       zn_delaccept(zn_Accept *a);

ZN_API int zn_accept(zn_Accept *a, zn_AcceptHandler *h, void *ud);

/* znet tcp socket routines */

ZN_API zn_Tcp *zn_connect(zn_State *S, const char *addr, unsigned port,
                          zn_ConnectHandler *h, void *ud);
ZN_API void    zn_deltcp(zn_Tcp *tcp);

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info);

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len,
                   zn_SendHandler *h, void *ud);
ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *h,
                   void *ud);

/* znet udp socket routines */

ZN_API zn_Udp *zn_bind(zn_State *S, const char *addr, unsigned port);
ZN_API void    zn_deludp(zn_Udp *udp);

ZN_API int zn_connectudp(zn_Udp *udp, const char *addr, unsigned port);

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len,
                     const char *addr, unsigned port);
ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len,
                       zn_RecvFromHandler *h, void *ud);

ZN_NS_END

#endif /* znet_h */

#ifndef zn_list_h
#define zn_list_h

/* clang-format off */
#define znL_rawset(l, r) (*(void **)&(l) = (void *)(r))
#define znL_entry(T)     T *next, *prev
#define znL_head(T)      struct T##_hlist { znL_entry(T); }
#define znL_init(h)      (znL_rawset((h)->prev, h), znL_rawset((h)->next, h))
#define znL_empty(h)     ((void *)(h)->next == (void *)(h))
/* clang-format on */

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

/* one pass: selectors first, every section then compiles from them */
#if defined(_WIN32)
# define ZN_USE_PLATFORM_WIN32
# define ZN_USE_BACKEND_IOCP
/* IOCP is the only Windows backend, so a select hint is ignored */
# undef ZN_USE_SELECT
#else /* POSIX systems */
# define ZN_USE_PLATFORM_POSIX
# define ZN_USE_POSTQUEUE
# if defined(__linux__) && !defined(ZN_USE_SELECT)
#  define ZN_USE_RESULTQUEUE
#  define ZN_USE_BACKEND_REACTIVE
#  define ZN_USE_BACKEND_EPOLL
# elif defined(ZN_HAVE_KQUEUE) && !defined(ZN_USE_SELECT)
#  define ZN_USE_RESULTQUEUE
#  define ZN_USE_BACKEND_REACTIVE
#  define ZN_USE_BACKEND_KQUEUE
# elif !defined(ZN_USE_SELECT) /* select when no native backend exists */
#  define ZN_USE_SELECT
# endif
#endif /* _WIN32 */

/* op kinds: IOCP's request and the reactive result name the same operations */

typedef enum zn_RequestType {
    ZN_TACCEPT,
    ZN_TRECV,
    ZN_TSEND,
    ZN_TCONNECT,
    ZN_TRECVFROM
} zn_RequestType;

/* mempool */

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct zn_MemPool {
    void  *pages; /* page chain, link at tail */
    void  *freed; /* free slot list */
    size_t size;  /* slot size, pointer multiple */
} zn_MemPool;

/* clang-format off */
static void znM_put(zn_MemPool *pool, void *obj)
{ *(void **)obj = pool->freed, pool->freed = obj; }
/* clang-format on */

static void znM_init(zn_MemPool *pool, size_t size) {
    memset(pool, 0, sizeof(*pool)), pool->size = size;
    assert(size >= sizeof(void *) && size % sizeof(void *) == 0);
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
    const size_t size = pool->size;
    const size_t offset = ZN_PAGESIZE - sizeof(void *);
    char        *page, *end, *obj = (char *)pool->freed;
    if (obj != NULL) return (pool->freed = *(void **)obj), obj;
    page = (char *)malloc(ZN_PAGESIZE);
    if (page == NULL) return NULL;
    end = &page[offset], *(void **)end = pool->pages;
    pool->pages = page, page += size, end -= size;
    while ((page += size) <= end) *(void **)(page - size) = page;
    *(void **)(page - size) = pool->freed;
    return (pool->freed = (void *)((char *)pool->pages + size)), pool->pages;
}

/* timer */

typedef struct zn_TimerState {
    zn_MemPool timers; /* pool of zn_Timer */
    zn_Time   *keys;   /* heap order, emit per slot (parallel to nodes) */
    zn_Timer **nodes;  /* heap payload, the timers (parallel to keys) */
    unsigned   used;   /* heap occupancy; keys[0] is the earliest emit */
    unsigned   size;   /* heap capacity */
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
};

static int znT_hastimers(zn_TimerState *ts) { return ts->used != 0; }

static void znT_clear(zn_TimerState *ts) {
    znM_free(&ts->timers);
    free(ts->keys), free(ts->nodes);
    memset(ts, 0, sizeof(*ts));
}

static void znT_update(zn_TimerState *ts, zn_Time current) {
    if (ts->used == 0 || ts->keys[0] > current) return;
    while (ts->used && ts->keys[0] <= current) {
        zn_Timer *timer = ts->nodes[0];
        zn_canceltimer(timer);
        if (timer->handler) {
            zn_Time r;
            r = timer->handler(timer->u.ud, timer, current - timer->start);
            if (r > 0) zn_starttimer(timer, r);
        }
    }
}

static zn_Time znT_gettimeout(zn_TimerState *ts, zn_Time current) {
    if (ts->used == 0) return ZN_FOREVER;
    if (ts->keys[0] < current) return 0;
    return ts->keys[0] - current;
}

static int znT_resize(zn_TimerState *ts, size_t size) {
    zn_Time   *keys;
    zn_Timer **nodes;
    size_t     realsize = ZN_MIN_TIMERHEAP;
    while (realsize < size && realsize < ZN_MAX_SIZET / sizeof(zn_Timer *) / 2)
        realsize <<= 1;
    if (realsize < size) return 0;
    keys = (zn_Time *)realloc(ts->keys, realsize * sizeof(zn_Time));
    if (keys == NULL) return 0;
    ts->keys = keys; /* committed: the old block is already gone */
    nodes = (zn_Timer **)realloc(ts->nodes, realsize * sizeof(zn_Timer *));
    if (nodes == NULL) return 0;
    return (ts->nodes = nodes, ts->size = (unsigned)realsize), 1;
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
    key = timer->start + delayms;
    for (; p = (i - 1) >> 1, i > 0 && ts->keys[p] > key; i = p)
        ts->keys[i] = ts->keys[p], (ts->nodes[i] = ts->nodes[p])->index = i;
    ts->keys[i] = key, (ts->nodes[i] = timer)->index = i;
    return ZN_OK;
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    zn_TimerState *ts = timer->ts;
    unsigned       i = timer->index, p, k;
    zn_Time        key;
    if (i == ZN_TIMER_NOINDEX) return;
    timer->index = ZN_TIMER_NOINDEX;
    if (ts->used == 0 || timer == ts->nodes[--ts->used]) return;
    timer = ts->nodes[ts->used];
    key = ts->keys[ts->used];
    for (; p = (i - 1) >> 1, i > 0 && ts->keys[p] > key; i = p)
        ts->keys[i] = ts->keys[p], (ts->nodes[i] = ts->nodes[p])->index = i;
    for (; (k = i * 2 + 1) < ts->used; i = k) {
        unsigned c = k + 1;
        if (c < ts->used && ts->keys[c] < ts->keys[k]) k = c;
        if (key <= ts->keys[k]) break;
        ts->keys[i] = ts->keys[k], (ts->nodes[i] = ts->nodes[k])->index = i;
    }
    ts->keys[i] = key, (ts->nodes[i] = timer)->index = i;
}

#ifdef ZN_USE_PLATFORM_WIN32

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

typedef SOCKET zn_Socket; /* the fd type, one name for both platforms */

static int znP_closefd(zn_Socket fd) { return closesocket(fd); }

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

typedef struct zn_Request {
    OVERLAPPED     overlapped; /* must be first */
    zn_RequestType type;       /* routes the completion */
} zn_Request;

#endif /* ZN_USE_PLATFORM_WIN32 */

#ifdef ZN_USE_PLATFORM_POSIX

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef ZN_USE_BACKEND_EPOLL
# include <sys/epoll.h>
# include <sys/eventfd.h>
#endif

#ifdef ZN_USE_BACKEND_KQUEUE
# include <sys/event.h>
#endif

#ifdef __MACH__
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#endif /* __MACH__ */

#define ZN_INVALID_SOCKET (-1)

typedef int zn_Socket; /* the fd type, one name for both platforms */

static int znP_closefd(zn_Socket fd) { return close(fd); }

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

/* clang-format off */
typedef enum zn_SocketType { ZN_ACCEPT, ZN_TCP, ZN_UDP, ZN_POST } zn_SocketType;
typedef struct zn_SocketInfo { zn_SocketType type; void *head; } zn_SocketInfo;
typedef struct zn_DataBuffer { size_t len; char  *buf; } zn_DataBuffer;

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

ZN_API void zn_initialize(void) {}
ZN_API void zn_deinitialize(void) {}
static void znB_initaccept(zn_Accept *a) { (void)a; }

#endif /* ZN_USE_PLATFORM_POSIX */

/* addr */

#define znP_family(sa) ((sa)->addr.sa_family)
#define znP_isv6(sa)   (znP_family(sa) == AF_INET6)
#define znP_size(sa)   (znP_isv6(sa) ? sizeof((sa)->ipv6) : sizeof((sa)->ipv4))

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

/* types */

typedef enum zn_Status {
    ZN_STATUS_IN_RUN = -1,       /* now in zn_run() */
    ZN_STATUS_READY = 0,         /* not close */
    ZN_STATUS_CLOSING = 1,       /* prepare close */
    ZN_STATUS_CLOSING_IN_RUN = 2 /* prepare close in run() */
} zn_Status;

typedef struct zn_Post   zn_Post;
typedef struct zn_Result zn_Result;

struct zn_Post {
    zn_State       *S;       /* owning state */
    zn_PostHandler *handler; /* NULL: post is dropped */
    void           *ud;      /* handler userdata */
#ifdef ZN_USE_POSTQUEUE
    znQ_entry(zn_Post); /* queue link */
#endif
};

#ifdef ZN_USE_RESULTQUEUE

struct zn_Result {
    zn_Result *next; /* queue link */
    union {
        zn_Tcp    *tcp;    /* connect/send/recv owner */
        zn_Accept *accept; /* accept owner */
        zn_Udp    *udp;    /* recvfrom owner */
    } owner;
    int            err;  /* ZN_* result */
    zn_RequestType kind; /* which direction completed */
};
#endif

struct zn_Accept {
    znL_entry(zn_Accept);             /* active-list link */
    zn_State         *S;              /* owning state */
    void             *accept_ud;      /* handler userdata */
    zn_AcceptHandler *accept_handler; /* armed until done */
#ifdef ZN_USE_PLATFORM_WIN32
    zn_Request accept_request; /* AcceptEx overlapped */
    SOCKET     socket;         /* listener */
    SOCKET     client;         /* accepted fd, not yet tcp */
    int        family;         /* v4 or v6 */
    DWORD      recv_length;    /* addrs length from AcceptEx */
    char       recv_buffer[(sizeof(zn_SockAddr) + 16) * 2]; /* 2 addrs */
#else
    int socket; /* listener */
# ifdef ZN_USE_BACKEND_REACTIVE
    unsigned can_read : 1; /* 0 = parked on EAGAIN */
# endif
    zn_SocketInfo info; /* poll tag: type + owner */
# ifdef ZN_USE_BACKEND_REACTIVE
    zn_Result accept_result; /* embedded completion */
    zn_Tcp   *accepted;      /* queued accept payload */
# endif
#endif
};

struct zn_Tcp {
    znL_entry(zn_Tcp);                  /* active-list link */
    zn_State          *S;               /* owning state */
    void              *connect_ud;      /* connect handler userdata */
    zn_ConnectHandler *connect_handler; /* armed connect */
    void              *send_ud;         /* send handler userdata */
    zn_SendHandler    *send_handler;    /* armed send */
    void              *recv_ud;         /* recv handler userdata */
    zn_RecvHandler    *recv_handler;    /* armed recv */
    zn_PeerInfo        peer_info;       /* peer address and port */
    unsigned           deleted : 1;     /* ownership given up */
#ifdef ZN_USE_BACKEND_REACTIVE
    unsigned can_read : 1;  /* 0 = parked on EAGAIN */
    unsigned can_write : 1; /* 0 = parked on EAGAIN */
#endif
#ifdef ZN_USE_PLATFORM_WIN32
    SOCKET     socket;          /* INVALID once closed */
    WSABUF     send_buffer;     /* points at caller buf */
    WSABUF     recv_buffer;     /* points at caller buf */
    zn_Request connect_request; /* overlapped for connect */
    zn_Request send_request;    /* overlapped for send */
    zn_Request recv_request;    /* overlapped for recv */
#else
    int           socket;      /* INVALID once closed */
    zn_SocketInfo info;        /* poll tag: type + owner */
    zn_DataBuffer send_buffer; /* caller buf + len */
    zn_DataBuffer recv_buffer; /* caller buf + len */
# ifdef ZN_USE_BACKEND_REACTIVE
    zn_Result send_result;    /* embedded completion */
    zn_Result recv_result;    /* embedded completion */
    zn_Result connect_result; /* embedded completion */
# endif
#endif
};

struct zn_Udp {
    znL_entry(zn_Udp);                /* active-list link */
    zn_State           *S;            /* owning state */
    void               *recv_ud;      /* handler userdata */
    zn_RecvFromHandler *recv_handler; /* armed recv */
#ifdef ZN_USE_PLATFORM_WIN32
    zn_Request  recv_request;  /* overlapped for recv */
    SOCKET      socket;        /* INVALID once closed */
    WSABUF      recv_buffer;   /* points at caller buf */
    zn_SockAddr recv_from;     /* sender, filled on recv */
    INT         recv_from_len; /* in: capacity, out: len */
#else
    int socket; /* INVALID once closed */
# ifdef ZN_USE_BACKEND_REACTIVE
    unsigned can_read : 1; /* 0 = parked on EAGAIN */
# endif
    zn_SocketInfo info;        /* poll tag: type + owner */
    zn_DataBuffer recv_buffer; /* caller buf + len */
# ifdef ZN_USE_BACKEND_REACTIVE
    zn_Result   recv_result; /* embedded completion */
    zn_SockAddr recv_from;   /* queued sender address */
# endif
#endif
};

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
    zn_Status     status;               /* lifecycle state; gates factories */
    unsigned      waitings;             /* pending ops, run() waits */
    int           last_error;           /* last factory outcome */
#ifdef ZN_USE_POSTQUEUE
    pthread_mutex_t post_lock;    /* guards post_queue */
    znQ_head(zn_Post) post_queue; /* posts from other threads */
    zn_SocketInfo post;           /* poll tag for the post fd */
#endif
#ifdef ZN_USE_RESULTQUEUE
    znQ_head(zn_Result) result_queue; /* deferred completions */
#endif
#ifdef ZN_USE_BACKEND_REACTIVE
    unsigned nevents; /* entries in the poll batch in flight */
#endif
#ifdef ZN_USE_BACKEND_IOCP
    HANDLE           iocp;                   /* completion port */
    CRITICAL_SECTION lock;                   /* guards the posts pool */
    OVERLAPPED_ENTRY entries[ZN_MAX_EVENTS]; /* poll batch buffer */
#elif defined(ZN_USE_BACKEND_EPOLL)
    int                epoll;                 /* epoll fd */
    int                eventfd;               /* post signal */
    struct epoll_event events[ZN_MAX_EVENTS]; /* poll batch buffer */
#elif defined(ZN_USE_BACKEND_KQUEUE)
    int           kqueue;                /* kqueue fd */
    int           sockpairs[2];          /* [0] wakes [1] for post */
    struct kevent events[ZN_MAX_EVENTS]; /* poll batch buffer */
#elif defined(ZN_USE_SELECT)
    int            sockpairs[2];      /* [0] wakes [1] for post */
    int            nfds;              /* highest fd for select */
    fd_set         reg[3];            /* interest: read/write/except */
    fd_set         result[3];         /* select() result, copied from reg */
    zn_SocketInfo *infos[FD_SETSIZE]; /* fd to poll tag */
#endif
};

/* state */

#define znS_valid(obj) ((obj)->socket != ZN_INVALID_SOCKET)

#ifdef ZN_HAVE_INFO
#define znS_setinfo(obj, t) ((obj)->info.type = (t), (obj)->info.head = (obj))
#else
#define znS_setinfo(obj, t) ((void)0)
#endif

static void *znS_newslot(zn_State *S, zn_MemPool *pool, int *e) {
    void *slot;
    if (S->status > ZN_STATUS_READY) return (*e = ZN_ESTATE), (void *)0;
    slot = znM_get(pool);
    if (slot == NULL) return (*e = ZN_ERROR), (void *)0;
    return (*e = 0), memset(slot, 0, pool->size);
}

static zn_Tcp *znS_newtcpfd(zn_State *S, zn_Socket fd, zn_SockAddr *raddr);

#define znS_putobject(n) (znL_remove(n), znM_put(&(n)->S->n##s, n))
#define znS_putaccept(n) (znL_remove(n), znM_put(&(n)->S->accepts, n))

static void znB_closetcp(zn_Tcp *tcp);

/* nothing is owed once the ownership is gone and every handler has landed */
static void znS_endtcp(zn_Tcp *tcp) {
    int busy = tcp->connect_handler || tcp->send_handler || tcp->recv_handler;
    if (tcp->deleted && !busy) znS_putobject(tcp);
}

static int znS_onconnect(zn_Tcp *tcp, int err) {
    zn_ConnectHandler *h = tcp->connect_handler;
    assert(h != NULL), tcp->connect_handler = NULL;
    zn_release(tcp->S);
    if (tcp->deleted) return znS_endtcp(tcp), ZN_OK;
    /* connect is the only op in flight, so only del takes the fd away */
    if (err != ZN_OK)
        znB_closetcp(tcp);
    else if (znS_valid(tcp))
        znP_setnodelay(tcp->socket);
    return h(tcp->connect_ud, tcp, err), ZN_OK;
}

static int znS_onsend(zn_Tcp *tcp, int err, unsigned count) {
    zn_SendHandler *h = tcp->send_handler;
    assert(h != NULL), tcp->send_handler = NULL;
    memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer));
    zn_release(tcp->S);
    if (tcp->deleted) return znS_endtcp(tcp), ZN_OK;
    if (!znS_valid(tcp)) return ZN_OK; /* broken: cleanup only */
    if (err != ZN_OK) znB_closetcp(tcp);
    return h(tcp->send_ud, tcp, err, count), ZN_OK;
}

static int znS_onrecv(zn_Tcp *tcp, int err, unsigned count) {
    zn_RecvHandler *h = tcp->recv_handler;
    assert(h != NULL), tcp->recv_handler = NULL;
    memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer));
    zn_release(tcp->S);
    if (tcp->deleted) return znS_endtcp(tcp), ZN_OK;
    if (!znS_valid(tcp)) return ZN_OK; /* broken: cleanup only */
    if (err != ZN_OK) znB_closetcp(tcp);
    return h(tcp->recv_ud, tcp, err, count), ZN_OK;
}

static int znS_onaccept(zn_Accept *a, int err, zn_Tcp *tcp) {
    zn_AcceptHandler *h = a->accept_handler;
    assert(h != NULL), a->accept_handler = NULL;
    zn_release(a->S);
    if (!znS_valid(a)) return znS_putaccept(a), ZN_OK; /* deleted */
    return h(a->accept_ud, a, err, tcp), ZN_OK;
}

static int znS_onudp(zn_Udp *udp, int err, const zn_SockAddr *from,
                     unsigned bytes) {
    zn_RecvFromHandler *h = udp->recv_handler;
    zn_PeerInfo         peer;
    assert(h != NULL), udp->recv_handler = NULL;
    memset(&udp->recv_buffer, 0, sizeof(udp->recv_buffer));
    zn_release(udp->S);
    if (!znS_valid(udp)) return znS_putobject(udp), ZN_OK; /* deleted */
    if (err != ZN_OK) return h(udp->recv_ud, udp, err, 0, NULL, 0), ZN_OK;
    znP_setinfo(from, &peer);
    h(udp->recv_ud, udp, err, bytes, peer.addr, peer.port);
    return ZN_OK;
}

/* clang-format off */
#define znP_retryintr(r, e) do { r = (e); } while (r < 0 && errno == EINTR)
/* clang-format on */

/* wakeup: a socketpair byte, an eventfd bump, or an IOCP completion packet */
#if defined(ZN_USE_SELECT) || defined(ZN_USE_BACKEND_KQUEUE)
static int znB_signal(zn_State *S) {
    char c = 0;
    int  e, r;
    znP_retryintr(r, (int)send(S->sockpairs[0], &c, 1, 0));
    e = errno; /* EAGAIN: pipe is full, so a wakeup byte is already pending */
    return r == 1 || (e == EAGAIN || e == EWOULDBLOCK) ? ZN_OK : ZN_ERROR;
}
#elif defined(ZN_USE_BACKEND_EPOLL)
static int znB_signal(zn_State *S) {
    /* a full counter is still readable: the loop will wake and drain it */
    if (eventfd_write(S->eventfd, (eventfd_t)1) == 0) return ZN_OK;
    return errno == EAGAIN ? ZN_OK : ZN_ERROR;
}
#endif

#ifdef ZN_USE_POSTQUEUE /* need: state, list */

/* clang-format off */
static void znS_initposts(zn_State *S)
{ pthread_mutex_init(&S->post_lock, 0), znQ_init(&S->post_queue); }
/* clang-format on */

static void znS_processposts(zn_State *S) {
    zn_Post *post;
    pthread_mutex_lock(&S->post_lock);
    post = znQ_first(&S->post_queue);
    znQ_init(&S->post_queue);
    pthread_mutex_unlock(&S->post_lock);
    znQ_apply(zn_Post, post, if (cur->handler) cur->handler(cur->ud, cur->S));
    pthread_mutex_lock(&S->post_lock); /* the pool is shared with zn_post */
    znQ_apply(zn_Post, post, znM_put(&S->posts, cur));
    pthread_mutex_unlock(&S->post_lock);
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *h, void *ud) {
    zn_Post *post;
    int      r = ZN_ERROR;
    if (S->status > ZN_STATUS_READY) return ZN_ERROR; /* closing: no delivery */
    pthread_mutex_lock(&S->post_lock);
    post = (zn_Post *)znM_get(&S->posts);
    if (post != NULL) {
        post->S = S;
        post->handler = h, post->ud = ud;
        znQ_enqueue(&S->post_queue, post);
        /* no wakeup means it can never run: drop it while still locked */
        r = znB_signal(S);
        if (r != ZN_OK) post->handler = NULL;
    }
    pthread_mutex_unlock(&S->post_lock);
    return r;
}

#endif /* ZN_USE_POSTQUEUE */

#ifdef ZN_USE_RESULTQUEUE /* need: state, list */

#define ZN_MAX_RESULT_LOOPS 100

static void znR_init(zn_State *S) { znQ_init(&S->result_queue); }

/* clang-format off */
static void znR_add(zn_State *S, int err, zn_Result *result)
{ result->err = err, znQ_enqueue(&S->result_queue, result); }
/* clang-format on */

static void znR_dispatch(zn_Result *result) {
    zn_Accept *a;
    zn_Tcp    *tcp;
    zn_Udp    *udp;
    unsigned   count;
    switch (result->kind) {
    case ZN_TSEND:
        tcp = result->owner.tcp;
        count = result->err == ZN_OK ? (unsigned)tcp->send_buffer.len : 0;
        znS_onsend(tcp, result->err, count);
        break;
    case ZN_TRECV:
        tcp = result->owner.tcp;
        count = result->err == ZN_OK ? (unsigned)tcp->recv_buffer.len : 0;
        znS_onrecv(tcp, result->err, count);
        break;
    case ZN_TCONNECT: znS_onconnect(result->owner.tcp, result->err); break;
    case ZN_TACCEPT:
        a = result->owner.accept;
        tcp = a->accepted, a->accepted = NULL;
        znS_onaccept(a, result->err, tcp);
        break;
    case ZN_TRECVFROM:
        udp = result->owner.udp;
        count = result->err == ZN_OK ? (unsigned)udp->recv_buffer.len : 0;
        znS_onudp(udp, result->err, &udp->recv_from, count);
        break;
    }
}

static int znR_process(zn_State *S, int max) {
    int count = 0;
    while (!znQ_empty(&S->result_queue) && count++ < max) {
        zn_Result *results = znQ_first(&S->result_queue);
        znQ_init(&S->result_queue);
        znQ_apply(zn_Result, results, znR_dispatch(cur));
    }
    return !znQ_empty(&S->result_queue);
}

#endif /* ZN_USE_RESULTQUEUE */

/* posix udp syscalls: select and reactive would otherwise carry a copy each */

#ifdef ZN_USE_PLATFORM_POSIX

static int znB_connectudp(zn_Udp *udp, zn_SockAddr *addr) {
    if (connect(udp->socket, &addr->addr, znP_size(addr)) == 0) return ZN_OK;
    if (errno == EAFNOSUPPORT || errno == EINVAL) return ZN_EPARAM;
    return znP_error(errno);
}

static int znB_sendto(zn_Udp *udp, const char *buff, unsigned len,
                      zn_SockAddr *addr) {
    int r, s = udp->socket;
    if (addr == NULL) {
        znP_retryintr(r, (int)send(s, buff, len, ZN_NOSIGNAL));
        if (r >= 0) return ZN_OK;
        if (errno == EDESTADDRREQ || errno == ENOTCONN) return ZN_ESTATE;
        return znP_error(errno);
    }
    znP_retryintr(r, (int)sendto(s, buff, len, 0, &addr->addr, znP_size(addr)));
    return r < 0 ? znP_error(errno) : ZN_OK;
}

#endif /* ZN_USE_PLATFORM_POSIX */

/* backends */

#ifdef ZN_USE_BACKEND_IOCP

static BOOL znP_started = FALSE;

typedef BOOL(WINAPI *LPGETQUEUEDCOMPLETIONSTATUSEX)(
        _In_ HANDLE              CompletionPort,
        _Out_ LPOVERLAPPED_ENTRY lpCompletionPortEntries, _In_ ULONG ulCount,
        _Out_ PULONG ulNumEntriesRemoved, _In_ DWORD dwMilliseconds,
        _In_ BOOL fAlertable);

static LPGETQUEUEDCOMPLETIONSTATUSEX pGetQueuedCompletionStatusEx;

ZN_API const char *zn_engine(void) { return "IOCP"; }

ZN_API void zn_initialize(void) {
    if (!znP_started) {
        HMODULE kernel32;
        WORD    version = MAKEWORD(2, 2);
        WSADATA d;
        if (WSAStartup(version, &d) != 0)
            assert(!"can not initialize Windows sockets!"), abort();
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

static void znB_initaccept(zn_Accept *a)
{ a->accept_request.type = ZN_TACCEPT, a->client = INVALID_SOCKET; }

static void znB_closetcp(zn_Tcp *tcp)
{ if (znS_valid(tcp)) closesocket(tcp->socket), tcp->socket = INVALID_SOCKET; }
/* clang-format on */

/* tcp */

static int znP_getextension(SOCKET socket, GUID *gid, void *fn) {
    DWORD dwSize = 0;
    return !WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, gid,
                     sizeof(*gid), fn, sizeof(void(PASCAL *)(void)), &dwSize,
                     NULL, NULL);
}

static int znB_tcpfromfamily(zn_Tcp *tcp, int family) {
    SOCKET      fd;
    zn_SockAddr laddr;
    fd = WSASocket(family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
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

static int znB_adoptfd(zn_Tcp *tcp) {
    HANDLE hRet = CreateIoCompletionPort((HANDLE)tcp->socket, tcp->S->iocp,
                                         (ULONG_PTR)tcp, 1);
    return hRet ? ZN_OK : ZN_EPOLL;
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr) {
    static LPFN_CONNECTEX lpConnectEx = NULL;

    int         err = znB_tcpfromfamily(tcp, znP_family(addr));
    static GUID gid = WSAID_CONNECTEX;
    DWORD       dwLength = 0;
    char        buf[1];
    if (err != ZN_OK) return err;
    /* leave the object reusable, not holding a socket */
    if (!lpConnectEx && !znP_getextension(tcp->socket, &gid, &lpConnectEx))
        return znB_closetcp(tcp), ZN_ECONNECT;
    if (!lpConnectEx(tcp->socket, &addr->addr, znP_size(addr), buf, 0,
                     &dwLength, &tcp->connect_request.overlapped)
        && WSAGetLastError() != ERROR_IO_PENDING)
        return znB_closetcp(tcp), ZN_ECONNECT;
    return ZN_OK;
}

static int znB_send(zn_Tcp *tcp) {
    DWORD dwTemp1 = 0;
    int   err, r;
    r = WSASend(tcp->socket, &tcp->send_buffer, 1, &dwTemp1, 0,
                &tcp->send_request.overlapped, NULL);
    if (r != 0 && (err = WSAGetLastError()) != WSA_IO_PENDING)
        return znB_closetcp(tcp), znP_error(err);
    return ZN_OK;
}

static int znB_recv(zn_Tcp *tcp) {
    DWORD dwRecv = 0, dwFlag = 0;
    int   err, r;
    r = WSARecv(tcp->socket, &tcp->recv_buffer, 1, &dwRecv, &dwFlag,
                &tcp->recv_request.overlapped, NULL);
    if (r != 0 && (err = WSAGetLastError()) != WSA_IO_PENDING)
        return znB_closetcp(tcp), znP_error(err);
    return ZN_OK;
}

static int znB_onrecv(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    int err = ZN_OK;
    assert(tcp->recv_handler);
    if (!bSuccess)
        err = ZN_EHANGUP;
    else if (dwBytes == 0)
        err = ZN_ECLOSED;
    return znS_onrecv(tcp, err, (unsigned)dwBytes);
}

/* accept */

static void znB_closeaccept(zn_Accept *a) {
    if (znS_valid(a)) closesocket(a->socket), a->socket = INVALID_SOCKET;
    if (a->client != INVALID_SOCKET)
        closesocket(a->client), a->client = INVALID_SOCKET;
}

static int znB_listen(zn_Accept *a, zn_SockAddr *addr) {
    SOCKET fd;
    a->family = znP_family(addr);
    fd = WSASocket(a->family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    znP_setexclusiveaddruse(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0)
        return closesocket(fd), ZN_EBIND;
    if (listen(fd, SOMAXCONN) != 0) return closesocket(fd), ZN_ERROR;
    if (!CreateIoCompletionPort((HANDLE)fd, a->S->iocp, (ULONG_PTR)a, 1))
        return closesocket(fd), ZN_EPOLL;
    return (a->socket = fd), ZN_OK;
}

static int znB_accept(zn_Accept *a) {
    static GUID          gid = WSAID_ACCEPTEX;
    static LPFN_ACCEPTEX lpAcceptEx = NULL;
    SOCKET               fd;
    if (!lpAcceptEx && !znP_getextension(a->socket, &gid, &lpAcceptEx))
        return ZN_ERROR;
    fd = WSASocket(a->family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                   WSA_FLAG_OVERLAPPED);
    if (fd == INVALID_SOCKET) return ZN_ESOCKET;
    if (!lpAcceptEx(a->socket, fd, a->recv_buffer, 0, sizeof(zn_SockAddr) + 16,
                    sizeof(zn_SockAddr) + 16, &a->recv_length,
                    &a->accept_request.overlapped)
        && WSAGetLastError() != ERROR_IO_PENDING)
        return closesocket(fd), ZN_ERROR;
    return (a->client = fd), ZN_OK;
}

static int znB_failaccept(zn_Accept *a) {
    if (a->client != INVALID_SOCKET)
        closesocket(a->client), a->client = INVALID_SOCKET;
    return znS_onaccept(a, ZN_ERROR, NULL);
}

static int znB_onaccept(zn_Accept *a, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID                      gid = WSAID_GETACCEPTEXSOCKADDRS;
    struct sockaddr                 *paddr1 = NULL, *paddr2 = NULL;
    int                              tmp1 = 0, tmp2 = 0;
    zn_Tcp                          *tcp;
    assert(a->accept_handler != NULL); /* one completion per accept */
    if (!znS_valid(a))                 /* deleted while armed: cleanup only */
        return znS_onaccept(a, ZN_ERROR, NULL);
    if (!bSuccess) return znB_failaccept(a);
    znP_updateacceptinfo(a->socket, a->client);
    znP_setnodelay(a->client);
    if (!lpGetAcceptExSockaddrs
        && !znP_getextension(a->client, &gid, &lpGetAcceptExSockaddrs))
        return znB_failaccept(a);
    lpGetAcceptExSockaddrs(a->recv_buffer, a->recv_length,
                           sizeof(zn_SockAddr) + 16, sizeof(zn_SockAddr) + 16,
                           &paddr1, &tmp1, &paddr2, &tmp2);
    tcp = znS_newtcpfd(a->S, a->client, (zn_SockAddr *)paddr2);
    a->client = INVALID_SOCKET; /* closed by the tcp unless it is live */
    return znS_onaccept(a, tcp ? ZN_OK : ZN_ERROR, tcp);
}

/* udp */

static void znB_closeudp(zn_Udp *udp) {
    if (!znS_valid(udp)) return; /* del already closed it */
    closesocket(udp->socket), udp->socket = ZN_INVALID_SOCKET;
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

static int znB_connectudp(zn_Udp *udp, zn_SockAddr *addr) {
    int e;
    if (connect(udp->socket, &addr->addr, (int)znP_size(addr)) != SOCKET_ERROR)
        return ZN_OK;
    e = (int)WSAGetLastError();
    if (e == WSAEAFNOSUPPORT || e == WSAEINVAL) return ZN_EPARAM;
    return znP_error(e);
}

static int znB_sendto(zn_Udp *udp, const char *buff, unsigned len,
                      zn_SockAddr *addr) {
    int r, e;
    if (addr == NULL) {
        r = send(udp->socket, buff, (int)len, 0);
        if (r != SOCKET_ERROR) return ZN_OK;
        e = (int)WSAGetLastError();
        if (e == WSAENOTCONN || e == WSAEDESTADDRREQ) return ZN_ESTATE;
        return znP_error(e);
    }
    r = sendto(udp->socket, buff, (int)len, 0, &addr->addr, znP_size(addr));
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

/* poll */

ZN_API int zn_post(zn_State *S, zn_PostHandler *h, void *ud) {
    zn_Post *post;
    if (S->status > ZN_STATUS_READY) return ZN_ERROR; /* closing: no delivery */
    EnterCriticalSection(&S->lock);
    post = (zn_Post *)znM_get(&S->posts);
    LeaveCriticalSection(&S->lock);
    if (post == NULL) return ZN_ERROR;
    post->S = S;
    post->handler = h, post->ud = ud;
    if (!PostQueuedCompletionStatus(S->iocp, 0, 0, (LPOVERLAPPED)post)) {
        EnterCriticalSection(&S->lock);
        znM_put(&S->posts, post);
        LeaveCriticalSection(&S->lock);
        return ZN_ERROR;
    }
    return ZN_OK;
}

static int znB_init(zn_State *S) {
    S->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    return InitializeCriticalSection(&S->lock), S->iocp != NULL;
}

static void znB_dispatch(BOOL bRet, LPOVERLAPPED_ENTRY pEntry) {
    zn_Request *req = (zn_Request *)pEntry->lpOverlapped;
    ULONG_PTR   key = pEntry->lpCompletionKey;
    DWORD       bytes = pEntry->dwNumberOfBytesTransferred;
    zn_Tcp     *tcp = (zn_Tcp *)key; /* the request type names the object */
    zn_Udp     *udp = (zn_Udp *)key;
    switch (req->type) {
    case ZN_TACCEPT: znB_onaccept((zn_Accept *)key, bRet); break;
    case ZN_TRECV: znB_onrecv(tcp, bRet, bytes); break;
    case ZN_TSEND:
        znS_onsend(tcp, bRet ? ZN_OK : ZN_ERROR, (unsigned)bytes);
        break;
    case ZN_TCONNECT: znS_onconnect(tcp, bRet ? ZN_OK : ZN_ERROR); break;
    case ZN_TRECVFROM:
        /* a zero-byte datagram is a successful receive too */
        znS_onudp(udp, bRet ? ZN_OK : ZN_ERROR, bRet ? &udp->recv_from : NULL,
                  (unsigned)bytes);
        break;
    }
}

static void znB_post(zn_State *S, zn_Post *post) {
    if (post->handler) post->handler(post->ud, post->S);
    EnterCriticalSection(&S->lock); /* the pool is shared */
    znM_put(&S->posts, post);
    LeaveCriticalSection(&S->lock);
}

static ULONG znB_read(zn_State *S, DWORD ms) {
    OVERLAPPED_ENTRY entry;
    ULONG            i, count;
    BOOL             bRet;
    if (!pGetQueuedCompletionStatusEx) {
        bRet = GetQueuedCompletionStatus(
                S->iocp, &entry.dwNumberOfBytesTransferred,
                &entry.lpCompletionKey, &entry.lpOverlapped, ms);
        if (!bRet && !entry.lpOverlapped) return 0; /* time out */
        if (entry.lpCompletionKey == 0)
            return znB_post(S, (zn_Post *)entry.lpOverlapped), 1;
        return znB_dispatch(bRet, &entry), 1;
    }
    bRet = pGetQueuedCompletionStatusEx(S->iocp, S->entries, ZN_MAX_EVENTS,
                                        &count, ms, FALSE);
    if (!bRet) return 0; /* time out */
    for (i = 0; i < count; ++i) {
        DWORD transfer;
        if (S->entries[i].lpCompletionKey == 0) {
            znB_post(S, (zn_Post *)S->entries[i].lpOverlapped);
            continue;
        }
        bRet = GetOverlappedResult(S->iocp, S->entries[i].lpOverlapped,
                                   &transfer, FALSE);
        znB_dispatch(bRet, &S->entries[i]);
    }
    return count;
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    DWORD ms = INFINITE;
    if (timeout < ZN_FOREVER)
        ms = timeout >= (zn_Time)INFINITE ? INFINITE - 1 : (DWORD)timeout;
    return (void)znB_read(S, ms), 0;
}

static void znB_close(zn_State *S) {
    /* CLOSING stays set: a post must not re-enter close or create objects */
    while (S->waitings > 0) znB_read(S, INFINITE);
    /* a post queued before the close is still promised delivery */
    while (znB_read(S, 0) != 0) continue;
    CloseHandle(S->iocp);
    DeleteCriticalSection(&S->lock);
}

#endif /* ZN_USE_BACKEND_IOCP */

/* select: forced with ZN_USE_SELECT, or the POSIX fallback */
#ifdef ZN_USE_SELECT

typedef enum zn_FdSet { ZN_READ, ZN_WRITE, ZN_EXCEPT } zn_FdSet;

static void znB_inittcp(zn_Tcp *tcp) { (void)tcp; }

ZN_API const char *zn_engine(void) { return "select"; }

static void znF_regread(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->reg[ZN_READ]);
    FD_SET(fd, &S->reg[ZN_EXCEPT]);
}

static void znF_regwrite(zn_State *S, int fd, zn_SocketInfo *info) {
    assert(fd < FD_SETSIZE);
    S->infos[fd] = info;
    FD_SET(fd, &S->reg[ZN_WRITE]);
    FD_SET(fd, &S->reg[ZN_EXCEPT]);
}

static void znF_unregread(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    FD_CLR(fd, &S->reg[ZN_READ]);
    FD_CLR(fd, &S->reg[ZN_EXCEPT]);
    FD_CLR(fd, &S->result[ZN_READ]);
    FD_CLR(fd, &S->result[ZN_EXCEPT]); /* a re-arm must not see it */
}

static void znF_unregwrite(zn_State *S, int fd) {
    if (fd >= FD_SETSIZE) return;
    FD_CLR(fd, &S->reg[ZN_WRITE]);
    FD_CLR(fd, &S->reg[ZN_EXCEPT]);
    FD_CLR(fd, &S->result[ZN_WRITE]);
    FD_CLR(fd, &S->result[ZN_EXCEPT]); /* a re-arm must not see it */
}

static void znF_unregister(zn_State *S, int fd) {
    int i;
    if (fd >= FD_SETSIZE) return;
    S->infos[fd] = NULL;
    for (i = 0; i < 3; ++i) {
        FD_CLR(fd, &S->reg[i]);
        FD_CLR(fd, &S->result[i]);
    }
}

/* tcp */

/* clang-format off */
static int znB_send(zn_Tcp *tcp)
{ return znF_regwrite(tcp->S, tcp->socket, &tcp->info), ZN_OK; }

static int znB_recv(zn_Tcp *tcp)
{ return znF_regread(tcp->S, tcp->socket, &tcp->info), ZN_OK; }
/* clang-format on */

static int znB_adoptfd(zn_Tcp *tcp) {
    assert(tcp->socket < FD_SETSIZE), znP_setnosigpipe(tcp->socket);
    return znP_setnonblock(tcp->socket), znP_setnodelay(tcp->socket), ZN_OK;
}

static void znB_closetcp(zn_Tcp *tcp) {
    if (tcp->connect_handler) zn_release(tcp->S);
    if (tcp->send_handler) zn_release(tcp->S);
    if (tcp->recv_handler) zn_release(tcp->S);
    tcp->connect_handler = NULL;
    tcp->send_handler = NULL;
    tcp->recv_handler = NULL;
    if (znS_valid(tcp)) {
        znF_unregister(tcp->S, tcp->socket);
        close(tcp->socket), tcp->socket = ZN_INVALID_SOCKET;
    }
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr) {
    int r, fd = socket(znP_family(addr), SOCK_STREAM, 0);
    if (fd < 0) return ZN_ESOCKET;
    if (fd >= FD_SETSIZE) return close(fd), ZN_ESOCKET;
    if (tcp->S->nfds < fd) tcp->S->nfds = fd;
    znP_setnonblock(fd), znP_setnosigpipe(fd);
    r = connect(fd, &addr->addr, znP_size(addr));
    if (r != 0 && errno != EINPROGRESS) return close(fd), ZN_ECONNECT;
    return (tcp->socket = fd), znF_regwrite(tcp->S, fd, &tcp->info), ZN_OK;
}

static int znB_onconnect(zn_Tcp *tcp, int err) {
    socklen_t len = (socklen_t)sizeof(err);
    if (tcp->connect_handler == NULL) return ZN_OK; /* closed by a callback */
    znF_unregwrite(tcp->S, tcp->socket);
    /* a refused connect is also write-ready: SO_ERROR holds the verdict */
    if (getsockopt(tcp->socket, SOL_SOCKET, SO_ERROR, (void *)&err, &len))
        err = 0;
    return znS_onconnect(tcp, err ? ZN_ERROR : ZN_OK);
}

static int znB_onsend(zn_Tcp *tcp, int err) {
    zn_DataBuffer b = tcp->send_buffer;
    int           bytes, syserr = 0;
    if (tcp->send_handler == NULL) return ZN_OK; /* closed by a callback */
    if (err) {
        znF_unregwrite(tcp->S, tcp->socket);
        return znS_onsend(tcp, ZN_ERROR, 0);
    }
    if ((bytes = send(tcp->socket, b.buf, b.len, ZN_NOSIGNAL)) < 0) {
        syserr = errno; /* EAGAIN: no room; EINTR: LT re-reports */
        if (syserr == EINTR || syserr == EAGAIN || syserr == EWOULDBLOCK)
            return ZN_OK;
    }
    znF_unregwrite(tcp->S, tcp->socket);
    if (bytes < 0) return znS_onsend(tcp, znP_error(syserr), 0);
    return znS_onsend(tcp, ZN_OK, (unsigned)bytes);
}

static int znB_onrecv(zn_Tcp *tcp, int err) {
    zn_DataBuffer b = tcp->recv_buffer;
    int           bytes, syserr = 0;
    if (tcp->recv_handler == NULL) return ZN_OK; /* closed by a callback */
    if (err)
        return znF_unregread(tcp->S, tcp->socket), znS_onrecv(tcp, ZN_ERROR, 0);
    if ((bytes = recv(tcp->socket, b.buf, b.len, 0)) < 0) {
        syserr = errno; /* EAGAIN: no data; EINTR: LT re-reports */
        if (syserr == EINTR || syserr == EAGAIN || syserr == EWOULDBLOCK)
            return ZN_OK;
    }
    znF_unregread(tcp->S, tcp->socket);
    if (bytes > 0) return znS_onrecv(tcp, ZN_OK, (unsigned)bytes);
    return znS_onrecv(tcp, bytes == 0 ? ZN_ECLOSED : znP_error(syserr), 0);
}

/* accept */

/* clang-format off */
static int znB_accept(zn_Accept *a)
{ return znF_regread(a->S, a->socket, &a->info), ZN_OK; }

static void znB_close(zn_State *S)
{ znS_processposts(S), close(S->sockpairs[0]), close(S->sockpairs[1]); }
/* clang-format on */

static void znB_closeaccept(zn_Accept *a) {
    if (a->accept_handler) zn_release(a->S);
    a->accept_handler = NULL;
    if (znS_valid(a)) {
        znF_unregister(a->S, a->socket);
        close(a->socket), a->socket = ZN_INVALID_SOCKET;
    }
}

static int znB_listen(zn_Accept *a, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return ZN_ESOCKET;
    if (fd >= FD_SETSIZE) return close(fd), ZN_ESOCKET;
    if (a->S->nfds < fd) a->S->nfds = fd;
    znP_setnonblock(fd), znP_setreuseaddr(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    if (listen(fd, SOMAXCONN) != 0) return close(fd), ZN_ERROR;
    return (a->socket = fd), ZN_OK;
}

static int znB_onaccept(zn_Accept *a, int err) {
    zn_SockAddr raddr;
    socklen_t   size = sizeof(raddr);
    int         fd, syserr, again;
    zn_Tcp     *tcp;
    if (a->accept_handler == NULL) return ZN_OK; /* closed by a callback */
    if (err)
        return znF_unregread(a->S, a->socket), znS_onaccept(a, ZN_ERROR, NULL);
    if ((fd = accept(a->socket, &raddr.addr, &size)) < 0) {
        /* nothing was lost; the next report retries */
        if ((syserr = errno) == EINTR || syserr == ECONNABORTED) return ZN_OK;
        /* EAGAIN: the report and the queue disagreed; surface it once */
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        znF_unregread(a->S, a->socket);
        return znS_onaccept(a, again ? ZN_ERROR : znP_error(syserr), NULL);
    }
    /* select can never track this connection */
    if (fd >= FD_SETSIZE) return close(fd), ZN_OK;
    znF_unregread(a->S, a->socket);
    tcp = znS_newtcpfd(a->S, fd, &raddr);
    if (tcp == NULL) return znS_onaccept(a, ZN_ERROR, NULL);
    if (a->S->nfds < fd) a->S->nfds = fd;
    return znS_onaccept(a, ZN_OK, tcp);
}

/* udp */

static int znB_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;
    if (fd >= FD_SETSIZE) return close(fd), ZN_ESOCKET;
    znP_setnonblock(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    return (udp->socket = fd), ZN_OK;
}

static void znB_closeudp(zn_Udp *udp) {
    if (!znS_valid(udp)) return; /* del already closed it */
    znF_unregister(udp->S, udp->socket);
    if (udp->recv_handler) zn_release(udp->S);
    udp->recv_handler = NULL;
    close(udp->socket), udp->socket = ZN_INVALID_SOCKET;
}

static int znB_recvfrom(zn_Udp *udp) {
    znF_regread(udp->S, udp->socket, &udp->info);
    if (udp->S->nfds < udp->socket) udp->S->nfds = udp->socket;
    return ZN_OK;
}

static int znB_onrecvfrom(zn_Udp *udp, int err) {
    zn_DataBuffer b = udp->recv_buffer;
    zn_SockAddr   raddr;
    socklen_t     size;
    int           bytes, syserr, again;
    if (udp->recv_handler == NULL) return ZN_OK; /* closed by a callback */
    if (err) {
        znF_unregread(udp->S, udp->socket);
        return znS_onudp(udp, ZN_ERROR, NULL, 0);
    }
    memset(&raddr, 0, size = sizeof(raddr));
    bytes = recvfrom(udp->socket, b.buf, b.len, 0, &raddr.addr, &size);
    if (bytes < 0) {
        if ((syserr = errno) == EINTR) return ZN_OK; /* LT re-reports */
        again = (syserr == EAGAIN || syserr == EWOULDBLOCK);
        znF_unregread(udp->S, udp->socket);
        return znS_onudp(udp, again ? ZN_ERROR : znP_error(syserr), NULL, 0);
    }
    znF_unregread(udp->S, udp->socket);
    return znS_onudp(udp, ZN_OK, &raddr, (unsigned)bytes);
}

/* poll */

static int znB_dispatch(zn_State *S, int fd, int setno) {
    char           buff[ZN_PAGESIZE];
    int            err = (setno == ZN_EXCEPT);
    zn_SocketInfo *info;
    zn_Tcp        *tcp;
    if (fd < 0 || fd >= FD_SETSIZE) return ZN_OK;
    info = S->infos[fd];
    if (info == NULL) return ZN_OK; /* closed by an earlier callback */
    switch (info->type) {
    case ZN_ACCEPT: return znB_onaccept((zn_Accept *)info->head, err), ZN_OK;
    case ZN_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler) return znB_onconnect(tcp, err), ZN_OK;
        /* ZN_EXCEPT reports both directions: dispatch one, then stop */
        if (tcp->send_handler && setno != ZN_READ) return znB_onsend(tcp, err);
        if (tcp->recv_handler && setno != ZN_WRITE) return znB_onrecv(tcp, err);
        return ZN_OK;
    case ZN_UDP: return znB_onrecvfrom((zn_Udp *)info->head, err), ZN_OK;
    default:
        assert(info->type == ZN_POST);
        while (recv(fd, buff, ZN_PAGESIZE, 0) > 0) continue;
        return znS_processposts(S), ZN_OK;
    }
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    struct timeval tv, *ptv = NULL;
    int            fd, i, r;
    if (timeout < ZN_FOREVER) {
        tv.tv_sec = timeout / 1000, tv.tv_usec = timeout % 1000 * 1000;
        ptv = &tv;
    }
    for (i = 0; i < 3; ++i) S->result[i] = S->reg[i];
    r = select(S->nfds + 1, &S->result[ZN_READ], &S->result[ZN_WRITE],
               &S->result[ZN_EXCEPT], ptv);
    /* an interrupted select leaves the sets undefined */
    if (r < 0) return 0;
    for (fd = 0; fd <= S->nfds; ++fd)
        for (i = 0; i < 3; ++i)
            if (FD_ISSET(fd, &S->result[i])) znB_dispatch(S, fd, i);
    return 0;
}

static int znB_init(zn_State *S) {
    int i;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0) return 0;
    if (S->sockpairs[1] >= FD_SETSIZE)
        return close(S->sockpairs[0]), close(S->sockpairs[1]), 0;
    znS_initposts(S);
    for (i = 0; i < 3; ++i) FD_ZERO(&S->reg[i]);
    FD_SET(S->sockpairs[1], &S->reg[ZN_READ]);
    if (S->nfds < S->sockpairs[1]) S->nfds = S->sockpairs[1];
    S->post.type = ZN_POST, S->infos[S->sockpairs[1]] = &S->post;
    znP_setnonblock(S->sockpairs[0]), znP_setnonblock(S->sockpairs[1]);
    return 1;
}

#endif /* select backend */

#ifdef ZN_USE_BACKEND_REACTIVE /* reactive model supporting edge triggering */

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info, int et);
static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info);
static int znF_unregister(zn_State *S, int fd, zn_SocketInfo *info);

static int znB_sysfail(zn_State *S, zn_Result *r, int syserr, int again_err) {
    if (syserr != EAGAIN && syserr != EWOULDBLOCK)
        return znR_add(S, znP_error(syserr), r), 1;
    /* a stale readiness report is surfaced once; a retried one is real */
    return again_err == ZN_OK ? 0 : (znR_add(S, again_err, r), 1);
}

static int znB_tryaccept(zn_Accept *a, int again_err) {
    zn_SockAddr raddr;
    zn_Tcp     *tcp;
    socklen_t   size;
    int         fd, syserr;
    do {
        memset(&raddr, 0, size = (socklen_t)sizeof(raddr));
        fd = accept(a->socket, &raddr.addr, &size);
        if (fd >= 0) break; /* isolated: leave with a connection */
        syserr = errno;
        if (syserr == EINTR || syserr == ECONNABORTED) again_err = ZN_OK;
    } while (syserr == EINTR || syserr == ECONNABORTED);
    if (fd < 0) return znB_sysfail(a->S, &a->accept_result, syserr, again_err);
    tcp = znS_newtcpfd(a->S, fd, &raddr);
    if (tcp == NULL) return znR_add(a->S, ZN_ERROR, &a->accept_result), 1;
    return (a->accepted = tcp), znR_add(a->S, ZN_OK, &a->accept_result), 1;
}

static int znB_tryrecvfrom(zn_Udp *udp, int again_err) {
    zn_DataBuffer b = udp->recv_buffer;
    zn_SockAddr   raddr;
    socklen_t     size;
    int           bytes, syserr;
    do {
        memset(&raddr, 0, size = (socklen_t)sizeof(raddr));
        bytes = (int)recvfrom(udp->socket, b.buf, b.len, 0, &raddr.addr, &size);
        if (bytes >= 0) break; /* isolated: leave with a datagram */
        if ((syserr = errno) == EINTR) again_err = ZN_OK;
    } while (syserr == EINTR);
    if (bytes < 0)
        return znB_sysfail(udp->S, &udp->recv_result, syserr, again_err);
    udp->recv_from = raddr, udp->recv_buffer.len = (unsigned)bytes;
    return znR_add(udp->S, ZN_OK, &udp->recv_result), 1;
}

static int znB_onconnect(zn_Tcp *tcp, int err) {
    int       soerr = 0;
    socklen_t len = (socklen_t)sizeof(soerr);
    if (tcp->connect_handler == NULL) return ZN_OK; /* closed by a callback */
    if (tcp->can_write) return ZN_OK;               /* result already queued */
    /* a refused connect is also write-ready: SO_ERROR holds the verdict */
    if (err == ZN_OK
        && (getsockopt(tcp->socket, SOL_SOCKET, SO_ERROR, (void *)&soerr, &len)
            || soerr != 0))
        err = ZN_ERROR;
    tcp->can_write = 1;
    return znR_add(tcp->S, err, &tcp->connect_result), ZN_OK;
}

static int znB_onaccept(zn_Accept *a, int err) {
    if (a->accept_handler == NULL) return ZN_OK; /* not armed / closed */
    if (a->can_read) return ZN_OK;               /* result already queued */
    if (err != ZN_OK) {
        a->can_read = 1; /* queued without a syscall */
        return znR_add(a->S, ZN_ERROR, &a->accept_result), ZN_OK;
    }
    return (a->can_read = znB_tryaccept(a, ZN_ERROR) != 0), ZN_OK;
}

static int znB_onrecvfrom(zn_Udp *udp, int err) {
    if (udp->recv_handler == NULL) return ZN_OK; /* not armed / closed */
    if (udp->can_read) return ZN_OK;             /* result already queued */
    if (err != ZN_OK) {
        udp->can_read = 1; /* queued without a syscall */
        return znR_add(udp->S, ZN_ERROR, &udp->recv_result), ZN_OK;
    }
    return (udp->can_read = znB_tryrecvfrom(udp, ZN_ERROR) != 0), ZN_OK;
}

/* clang-format off */
static int znB_accept(zn_Accept *a)
{ return (a->can_read = znB_tryaccept(a, ZN_OK) != 0), ZN_OK; }

static int znB_recvfrom(zn_Udp *udp)
{ return (udp->can_read = znB_tryrecvfrom(udp, ZN_OK) != 0), ZN_OK; }
/* clang-format on */

/* tcp */

static int znB_adoptfd(zn_Tcp *tcp) {
    znP_setnonblock(tcp->socket), znP_setnosigpipe(tcp->socket);
    znP_setnodelay(tcp->socket);
    return znF_regall(tcp->S, tcp->socket, &tcp->info) ? ZN_OK : ZN_EPOLL;
}

static void znB_inittcp(zn_Tcp *tcp) {
    tcp->can_read = tcp->can_write = 1;
    tcp->send_result.kind = ZN_TSEND;
    tcp->recv_result.kind = ZN_TRECV;
    tcp->connect_result.kind = ZN_TCONNECT;
    tcp->send_result.owner.tcp = tcp;
    tcp->recv_result.owner.tcp = tcp;
    tcp->connect_result.owner.tcp = tcp;
}

static void znB_closetcp(zn_Tcp *tcp) {
    /* completing: the queued result keeps its handler for znR_process */
    if (tcp->connect_handler != NULL && !tcp->can_write)
        zn_release(tcp->S), tcp->connect_handler = NULL;
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
        znF_unregister(tcp->S, tcp->socket, &tcp->info);
        close(tcp->socket), tcp->socket = ZN_INVALID_SOCKET;
    }
}

static int znB_connect(zn_Tcp *tcp, zn_SockAddr *addr) {
    int r, fd;
    if ((fd = socket(znP_family(addr), SOCK_STREAM, 0)) < 0) return ZN_ESOCKET;
    znP_setnonblock(fd), znP_setnosigpipe(fd); /* before the first send() */
    r = connect(fd, &addr->addr, znP_size(addr));
    if (r != 0 && errno != EINPROGRESS) return close(fd), ZN_ECONNECT;
    if (!znF_regall(tcp->S, fd, &tcp->info)) return close(fd), ZN_EPOLL;
    tcp->socket = fd;
    if (r != 0) return (tcp->can_write = 0), ZN_OK; /* wait for the edge */
    /* connected already: no edge is coming, so queue the verdict now */
    tcp->can_write = 1;
    return znR_add(tcp->S, ZN_OK, &tcp->connect_result), ZN_OK;
}

static int znB_send(zn_Tcp *tcp) {
    zn_DataBuffer *b = &tcp->send_buffer;
    int            bytes;
    if (!tcp->can_write) return ZN_OK;
    znP_retryintr(bytes, (int)send(tcp->socket, b->buf, b->len, ZN_NOSIGNAL));
    if (bytes >= 0)
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
    znP_retryintr(bytes, (int)recv(tcp->socket, b->buf, b->len, 0));
    if (bytes > 0)
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
    int r;
    if (tcp->send_handler == NULL || tcp->can_write) return ZN_OK;
    tcp->can_write = 1, r = znB_send(tcp), assert(r == ZN_OK), (void)r;
    return ZN_OK;
}

static int znB_retryrecv(zn_Tcp *tcp) {
    int r;
    if (tcp->recv_handler == NULL || tcp->can_read) return ZN_OK;
    tcp->can_read = 1, r = znB_recv(tcp), assert(r == ZN_OK), (void)r;
    return ZN_OK;
}

/* accept */

static void znB_closeaccept(zn_Accept *a) {
    /* completing: the queued result keeps its handler for znR_process */
    if (a->accept_handler != NULL && !a->can_read)
        zn_release(a->S), a->accept_handler = NULL;
    /* the payload rides in the queued result: drop it with the listener */
    if (a->accepted != NULL) zn_deltcp(a->accepted), a->accepted = NULL;
    if (znS_valid(a)) {
        znF_unregister(a->S, a->socket, &a->info);
        close(a->socket), a->socket = ZN_INVALID_SOCKET;
    }
}

static int znB_listen(zn_Accept *a, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return ZN_ESOCKET;
    /* accept() must never block the loop: readiness can go stale */
    znP_setnonblock(fd), znP_setreuseaddr(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    if (listen(fd, SOMAXCONN) != 0) return close(fd), ZN_ERROR;
    a->can_read = 1;
    a->accept_result.kind = ZN_TACCEPT;
    a->accept_result.owner.accept = a;
    if (!znF_regread(a->S, fd, &a->info, 1)) return close(fd), ZN_EPOLL;
    return (a->socket = fd), ZN_OK;
}

/* udp */

static int znB_initudp(zn_Udp *udp, zn_SockAddr *addr) {
    int fd = socket(znP_family(addr), SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return ZN_ESOCKET;
    znP_setnonblock(fd);
    if (bind(fd, &addr->addr, znP_size(addr)) != 0) return close(fd), ZN_EBIND;
    udp->can_read = 1;
    udp->recv_result.kind = ZN_TRECVFROM;
    udp->recv_result.owner.udp = udp;
    if (!znF_regread(udp->S, fd, &udp->info, 1)) return close(fd), ZN_EPOLL;
    return (udp->socket = fd), ZN_OK;
}

static void znB_closeudp(zn_Udp *udp) {
    if (!znS_valid(udp)) return; /* del already closed it */
    znF_unregister(udp->S, udp->socket, &udp->info);
    /* completing: the queued result keeps its handler for znR_process */
    if (udp->recv_handler != NULL && !udp->can_read)
        zn_release(udp->S), udp->recv_handler = NULL;
    close(udp->socket), udp->socket = ZN_INVALID_SOCKET;
}

#endif /* ZN_USE_BACKEND_REACTIVE */

#ifdef ZN_USE_BACKEND_EPOLL

ZN_API const char *zn_engine(void) { return "epoll"; }

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info, int et) {
    struct epoll_event event;
    event.events = et ? (EPOLLIN | EPOLLET) : EPOLLIN;
    event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    event.events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    event.data.ptr = info;
    return epoll_ctl(S->epoll, EPOLL_CTL_ADD, fd, &event) == 0;
}

static int znF_unregister(zn_State *S, int fd, zn_SocketInfo *info) {
    struct epoll_event event;
    unsigned           i;
    /* the batch may still hold a report for this registration */
    for (i = 0; i < S->nevents; ++i)
        if (S->events[i].data.ptr == info) S->events[i].data.ptr = NULL;
    memset(&event, 0, sizeof(event));
    return epoll_ctl(S->epoll, EPOLL_CTL_DEL, fd, &event) == 0;
}

static int znB_onevent(zn_Tcp *tcp, int eventmask) {
    int err = (eventmask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
    /* err names no direction: only the parked syscall reports it */
    if (err || (eventmask & EPOLLIN) != 0) znB_retryrecv(tcp);
    if (err || (eventmask & EPOLLOUT) != 0) znB_retrysend(tcp);
    return ZN_OK;
}

static int znB_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_SocketInfo *info = (zn_SocketInfo *)evt->data.ptr;
    int            eventmask = evt->events;
    int            err = (eventmask & (EPOLLERR | EPOLLHUP)) != 0;
    eventfd_t      value = 0;
    zn_Tcp        *tcp;
    if (info == NULL) return ZN_OK; /* invalidated in this batch */
    switch (info->type) {
    case ZN_ACCEPT: znB_onaccept((zn_Accept *)info->head, err); return ZN_OK;
    case ZN_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler != NULL) {
            if (err) return znB_onconnect(tcp, ZN_ERROR), ZN_OK;
            if ((eventmask & EPOLLOUT) != 0) znB_onconnect(tcp, ZN_OK);
        } else if (tcp->send_handler || tcp->recv_handler)
            znB_onevent(tcp, eventmask);
        return ZN_OK;
    case ZN_UDP: znB_onrecvfrom((zn_Udp *)info->head, err); return ZN_OK;
    default:
        assert(info->type == ZN_POST);
        eventfd_read(S->eventfd, &value);
        return znS_processposts(S), ZN_OK;
    }
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    int had_results, i, r, ms = -1;
    if (timeout < ZN_FOREVER)
        ms = timeout > (zn_Time)INT_MAX ? INT_MAX : (int)timeout;
    had_results = !znQ_empty(&S->result_queue);
    if (znR_process(S, ZN_MAX_RESULT_LOOPS)) return 1;
    /* only a blocking wait is skipped: CHECK (timeout 0) still polls */
    if (had_results && timeout != 0) return 0;
    r = epoll_wait(S->epoll, S->events, ZN_MAX_EVENTS, ms);
    if (r < 0) return 0; /* EINTR or error: nothing was reported */
    S->nevents = (unsigned)r;
    for (i = 0; i < r; ++i) znB_dispatch(S, &S->events[i]);
    S->nevents = 0;
    return znR_process(S, ZN_MAX_RESULT_LOOPS);
}

static int znB_init(zn_State *S) {
    if ((S->eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) < 0) return 0;
    S->epoll = epoll_create(ZN_MAX_EVENTS);
    S->post.type = ZN_POST;
    if (S->epoll < 0 || !znF_regread(S, S->eventfd, &S->post, 0)) {
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

#endif /* ZN_USE_BACKEND_EPOLL */

#ifdef ZN_USE_BACKEND_KQUEUE

ZN_API const char *zn_engine(void) { return "kqueue"; }

static int znF_regread(zn_State *S, int fd, zn_SocketInfo *info, int et) {
    struct kevent kev;
    int           v = EV_ADD | EV_ENABLE | (et ? EV_CLEAR : 0);
    EV_SET(&kev, fd, EVFILT_READ, v, 0, 0, info);
    return kevent(S->kqueue, &kev, 1, NULL, 0, NULL) == 0;
}

static int znF_regall(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev[2];
    int           v = EV_CLEAR | EV_ADD | EV_ENABLE;
    EV_SET(&kev[0], fd, EVFILT_READ, v, 0, 0, info);
    EV_SET(&kev[1], fd, EVFILT_WRITE, v, 0, 0, info);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znF_unregister(zn_State *S, int fd, zn_SocketInfo *info) {
    struct kevent kev[2];
    unsigned      i;
    /* the batch may still hold a report for this registration */
    for (i = 0; i < S->nevents; ++i)
        if (S->events[i].udata == info) S->events[i].udata = NULL;
    EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(S->kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znB_onevent(zn_Tcp *tcp, int filter, int flags) {
    int err = (flags & (EV_EOF | EV_ERROR)) != 0;
    /* eof/error comes on whichever filter changed: only the syscall tells */
    if (err || filter == EVFILT_READ) znB_retryrecv(tcp);
    if (err || filter == EVFILT_WRITE) znB_retrysend(tcp);
    return ZN_OK;
}

static int znB_dispatch(zn_State *S, struct kevent *evt) {
    zn_SocketInfo *info = (zn_SocketInfo *)evt->udata;
    char           buff[ZN_PAGESIZE];
    zn_Tcp        *tcp;
    int            err = (evt->flags & (EV_ERROR | EV_EOF)) != 0;
    if (info == NULL) return ZN_OK; /* invalidated in this batch */
    switch (info->type) {
    case ZN_ACCEPT: znB_onaccept((zn_Accept *)info->head, err); return ZN_OK;
    case ZN_TCP:
        tcp = (zn_Tcp *)info->head;
        if (tcp->connect_handler != NULL) {
            if (err) return znB_onconnect(tcp, ZN_ERROR), ZN_OK;
            if (evt->filter == EVFILT_WRITE) znB_onconnect(tcp, ZN_OK);
        } else if (tcp->send_handler || tcp->recv_handler)
            znB_onevent(tcp, evt->filter, evt->flags);
        return ZN_OK;
    case ZN_UDP: znB_onrecvfrom((zn_Udp *)info->head, err); return ZN_OK;
    default:
        assert(info->type == ZN_POST);
        while (recv(S->sockpairs[1], buff, ZN_PAGESIZE, 0) > 0) continue;
        return znS_processposts(S), ZN_OK;
    }
}

static int znB_poll(zn_State *S, zn_Time timeout) {
    struct timespec ms, *pms = NULL;
    int             had_results, i, r;
    if (timeout < ZN_FOREVER) {
        ms.tv_sec = timeout / 1000;
        ms.tv_nsec = (timeout % 1000) * 1000000;
        pms = &ms;
    }
    had_results = !znQ_empty(&S->result_queue);
    if (znR_process(S, ZN_MAX_RESULT_LOOPS)) return 1;
    /* only a blocking wait is skipped: CHECK (timeout 0) still polls */
    if (had_results && timeout != 0) return 0;
    r = kevent(S->kqueue, NULL, 0, S->events, ZN_MAX_EVENTS, pms);
    if (r < 0) return 0; /* EINTR or error: nothing was reported */
    S->nevents = (unsigned)r;
    for (i = 0; i < r; ++i) znB_dispatch(S, &S->events[i]);
    S->nevents = 0;
    return znR_process(S, ZN_MAX_RESULT_LOOPS);
}

static int znB_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0) return 0;
    S->kqueue = kqueue();
    S->post.type = ZN_POST;
    if (S->kqueue < 0 || !znF_regread(S, S->sockpairs[1], &S->post, 0)) {
        if (S->kqueue >= 0) close(S->kqueue);
        return close(S->sockpairs[0]), close(S->sockpairs[1]), 0;
    }
    znS_initposts(S), znR_init(S);
    znP_setnonblock(S->sockpairs[0]), znP_setnonblock(S->sockpairs[1]);
    return 1;
}

static void znB_close(zn_State *S) {
    znS_processposts(S);
    znR_process(S, INT_MAX);
    assert(znQ_first(&S->result_queue) == NULL);
    close(S->sockpairs[0]), close(S->sockpairs[1]);
    close(S->kqueue);
}

#endif /* ZN_USE_BACKEND_KQUEUE */

/* api */

/* clang-format off */
ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)
{ *info = tcp->peer_info; }

ZN_API int zn_lasterror(zn_State *S)
{ return S ? S->last_error : ZN_EPARAM; }
/* clang-format on */

ZN_API unsigned zn_retain(zn_State *S) { return ++S->waitings; }
ZN_API unsigned zn_release(zn_State *S) { return --S->waitings; }
ZN_API void    *zn_getuserdata(zn_State *S) { return S->userdata; }
ZN_API void     zn_setuserdata(zn_State *S, void *ud) { S->userdata = ud; }

static int znS_poll(zn_State *S, zn_Time budget) {
    zn_Time current, timeout;
    int     has_more = 0;
    S->status = ZN_STATUS_IN_RUN;
    znT_update(&S->ts, current = zn_time());
    if (S->status != ZN_STATUS_CLOSING_IN_RUN) {
        timeout = znT_gettimeout(&S->ts, current);
        if (budget < timeout) timeout = budget;
        has_more = znB_poll(S, timeout);
        znT_update(&S->ts, zn_time());
    }
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
    if (status == ZN_STATUS_CLOSING) return; /* already tearing down */
    if (status == ZN_STATUS_IN_RUN || status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_CLOSING_IN_RUN;
        return;
    }
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

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *h, void *ud) {
    zn_Timer *timer;
    if (S->status > ZN_STATUS_READY) return NULL;
    if ((timer = (zn_Timer *)znM_get(&S->ts.timers)) == NULL) return NULL;
    timer->u.ud = ud, timer->handler = h;
    timer->ts = &S->ts, timer->index = ZN_TIMER_NOINDEX;
    return timer;
}

static zn_Tcp *znS_newtcpfd(zn_State *S, zn_Socket fd, zn_SockAddr *raddr) {
    zn_Tcp *tcp;
    int     err; /* the accept completion reports the failure as NULL */
    tcp = (zn_Tcp *)znS_newslot(S, &S->tcps, &err);
    if (tcp == NULL) return znP_closefd(fd), (zn_Tcp *)0;
    znL_insert(&S->active_tcps, tcp);
    tcp->S = S, tcp->socket = fd;
    znB_inittcp(tcp), znS_setinfo(tcp, ZN_TCP);
    if (znB_adoptfd(tcp) != ZN_OK)
        return znB_closetcp(tcp), znS_putobject(tcp), (zn_Tcp *)0;
    return znP_setinfo(raddr, &tcp->peer_info), tcp;
}

ZN_API zn_Accept *zn_listen(zn_State *S, const char *addr, unsigned port) {
    zn_SockAddr laddr;
    zn_Accept  *a;
    int         r;
    if (S == NULL) return NULL; /* nowhere to record the reason */
    if (addr == NULL || !znP_parseaddr(&laddr, addr, port))
        return (S->last_error = ZN_EPARAM), (zn_Accept *)0;
    a = (zn_Accept *)znS_newslot(S, &S->accepts, &r);
    if (a == NULL) return (S->last_error = r), (zn_Accept *)0;
    a->S = S, a->socket = ZN_INVALID_SOCKET;
    znL_insert(&S->active_accepts, a);
    znS_setinfo(a, ZN_ACCEPT), znB_initaccept(a);
    if ((r = znB_listen(a, &laddr)) != ZN_OK) {
        znB_closeaccept(a), znS_putaccept(a);
        return (S->last_error = r), (zn_Accept *)0;
    }
    return (S->last_error = ZN_OK), a;
}

ZN_API void zn_delaccept(zn_Accept *a) {
    if (a == NULL || !znS_valid(a)) return;
    znB_closeaccept(a);
    if (a->accept_handler == NULL) znS_putaccept(a);
}

ZN_API int zn_accept(zn_Accept *a, zn_AcceptHandler *h, void *ud) {
    int r;
    if (a == NULL || h == NULL) return ZN_EPARAM;
    if (!znS_valid(a)) return ZN_ESTATE;
    if (a->accept_handler != NULL) return ZN_EBUSY;
    if ((r = znB_accept(a)) != ZN_OK) return r;
    a->accept_handler = h, a->accept_ud = ud;
    return zn_retain(a->S), ZN_OK;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    if (tcp == NULL || tcp->deleted) return;
    tcp->deleted = 1; /* ownership gone: later completions only clean up */
    znB_closetcp(tcp), znS_endtcp(tcp);
}

ZN_API zn_Tcp *zn_connect(zn_State *S, const char *addr, unsigned port,
                          zn_ConnectHandler *h, void *ud) {
    zn_SockAddr raddr;
    zn_Tcp     *tcp;
    int         r;
    if (S == NULL) return NULL; /* nowhere to record the reason */
    if (addr == NULL || h == NULL || !znP_parseaddr(&raddr, addr, port))
        return (S->last_error = ZN_EPARAM), (zn_Tcp *)0;
    tcp = (zn_Tcp *)znS_newslot(S, &S->tcps, &r);
    if (tcp == NULL) return (S->last_error = r), (zn_Tcp *)0;
    tcp->S = S, tcp->socket = ZN_INVALID_SOCKET;
    znL_insert(&S->active_tcps, tcp);
    znS_setinfo(tcp, ZN_TCP), znB_inittcp(tcp);
    if ((r = znB_connect(tcp, &raddr)) != ZN_OK) {
        znB_closetcp(tcp), znS_putobject(tcp);
        return (S->last_error = r), (zn_Tcp *)0;
    }
    tcp->connect_handler = h, tcp->connect_ud = ud;
    znP_setinfo(&raddr, &tcp->peer_info);
    return zn_retain(S), (S->last_error = ZN_OK), tcp;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len,
                   zn_SendHandler *h, void *ud) {
    int r;
    if (!tcp || !buff || !len || !h) return ZN_EPARAM;
    if (!znS_valid(tcp) || tcp->connect_handler != NULL) return ZN_ESTATE;
    if (tcp->send_handler != NULL) return ZN_EBUSY;
    tcp->send_buffer.buf = (char *)buff;
    tcp->send_buffer.len = len;
    if ((r = znB_send(tcp)) != ZN_OK)
        return memset(&tcp->send_buffer, 0, sizeof(tcp->send_buffer)), r;
    tcp->send_handler = h, tcp->send_ud = ud;
    return zn_retain(tcp->S), ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *h,
                   void *ud) {
    int r;
    if (!tcp || !buff || !len || !h) return ZN_EPARAM;
    if (!znS_valid(tcp) || tcp->connect_handler != NULL) return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    tcp->recv_buffer.buf = buff;
    tcp->recv_buffer.len = len;
    if ((r = znB_recv(tcp)) != ZN_OK)
        return memset(&tcp->recv_buffer, 0, sizeof(tcp->recv_buffer)), r;
    tcp->recv_handler = h, tcp->recv_ud = ud;
    return zn_retain(tcp->S), ZN_OK;
}

ZN_API zn_Udp *zn_bind(zn_State *S, const char *addr, unsigned port) {
    zn_SockAddr laddr;
    zn_Udp     *udp;
    int         r;
    if (S == NULL) return NULL; /* nowhere to record the reason */
    if (addr == NULL || !znP_parseaddr(&laddr, addr, port))
        return (S->last_error = ZN_EPARAM), (zn_Udp *)0;
    udp = (zn_Udp *)znS_newslot(S, &S->udps, &r);
    if (udp == NULL) return (S->last_error = r), (zn_Udp *)0;
    udp->S = S, udp->socket = ZN_INVALID_SOCKET;
    znL_insert(&S->active_udps, udp);
    znS_setinfo(udp, ZN_UDP);
    if ((r = znB_initudp(udp, &laddr)) != ZN_OK) {
        znB_closeudp(udp), znS_putobject(udp);
        return (S->last_error = r), (zn_Udp *)0;
    }
    return (S->last_error = ZN_OK), udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    if (udp == NULL || !znS_valid(udp)) return;
    znB_closeudp(udp);
    if (udp->recv_handler == NULL) znS_putobject(udp);
}

ZN_API int zn_connectudp(zn_Udp *udp, const char *addr, unsigned port) {
    zn_SockAddr raddr;
    if (udp == NULL || addr == NULL) return ZN_EPARAM;
    if (!znS_valid(udp)) return ZN_ESTATE;
    if (!znP_parseaddr(&raddr, addr, port)) return ZN_EPARAM;
    return znB_connectudp(udp, &raddr);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len,
                     const char *addr, unsigned port) {
    zn_SockAddr raddr;
    if (!udp || !buff || !len || len > ZN_MAX_UDPSEND) return ZN_EPARAM;
    if (!znS_valid(udp)) return ZN_ESTATE;
    if (addr == NULL) {
        if (port != 0) return ZN_EPARAM;
        return znB_sendto(udp, buff, len, NULL);
    }
    if (!znP_parseaddr(&raddr, addr, port)) return ZN_EPARAM;
    return znB_sendto(udp, buff, len, &raddr);
}

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len,
                       zn_RecvFromHandler *h, void *ud) {
    int r;
    if (!udp || !buff || !len || !h) return ZN_EPARAM;
    if (!znS_valid(udp)) return ZN_ESTATE;
    if (udp->recv_handler) return ZN_EBUSY;
    udp->recv_buffer.buf = buff, udp->recv_buffer.len = len;
    if ((r = znB_recvfrom(udp)) != ZN_OK)
        return memset(&udp->recv_buffer, 0, sizeof(udp->recv_buffer)), r;
    udp->recv_handler = h, udp->recv_ud = ud;
    return zn_retain(udp->S), ZN_OK;
}

ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* unixcc: flags+='-Wall -Wextra -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * win32cc: flags+='-Wall -Wextra -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll'
 * linuxcc: libs+='-pthread -lrt' output='znet.so'
 * maccc: libs+='-pthread' output='znet.so' */
