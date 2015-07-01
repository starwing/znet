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

#define ZN_MAX_ADDRLEN   512


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

typedef struct zn_PeerInfo {
    char     addr[ZN_MAX_ADDRLEN];
    unsigned port;
} zn_PeerInfo;

typedef void zn_PostHandler     (void *ud, zn_State *S);
typedef void zn_TimerHandler    (void *ud, zn_Timer *timer, unsigned delayed);
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

ZN_API zn_State *zn_newstate (void);
ZN_API zn_State *zn_clone    (zn_State *S);
ZN_API void      zn_close    (zn_State *S);

ZN_API int zn_run  (zn_State *S, int mode);
ZN_API int zn_post (zn_State *S, zn_PostHandler *cb, void *ud);


/* znet timer routines */

ZN_API unsigned zn_time (void);

ZN_API zn_Timer *zn_newtimer (zn_State *S, zn_TimerHandler *cb, void *ud);
ZN_API void      zn_deltimer (zn_Timer *timer);

ZN_API void zn_starttimer  (zn_Timer *timer, unsigned delayms);
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
    type *tmp_ = (h);                       \
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
    (pn) = (h)->first;                      \
    if ((pn) != NULL)                       \
        (h)->first = (h)->first->next;    } while (0)

#endif /* zn_list_h */


/* pre-defined platform-independency routines */

#define ZN_STATE_FIELDS                     \
    zn_Accept *accepts;                     \
    zn_Tcp    *tcps;                        \
    zn_Udp    *udps;                        \
    zn_Timer  *timers;                      \
    zn_Timer  *active_timers;               \
    zn_Status  status;                      \
    unsigned   nexttime;                    \

# define ZN_GETOBJECT(S, type, name)        \
                         type* name;   do { \
    if (S->status > ZN_STATUS_READY)        \
        return NULL;                        \
    name = (type*)malloc(sizeof(type));     \
    if (name == NULL) return NULL;          \
    memset(name, 0, sizeof(type));          \
    name->S = S;                            \
    znL_insert(&S->name##s, name);        } while (0)

# define ZN_PUTOBJECT(name)            do { \
    znL_remove(name);                       \
    free(name);                           } while (0)

typedef enum zn_Status {
    ZN_STATUS_IN_RUN  = -1,       /* now in zn_run() */
    ZN_STATUS_READY   =  0,       /* not close */
    ZN_STATUS_CLOSING =  1,       /* prepare close */
    ZN_STATUS_CLOSING_IN_RUN = 2, /* prepare close in run() */
} zn_Status;

/* routines implemented in this header and can be used
 * in platform-specified headers */
static void znT_updatetimer (zn_State *S, unsigned current);
static void znT_cleartimers (zn_State *S);
static unsigned znT_gettimeout (zn_State *S, unsigned current);

/* static functions should be implement
 * in platform-specified headers */
static int  znS_init  (zn_State *S);
static void znS_close (zn_State *S);
static int  znS_clone (zn_State *NS, zn_State *S);
static int  znS_poll  (zn_State *S, int checkonly);


/* system specified routines */
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
    S->nexttime = ~(unsigned)0;
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
    znL_apply(zn_Accept, S->accepts, zn_delaccept);
    znL_apply(zn_Tcp,    S->tcps,    zn_deltcp);
    znL_apply(zn_Udp,    S->udps,    zn_deludp);
    znS_close(S);
    free(S);
}

ZN_API zn_State *zn_clone(zn_State *S) {
    zn_State *NS = (zn_State*)malloc(sizeof(zn_State));
    if (NS == NULL) return NULL;
    memset(NS, 0, sizeof(zn_State));
    NS->nexttime = ~(unsigned)0;
    if (!znS_clone(NS, S)) {
        free(NS);
        return NULL;
    }
    return NS;
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

struct zn_Timer {
    znL_entry(zn_Timer);
    zn_State *S;
    void *ud;
    zn_TimerHandler *handler;
    unsigned starttime;
    unsigned time;
};

static void znT_updatenexttime(zn_State *S)
{ S->nexttime = S->active_timers ? S->active_timers->time : ~(unsigned)0; }

static unsigned znT_gettimeout(zn_State *S, unsigned current)
{ return S->nexttime < current ? 0 : S->nexttime - current; }

static void znT_inserttimer(zn_Timer *t) {
    zn_Timer **head = &t->S->active_timers;
    while (*head != NULL && (*head)->time <= t->time)
        head = &(*head)->next;
    /* detach timer and insert into active linked list */
    znL_remove(t);
    znL_insert(head, t);
}

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *cb, void *ud) {
    ZN_GETOBJECT(S, zn_Timer, timer);
    timer->ud = ud;
    timer->handler = cb;
    return timer;
}

ZN_API void zn_deltimer(zn_Timer *timer) {
    ZN_PUTOBJECT(timer);
    znT_updatenexttime(timer->S);
}

ZN_API void zn_starttimer(zn_Timer *timer, unsigned delayms) {
    timer->starttime = zn_time();
    timer->time = timer->starttime + delayms;
    znL_remove(timer);
    znT_inserttimer(timer);
    timer->S->nexttime = timer->S->active_timers->time;
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    znL_remove(timer);
    znL_insert(&timer->S->timers, timer);
    timer->time = ~(unsigned)0;
    znT_updatenexttime(timer->S);
}

static void znT_cleartimers(zn_State *S) {
    znL_apply(zn_Timer, S->timers, free);
    znL_apply(zn_Timer, S->active_timers, free);
    S->nexttime = ~(unsigned)0;
}

static void znT_updatetimer(zn_State *S, unsigned current) {
    zn_Timer *nextticks = NULL;
    if (S->nexttime > current) return;
    while (S->active_timers && S->active_timers->time <= current) {
        zn_Timer *cur = S->active_timers;
        znL_remove(cur);
        znL_init(cur);
        if (cur->handler) {
            unsigned elapsed = current - cur->starttime;
            cur->starttime = 0;
            cur->time = ~(unsigned)0;
            cur->handler(cur->ud, cur, elapsed);
        }
        if (!cur->pprev)
            znL_insert(&S->timers, cur);
        else if (cur->time <= current) { /* avoid forever loop */
            znL_remove(cur);
            znL_insert(&nextticks, cur);
        }
    }
    znL_apply(zn_Timer, nextticks, znT_inserttimer);
    znT_updatenexttime(S);
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-lpthread' output='znet.so' */
