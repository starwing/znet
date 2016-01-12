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

#ifndef ZN_MAX_EVENTS
# define ZN_MAX_EVENTS   1024
#endif

#define ZN_MAX_ADDRLEN   46
#define ZN_MAX_TIMERPOOL 512
#define ZN_MAX_TIMERHEAP 512

#define ZN_TIMER_NOINDEX (~(unsigned)0)
#define ZN_FOREVER       (~(zn_Time)0)
#define ZN_MAX_SIZET     ((~(size_t)0)-100)


ZN_NS_BEGIN

#define ZN_ERRORS(X)                           \
    X(OK,       "No error")                    \
    X(ERROR,    "Operation failed")            \
    X(ECLOSED,  "Remote socket closed")        \
    X(EHANGUP,  "Remote socket hang up")       \
    X(ESOCKET,  "Socket creation error")       \
    X(ECONNECT, "Connect error")               \
    X(EBIND,    "Local address bind error")    \
    X(EPARAM,   "Parameter error")             \
    X(EPOLL,    "Register to poll error")      \
    X(ESTATE,   "State error")                 \
    X(EBUSY,    "Another operation performed") \

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

#ifdef ZN_USE_64BIT_TIMER
typedef unsigned long long zn_Time;
#else
typedef unsigned zn_Time;
#endif

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

#define ZN_RUN_ONCE  0 
#define ZN_RUN_CHECK 1
#define ZN_RUN_LOOP  2

ZN_API void zn_initialize   (void);
ZN_API void zn_deinitialize (void);

ZN_API const char *zn_strerror (int err);
ZN_API const char *zn_engine (void);

ZN_API zn_State *zn_newstate (void);
ZN_API void      zn_close    (zn_State *S);

ZN_API int zn_run  (zn_State *S, int mode);
ZN_API int zn_post (zn_State *S, zn_PostHandler *cb, void *ud);


/* znet timer routines */

ZN_API zn_Time zn_time (void);

ZN_API zn_Timer *zn_newtimer (zn_State *S, zn_TimerHandler *cb, void *ud);
ZN_API void      zn_deltimer (zn_Timer *timer);

ZN_API int  zn_starttimer  (zn_Timer *timer, zn_Time delayms);
ZN_API void zn_canceltimer (zn_Timer *timer);


/* znet accept routines */

ZN_API zn_Accept* zn_newaccept   (zn_State *S);
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
                       zn_ConnectHandler *cb, void *ud);

ZN_API int zn_send (zn_Tcp *tcp, const char *buff, unsigned len,
                    zn_SendHandler *cb, void *ud);
ZN_API int zn_recv (zn_Tcp *tcp,       char *buff, unsigned len,
                    zn_RecvHandler *cb, void *ud);


/* znet udp socket routines */

ZN_API zn_Udp* zn_newudp (zn_State *S, const char *addr, unsigned port);
ZN_API void    zn_deludp (zn_Udp *udp);

ZN_API int zn_sendto   (zn_Udp *udp, const char *buff, unsigned len,
                        const char *addr, unsigned port);
ZN_API int zn_recvfrom (zn_Udp *udp,       char *buff, unsigned len,
                        zn_RecvFromHandler *cb, void *ud);

ZN_NS_END


#endif /* znet_h */


#if defined(ZN_IMPLEMENTATION) && !defined(znet_implemented)
#define znet_implemented


ZN_NS_BEGIN


#include <assert.h>
#include <stdlib.h>
#include <string.h>


/* linked list routines */

#ifndef zn_list_h
#define zn_list_h

#define znL_entry(T) T *next; T **pprev
#define znL_head(T)  struct T##_hlist { znL_entry(T); }

#define znL_init(n)                    do { \
    (n)->pprev = &(n)->next;                \
    (n)->next = NULL;                     } while (0)

#define znL_insert(h, n)               do { \
    (n)->pprev = (h);                       \
    (n)->next = *(h);                       \
    if (*(h) != NULL)                       \
        (*(h))->pprev = &(n)->next;         \
    *(h) = (n);                           } while (0)

#define znL_remove(n)                  do { \
    if ((n)->next != NULL)                  \
        (n)->next->pprev = (n)->pprev;      \
    *(n)->pprev = (n)->next;              } while (0)

#define znL_apply(type, h, func)       do { \
    type *tmp_ = (type*)(h);                \
    (h) = NULL;                             \
    while (tmp_) {                          \
        type *next_ = tmp_->next;           \
        func(tmp_);                         \
        tmp_ = next_;                       \
    }                                     } while (0)

#define znQ_entry(T) T* next
#define znQ_type(T)  struct T##_queue { T *first; T **plast; }

#define znQ_init(h)                    do { \
    (h)->first = NULL;                      \
    (h)->plast = &(h)->first;             } while (0)

#define znQ_enqueue(h, n)              do { \
    *(h)->plast = (n);                      \
    (h)->plast = &(n)->next;                \
    (n)->next = NULL;                     } while (0)

#define znQ_dequeue(h, pn)             do { \
    if (((pn) = (h)->first) != NULL)        \
        (h)->first = (h)->first->next;      \
    if ((h)->plast == &(pn)->next)          \
        (h)->plast = &(h)->first;           } while (0)

#define znQ_apply(type, h, func)       do { \
    type *tmp_ = (h)->first;                \
    znQ_init(h);                            \
    while (tmp_) {                          \
        type *next_ = tmp_->next;           \
        func(tmp_);                         \
        tmp_ = next_;                       \
    }                                     } while (0)

#endif /* zn_list_h */


/* pre-defined platform-independency routines */

#define ZN_OBJECT_TYPES(X)                  \
    X(post,   zn_Post)                      \
    X(accept, zn_Accept)                    \
    X(tcp,    zn_Tcp)                       \
    X(udp,    zn_Udp)                       \

#define ZN_STATE_FIELDS                     \
    void *objects[ZN_MAX_TYPES];            \
    void *cached[ZN_MAX_TYPES];             \
    zn_Timers  timers;                      \
    zn_Status  status;                      \
    unsigned   waitings;                    \

# define ZN_GETOBJECT(S, type, name)        \
                         type* name;   do { \
    if (S->status > ZN_STATUS_READY)        \
        return 0;                           \
    name = S->cached[ZN_T##name];           \
    if (name)                               \
        S->cached[ZN_T##name] = name->next; \
    else {                                  \
        name = (type*)malloc(sizeof(type)); \
        if (name == NULL) return 0;         \
    }                                       \
    memset(name, 0, sizeof(type));          \
    name->S = S;                            \
    znL_insert((type**)&S->objects[ZN_T##name], name); } while (0)

# define ZN_PUTOBJECT(name)            do { \
    zn_State *S = name->S;                  \
    znL_remove(name);                       \
    if (S->status > ZN_STATUS_READY)        \
        free(name);                         \
    else {                                  \
        name->next = S->cached[ZN_T##name]; \
        S->cached[ZN_T##name] = name;       \
    }                                     } while (0)

typedef enum zn_Status {
    ZN_STATUS_IN_RUN  = -1,       /* now in zn_run() */
    ZN_STATUS_READY   =  0,       /* not close */
    ZN_STATUS_CLOSING =  1,       /* prepare close */
    ZN_STATUS_CLOSING_IN_RUN = 2, /* prepare close in run() */
} zn_Status;

typedef enum zn_Types {
#define X(name, type) ZN_T##name,
    ZN_OBJECT_TYPES(X)
#undef  X
    ZN_MAX_TYPES
} zn_Types;

struct zn_Timer {
    union { zn_Timer *next; void *ud; } u;
    zn_TimerHandler *handler;
    zn_State *S;
    unsigned index;
    zn_Time starttime;
    zn_Time emittime;
};

typedef struct zn_TimerPool {
    struct zn_TimerPool *next;
    zn_Timer timers[ZN_MAX_TIMERPOOL];
} zn_TimerPool;

typedef struct zn_Timers {
    zn_TimerPool *pool;
    zn_Timer *freed;
    zn_Timer **heap;
    zn_Time nexttime;
    unsigned pool_free;
    unsigned heap_used;
    unsigned heap_size;
} zn_Timers;

/* routines implemented in this header and can be used
 * in platform-specified headers */
static void    znT_cleartimers  (zn_State *S);
static void    znT_updatetimers (zn_State *S, zn_Time current);
static int     znT_hastimers    (zn_State *S);
static zn_Time znT_gettimeout   (zn_State *S, zn_Time current);

/* static functions should be implement
 * in platform-specified headers */
static int  znS_init  (zn_State *S);
static void znS_close (zn_State *S);
static int  znS_poll  (zn_State *S, int checkonly);


/* system specified implementations */
#if defined(_WIN32)
# define ZN_USE_IOCP
# include "znet_iocp.h"
#elif defined(__linux__) && !defined(ZN_USE_SELECT)
# define ZN_USE_EPOLL
# include "znet_epoll.h"
#else
# undef  ZN_USE_SELECT
# define ZN_USE_SELECT
# include "znet_select.h"
#endif


/* global routines */

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
    S->timers.nexttime = ZN_FOREVER;
    if (!znS_init(S)) {
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
    znT_cleartimers(S);
    znL_apply(zn_Accept, S->objects[ZN_Taccept], zn_delaccept);
    znL_apply(zn_Tcp,    S->objects[ZN_Ttcp],    zn_deltcp);
    znL_apply(zn_Udp,    S->objects[ZN_Tudp],    zn_deludp);
    /* 2. delete all remaining objects */
    znL_apply(zn_Accept, S->cached[ZN_Taccept], free);
    znL_apply(zn_Tcp,    S->cached[ZN_Ttcp],    free);
    znL_apply(zn_Udp,    S->cached[ZN_Tudp],    free);
    znS_close(S);
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


/* timer routines */

static int znT_hastimers(zn_State *S)
{ return S->timers.heap_used != 0; }

static int znT_resizeheap(zn_Timers *S, size_t size) {
    zn_Timer **heap;
    size_t realsize = ZN_MAX_TIMERHEAP;
    while (realsize < size && realsize < ZN_MAX_SIZET/sizeof(zn_Timer*)/2)
        realsize <<= 1;
    if (realsize < size) return 0;
    heap = (zn_Timer**)realloc(S->heap, realsize*sizeof(zn_Timer*));
    if (heap == NULL) return 0;
    S->heap = heap;
    S->heap_size = realsize;
    return 1;
}

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *cb, void *ud) {
    zn_Timers *ts = &S->timers;
    zn_Timer *timer = ts->freed;
    if (timer != NULL)
        ts->freed = timer->u.next;
    else {
        if (ts->pool_free == 0) {
            zn_TimerPool *pool = (zn_TimerPool*)malloc(sizeof(zn_TimerPool));
            if (pool == NULL) return NULL;
            pool->next = ts->pool;
            ts->pool = pool;
            ts->pool_free = ZN_MAX_TIMERPOOL;
        }
        timer = &ts->pool->timers[ZN_MAX_TIMERPOOL - ts->pool_free--];
    }
    timer->u.ud = ud;
    timer->handler = cb;
    timer->S = S;
    timer->index = ZN_TIMER_NOINDEX;
    return timer;
}

ZN_API void zn_deltimer(zn_Timer *timer) {
    zn_canceltimer(timer);
    timer->handler = NULL;
    timer->u.next = timer->S->timers.freed;
    timer->S->timers.freed = timer;
}

ZN_API int zn_starttimer(zn_Timer *timer, zn_Time delayms) {
    unsigned index;
    zn_Timers *ts = &timer->S->timers;
    if (timer->index != ZN_TIMER_NOINDEX)
        zn_canceltimer(timer);
    if (ts->heap_size == ts->heap_used
            && !znT_resizeheap(ts, ts->heap_size * 2))
        return 0;
    index = ts->heap_used++;
    timer->starttime = zn_time();
    timer->emittime = timer->starttime + delayms;
    while (index) {
        unsigned parent = (index-1)>>1;
        if (ts->heap[parent]->emittime <= timer->emittime)
            break;
        ts->heap[index] = ts->heap[parent];
        ts->heap[index]->index = index;
        index = parent;
    }
    ts->heap[index] = timer;
    timer->index = index;
    if (index == 0) ts->nexttime = timer->emittime;
    return 1;
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    zn_Timers *ts = &timer->S->timers;
    unsigned index = timer->index;
    if (index == ZN_TIMER_NOINDEX) return;
    timer->index = ZN_TIMER_NOINDEX;
    if (ts->heap_used == 0 || timer == ts->heap[--ts->heap_used])
        return;
    timer = ts->heap[ts->heap_used];
    while (1) {
        unsigned left = (index<<1)|1, right = (index+1)<<1;
        unsigned newindex = right;
        if (left >= ts->heap_used) break;
        if (timer->emittime >= ts->heap[left]->emittime) {
            if (right >= ts->heap_used
                    || ts->heap[left]->emittime < ts->heap[right]->emittime)
                newindex = left;
        }
        else if (right >= ts->heap_used
                || timer->emittime <= ts->heap[right]->emittime)
            break;
        ts->heap[index] = ts->heap[newindex];
        ts->heap[index]->index = index;
        index = newindex;
    }
    ts->heap[index] = timer;
    timer->index = index;
}

static void znT_cleartimers(zn_State *S) {
    zn_Timers *ts = &S->timers;
    while (ts->pool != NULL) {
        zn_TimerPool *next = ts->pool->next;
        free(ts->pool);
        ts->pool = next;
    }
    free(ts->heap);
    memset(ts, 0, sizeof(zn_Timers));
    ts->nexttime = ZN_FOREVER;
}

static void znT_updatetimers(zn_State *S, zn_Time current) {
    zn_Timers *ts = &S->timers;
    if (ts->nexttime > current) return;
    while (ts->heap_used && ts->heap[0]->emittime <= current) {
        zn_Timer *timer = ts->heap[0];
        zn_canceltimer(timer);
        if (timer->handler) {
            int ret = timer->handler(timer->u.ud,
                    timer, current - timer->starttime);
            if (ret > 0) zn_starttimer(timer, ret);
        }
    }
    ts->nexttime = ts->heap_used == 0 ? ZN_FOREVER : ts->heap[0]->emittime;
}

static zn_Time znT_gettimeout(zn_State *S, zn_Time current) {
    zn_Time emittime = S->timers.nexttime;
    if (emittime < current) return 0;
    return emittime - current;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-pthread -lrt' output='znet.so' */
