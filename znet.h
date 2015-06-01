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


/* implementations */
#ifdef ZN_IMPLEMENTATION

ZN_NS_BEGIN


#include <assert.h>
#include <stdlib.h>
#include <string.h>


/* half list routines */

#define znL_cond(c,e) ((void)((c) && (e)))

#define znL_type(T) struct T##_hlist { T *next; T **pprev; }

#define znL_init(n) \
    ((void)((n)->pprev = &(n)->next, (n)->next = NULL))

#define znL_insert(h, n)                          ((void)( \
    (n)->pprev = (h), (n)->next = *(h),                    \
    znL_cond(*(h), (*(h))->pprev = &(n)->next),            \
    *(h) = (n)                                           ))

#define znL_remove(n)                             ((void)( \
    znL_cond((n)->next, (n)->next->pprev = (n)->pprev),    \
    *(n)->pprev = (n)->next                              ))


/* global routines */

static void znS_clear(zn_State *S);
static int  znS_poll(zn_State *S, int checkonly);

typedef enum zn_CloseSource {
    ZN_CLOSE_IN_RUN  = -1,        /* now in zn_run() */
    ZN_CLOSE_NORMAL  =  0,        /* not close */
    ZN_CLOSE_PREPARE =  1,        /* prepare close */
    ZN_CLOSE_PREPARE_IN_RUN = 2,  /* prepare close in run() */
} zm_CloseSource;

struct zn_State {
    int closing;
    zn_Accept *accepts;
    zn_Tcp *tcps;
    zn_Udp *udps;
    zn_Timer *timers;
    zn_Timer *unused_timers;
};

static zn_State *znS_init(zn_State *S) {
    memset(S, 0, sizeof(*S));
    return S;
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
    zn_Timer *next;
    zn_Timer **pprev;
    zn_State *S;
    void *ud;
    zn_TimerHandler *handler;
    unsigned starttime;
    unsigned time;
};

static void znT_inserttimer(zn_Timer *t) {
    zn_Timer **head = &t->S->timers;
    while (*head != NULL && (*head)->time <= t->time)
        head = &(*head)->next;
    /* detach timer and insert into active linked list */
    znL_remove(t);
    znL_insert(head, t);
}

static void znT_cleartimers(zn_State *S) {
    while (S->unused_timers) {
        zn_Timer *next = S->unused_timers->next;
        free(S->unused_timers);
        S->unused_timers = next;
    }
    while (S->timers) {
        zn_Timer *next = S->timers->next;
        free(S->timers);
        S->timers = next;
    }
}

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *cb, void *ud) {
    zn_Timer *t;
    if (S->closing > ZN_CLOSE_NORMAL
            || (t = (zn_Timer*)malloc(sizeof(zn_Timer))) == NULL)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->S = S;
    t->ud = ud;
    t->handler = cb;
    znL_insert(&S->unused_timers, t);
    return t;
}

ZN_API void zn_deltimer(zn_Timer *timer) {
    znL_remove(timer);
    free(timer);
}

ZN_API void zn_starttimer(zn_Timer *timer, unsigned delayms) {
    timer->starttime = zn_time();
    timer->time = timer->starttime + delayms;
    znL_remove(timer);
    znT_inserttimer(timer);
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    znL_remove(timer);
    znL_insert(&timer->S->unused_timers, timer);
    timer->time = ~(unsigned)0;
}

static void znT_updatetimer(zn_State *S, unsigned current) {
    zn_Timer *nextticks = NULL;
    while (S->timers && S->timers->time <= current) {
        zn_Timer *cur = S->timers;
        znL_remove(cur);
        znL_init(cur);
        if (cur->handler) {
            unsigned elapsed = current - cur->starttime;
            cur->starttime = 0;
            cur->time = ~(unsigned)0;
            cur->handler(cur->ud, cur, elapsed);
        }
        if (!cur->pprev)
            znL_insert(&S->unused_timers, cur);
        else if (cur->time <= current) { /* avoid forever loop */
            znL_remove(cur);
            znL_insert(&nextticks, cur);
        }
    }
    while (nextticks != NULL) {
        zn_Timer *next = nextticks->next;
        znT_inserttimer(nextticks);
        nextticks = next;
    }
}

static unsigned znT_getnexttime(zn_State *S, unsigned time) {
    if (S->timers == NULL)
        return ~(unsigned)0; /* no timer, wait forever */
    if (S->timers->time <= time)
        return 0; /* immediately */
    return S->timers->time - time;
}


/* system specified routines */
#ifdef _WIN32 /* IOCP (Completion Port) implementations */

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>

#ifdef _MSC_VER
# pragma warning(disable:4996)
# pragma comment(lib, "ws2_32")
#endif /* _MSC_VER */

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
    struct zn_Request *next;
    struct zn_Request **pprev;
    zn_RequestType type;
} zn_Request;

typedef struct zn_Post {
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

typedef struct zn_IOCPState {
    zn_State base;
    HANDLE iocp;
    zn_Request *requests;
} zn_IOCPState;

#define zn_iocp(S)     (((zn_IOCPState*)(S))->iocp)
#define zn_requests(S) (((zn_IOCPState*)(S))->requests)

/* tcp */

struct zn_Tcp {
    zn_Tcp *next;
    zn_Tcp **pprev;
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    zn_Request connect_request;
    zn_Request send_request;
    zn_Request recv_request;
    SOCKET socket;
    zn_PeerInfo info;
    WSABUF sendBuffer;
    WSABUF recvBuffer;
};

static int zn_getextension(SOCKET socket, GUID* gid, void *fn) {
    DWORD dwSize = 0;
    return WSAIoctl(socket,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            gid, sizeof(*gid),
            fn, sizeof(void(PASCAL*)(void)),
            &dwSize, NULL, NULL) == 0;
}

static int zn_inittcp(zn_Tcp *tcp) {
    SOCKET socket;
    SOCKADDR_IN localAddr;

    if ((socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    if (bind(socket, (struct sockaddr *)&localAddr,
                sizeof(localAddr)) != 0) {
        closesocket(socket);
        return ZN_EBIND;
    }

    if (CreateIoCompletionPort((HANDLE)socket, zn_iocp(tcp->S),
                (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    tcp->socket = socket;
    return ZN_OK;
}

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned port) {
    strcpy(tcp->info.addr, addr);
    tcp->info.port = port;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    zn_Tcp *tcp;
    if (S->closing > ZN_CLOSE_NORMAL
            || (tcp = (zn_Tcp*)malloc(sizeof(zn_Tcp))) == NULL)
        return NULL;
    memset(tcp, 0, sizeof(*tcp));
    tcp->S = S;
    tcp->socket = INVALID_SOCKET;
    tcp->connect_request.type = ZN_TCONNECT;
    tcp->send_request.type = ZN_TSEND;
    tcp->recv_request.type = ZN_TRECV;
    znL_insert(&S->tcps, tcp);
    return tcp;
}

ZN_API int zn_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    if (tcp->socket != INVALID_SOCKET) {
        if (closesocket(tcp->socket) != 0)
            ret = ZN_ERROR;
        tcp->socket = INVALID_SOCKET;
    }
    return ret;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    if (tcp->connect_handler
            || tcp->recv_handler
            || tcp->send_handler)
        tcp->S = NULL; /* mark tcp is dead */
    else {
        znL_remove(tcp);
        free(tcp);
    }
}

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info) {
    *info = tcp->info;
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, zn_ConnectHandler *cb, void *ud) {
    static LPFN_CONNECTEX lpConnectEx = NULL;
    static GUID gid = WSAID_CONNECTEX;
    DWORD dwLength = 0;
    SOCKADDR_IN remoteAddr;
    char buf[1];
    int err;
    if (tcp->S == NULL)                return ZN_ESTATE;
    if (tcp->socket != INVALID_SOCKET) return ZN_ESTATE;
    if (tcp->connect_handler != NULL)  return ZN_EBUSY;
    if (cb == NULL)                    return ZN_EPARAM;

    if ((err = zn_inittcp(tcp)) != ZN_OK)
        return err;

    if (!lpConnectEx && !zn_getextension(tcp->socket, &gid, &lpConnectEx))
        return ZN_ECONNECT;

    memset(&remoteAddr, 0, sizeof(remoteAddr));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_addr.s_addr = inet_addr(addr);
    remoteAddr.sin_port = htons(port);
    zn_setinfo(tcp, addr, port);

    if (!lpConnectEx(tcp->socket,
                (struct sockaddr *)&remoteAddr, sizeof(remoteAddr),
                buf, 0, &dwLength, &tcp->connect_request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(tcp->socket);
        tcp->socket = INVALID_SOCKET;
        return ZN_ECONNECT;
    }

    znL_insert(&zn_requests(tcp->S), &tcp->connect_request);
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    return ZN_OK;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len, zn_SendHandler *cb, void *ud) {
    DWORD dwTemp1=0;
    if (tcp->S == NULL)                return ZN_ESTATE;
    if (tcp->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (tcp->send_handler != NULL)     return ZN_EBUSY;
    if (cb == NULL || len == 0)        return ZN_EPARAM;

    tcp->sendBuffer.buf = (char*)buff;
    tcp->sendBuffer.len = len;
    if (WSASend(tcp->socket, &tcp->sendBuffer, 1,
                &dwTemp1, 0, &tcp->send_request.overlapped, NULL) != 0
            && WSAGetLastError() != WSA_IO_PENDING)
    {
        tcp->sendBuffer.buf = NULL;
        tcp->sendBuffer.len = 0;
        zn_closetcp(tcp);
        return ZN_ERROR;
    }

    znL_insert(&zn_requests(&tcp->S), &tcp->send_request);
    tcp->send_handler = cb;
    tcp->send_ud = ud;
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
    if (tcp->S == NULL)                return ZN_ESTATE;
    if (tcp->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (tcp->recv_handler != NULL)     return ZN_EBUSY;
    if (cb == NULL || len == 0)        return ZN_EPARAM;

    tcp->recvBuffer.buf = buff;
    tcp->recvBuffer.len = len;
    if (WSARecv(tcp->socket, &tcp->recvBuffer, 1,
                &dwRecv, &dwFlag, &tcp->recv_request.overlapped, NULL) != 0
            && WSAGetLastError() != WSA_IO_PENDING)
    {
        tcp->recvBuffer.buf = NULL;
        tcp->recvBuffer.len = 0;
        zn_closetcp(tcp);
        return ZN_ERROR;
    }

    znL_insert(&zn_requests(tcp->S), &tcp->recv_request);
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, BOOL bSuccess) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    tcp->connect_handler = NULL;
    if (tcp->S == NULL) {
        assert(tcp->socket == INVALID_SOCKET);
        /* cb(tcp->connect_ud, tcp, ZN_ECLOSED); */
        znL_remove(tcp);
        free(tcp);
    }
    else if (!bSuccess) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
    }
    else {
        BOOL bEnable = 1;
        if (setsockopt(tcp->socket, IPPROTO_TCP, TCP_NODELAY,
                    (char*)&bEnable, sizeof(bEnable)) != 0)
        { /* XXX */ }
        cb(tcp->connect_ud, tcp, ZN_OK);
    }
}

static void zn_onsend(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_SendHandler *cb = tcp->send_handler;
    assert(tcp->send_handler);
    tcp->send_handler = NULL;
    if (tcp->S == NULL || tcp->socket == INVALID_SOCKET) {
        assert(tcp->socket == INVALID_SOCKET);
        /* cb(tcp->send_ud, tcp, ZN_ECLOSED, dwBytes); */
        if (tcp->recv_handler == NULL) {
            znL_remove(tcp);
            free(tcp);
        }
    }
    else if (!bSuccess) {
        zn_closetcp(tcp);
        cb(tcp->send_ud, tcp, ZN_ERROR, dwBytes);
    }
    else
        cb(tcp->send_ud, tcp, ZN_OK, dwBytes);
}

static void zn_onrecv(zn_Tcp *tcp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvHandler *cb = tcp->recv_handler;
    assert(tcp->recv_handler);
    tcp->recv_handler = NULL;
    if (tcp->S == NULL) {
        assert(tcp->socket == INVALID_SOCKET);
        /* cb(tcp->recv_ud, tcp, ZN_ECLOSED, dwBytes); */
        if (tcp->send_handler == NULL) {
            znL_remove(tcp);
            free(tcp);
        }
    }
    else if (dwBytes == 0 || tcp->socket == INVALID_SOCKET) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ECLOSED, dwBytes);
    }
    else if (!bSuccess) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_EHANGUP, dwBytes);
    }
    else 
        cb(tcp->recv_ud, tcp, ZN_OK, dwBytes);
}

/* accept */

struct zn_Accept {
    zn_Accept *next;
    zn_Accept **pprev;
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    zn_Request accept_request;
    SOCKET socket;
    SOCKET client;
    DWORD recv_length;
    char recv_buffer[(sizeof(SOCKADDR_IN)+16)*2];
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    zn_Accept *accept;
    if (S->closing > ZN_CLOSE_NORMAL
            || (accept = (zn_Accept*)malloc(sizeof(zn_Accept))) == NULL)
        return NULL;
    memset(accept, 0, sizeof(*accept));
    accept->S = S;
    accept->socket = INVALID_SOCKET;
    accept->client = INVALID_SOCKET;
    accept->accept_request.type = ZN_TACCEPT;
    znL_insert(&S->accepts, accept);
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    if (accept->S == NULL) return;
    zn_closeaccept(accept);
    if (accept->accept_handler != NULL) {
        accept->S = NULL; /* mark dead */
        return;
    }
    znL_remove(accept);
    free(accept);
}

ZN_API int zn_closeaccept(zn_Accept *accept) {
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

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port) {
    SOCKADDR_IN sockAddr;
    BOOL bReuseAddr = TRUE;
    SOCKET socket;
    if (accept->socket != INVALID_SOCKET) return ZN_ESTATE;
    if (accept->accept_handler != NULL)   return ZN_EBUSY;

    if ((socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    if (setsockopt(accept->socket, SOL_SOCKET, SO_REUSEADDR,
                (char*)&bReuseAddr, sizeof(BOOL)) != 0)
    { /* XXX */ }

    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(addr);
    sockAddr.sin_port = htons(port);
    if (bind(socket, (struct sockaddr *)&sockAddr,
                sizeof(sockAddr)) != 0)
    {
        closesocket(socket);
        return ZN_EBIND;
    }

    if (listen(socket, SOMAXCONN) != 0) {
        closesocket(socket);
        return ZN_ERROR;
    }

    if (CreateIoCompletionPort((HANDLE)socket, zn_iocp(accept->S),
                (ULONG_PTR)accept, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    accept->socket = socket;
    return ZN_OK;
}

ZN_API int zn_accept(zn_Accept *accept, zn_AcceptHandler *cb, void *ud) {
    static GUID gid = WSAID_ACCEPTEX;
    static LPFN_ACCEPTEX lpAcceptEx = NULL;
    SOCKET socket;
    if (accept->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (cb == NULL)                       return ZN_EPARAM;

    if (!lpAcceptEx && !zn_getextension(accept->socket, &gid, &lpAcceptEx))
        return ZN_ERROR;

    if ((socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    if (!lpAcceptEx(accept->socket, socket,
                accept->recv_buffer, 0,
                sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16,
                &accept->recv_length, &accept->accept_request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(socket);
        return ZN_ERROR;
    }

    znL_insert(&zn_requests(accept->S), &accept->accept_request);
    accept->accept_handler = cb;
    accept->accept_ud = ud;
    accept->client = socket;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *accept, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID gid = WSAID_GETACCEPTEXSOCKADDRS;
    zn_AcceptHandler *cb = accept->accept_handler;
    BOOL bEnable = 1;
    zn_Tcp *tcp;
    struct sockaddr *paddr1 = NULL;
    struct sockaddr *paddr2 = NULL;
    int tmp1 = 0;
    int tmp2 = 0;
    accept->accept_handler = NULL;

    if (accept->S == NULL) {
        /* cb(accept->ud, accept, ZN_ECLOSED, NULL); */
        znL_remove(accept);
        free(accept);
        return;
    }

    if (!bSuccess) {
        /* zn_closeaccept(accept); */
        cb(accept->accept_ud, accept, ZN_ERROR, NULL);
        return;
    }

    if (setsockopt(accept->client, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                (char*)&accept->socket, sizeof(accept->socket)) != 0)
    { /* XXX */ }
    if (setsockopt(accept->client, IPPROTO_TCP, TCP_NODELAY,
                (char*)&bEnable, sizeof(bEnable)) != 0)
    { /* XXX */ }

    if (!lpGetAcceptExSockaddrs && !zn_getextension(accept->client,
                &gid, &lpGetAcceptExSockaddrs))
        return;

    lpGetAcceptExSockaddrs(accept->recv_buffer,
            accept->recv_length,
            sizeof(SOCKADDR_IN)+16,
            sizeof(SOCKADDR_IN)+16,
            &paddr1, &tmp1, &paddr2, &tmp2);

    tcp = zn_newtcp(accept->S);
    tcp->socket = accept->client;
    accept->client = INVALID_SOCKET;
    if (CreateIoCompletionPort((HANDLE)tcp->socket,
                zn_iocp(tcp->S), (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(tcp->socket);
        znL_remove(tcp);
        free(tcp);
        return;
    }

    zn_setinfo(tcp, 
            inet_ntoa(((struct sockaddr_in*)paddr2)->sin_addr),
            ntohs(((struct sockaddr_in*)paddr2)->sin_port));
    cb(accept->accept_ud, accept, ZN_OK, tcp);
}

/* udp */

struct zn_Udp {
    zn_Udp *next;
    zn_Udp **pprev;
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
    zn_Request recv_request;
    SOCKET socket;
    WSABUF recvBuffer;
    SOCKADDR_IN recvFrom;
    INT recvFromLen;
};

static int zn_initudp(zn_Udp *udp, const char *addr, unsigned port) {
    SOCKET socket;
    SOCKADDR_IN sockAddr;

    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(addr);
    sockAddr.sin_port = htons(port);

    socket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
            WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET)
        return ZN_ESOCKET;

    if (bind(socket, (struct sockaddr*)&sockAddr, sizeof(SOCKADDR_IN)) != 0)
        return ZN_EBIND;

    if (CreateIoCompletionPort((HANDLE)socket, zn_iocp(udp->S),
                (ULONG_PTR)udp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    udp->socket = socket;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    zn_Udp *udp;
    if (S->closing > ZN_CLOSE_NORMAL
            || (udp = (zn_Udp*)malloc(sizeof(zn_Udp))) == NULL)
        return NULL;
    memset(udp, 0, sizeof(*udp));
    udp->S = S;
    udp->socket = INVALID_SOCKET;
    udp->recv_request.type = ZN_TRECVFROM;
    if (!zn_initudp(udp, addr, port)) {
        free(udp);
        return NULL;
    }
    znL_insert(&S->udps, udp);
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    closesocket(udp->socket);
    if (udp->recv_handler != NULL)
        udp->S = NULL; /* mark dead */
    else {
        znL_remove(udp);
        free(udp);
    }
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned port) {
    SOCKADDR_IN dst;
    if (udp->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (len == 0 || len >1200)         return ZN_EPARAM;

    memset(&dst, 0, sizeof(SOCKADDR_IN));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(addr);
    dst.sin_port = htons(port);
    sendto(udp->socket, buff, len, 0, (struct sockaddr*)&dst, sizeof(dst));
    return ZN_OK;
}

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len, zn_RecvFromHandler *cb, void *ud) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
    if (udp->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (udp->recv_handler)             return ZN_EBUSY;
    if (len == 0 || cb == NULL)        return ZN_EPARAM;

    udp->recvBuffer.buf = buff;
    udp->recvBuffer.len = len;

    memset(&udp->recvFrom, 0, sizeof(udp->recvFrom));
    udp->recvFromLen = sizeof(udp->recvFrom);
    if ((WSARecvFrom(udp->socket, &udp->recvBuffer, 1, &dwRecv, &dwFlag,
                (struct sockaddr*)&udp->recvFrom, &udp->recvFromLen,
                &udp->recv_request.overlapped, NULL) != 0)
            && WSAGetLastError() != WSA_IO_PENDING)
    {
        udp->recvBuffer.buf = NULL;
        udp->recvBuffer.len = 0;
        return ZN_ERROR;
    }

    znL_insert(&zn_requests(udp->S), &udp->recv_request);
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvFromHandler *cb = udp->recv_handler;
    if (udp->S == NULL) {
        /* cb(udp->recv_ud, udp, ZN_ERROR, dwBytes, "0.0.0.0", 0); */
        znL_remove(udp);
        free(udp);
        return;
    }
    if (!cb) return;
    udp->recv_handler = NULL;
    if (bSuccess && dwBytes > 0)
        cb(udp->recv_ud, udp, ZN_OK, dwBytes,
                inet_ntoa(((struct sockaddr_in*)&udp->recvFrom)->sin_addr),
                ntohs(udp->recvFrom.sin_port));
    else cb(udp->recv_ud, udp, ZN_ERROR, dwBytes,
            "0.0.0.0", 0);
}

/* poll */

static BOOL zn_initialized = FALSE;

ZN_API void zn_initialize(void) {
    if (!zn_initialized) {
        WORD version = MAKEWORD(2, 2);
        WSADATA d;
        if (WSAStartup(version, &d) != 0) {
            abort();
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

ZN_API zn_State *zn_newstate(void) {
    zn_IOCPState *S = (zn_IOCPState*)malloc(sizeof(zn_IOCPState));
    if (S == NULL) return NULL;
    S->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
            NULL, (ULONG_PTR)0, 1);
    S->requests = NULL;
    if (S->iocp != NULL)
        return znS_init(&S->base);
    free(S);
    return NULL;
}

ZN_API void zn_close(zn_State *S) {
    int closing = S->closing;
    if (closing == ZN_CLOSE_IN_RUN || closing == ZN_CLOSE_PREPARE_IN_RUN) {
        S->closing = ZN_CLOSE_PREPARE_IN_RUN;
        return;
    }
    /* 0. doesn't allow create new objects */
    S->closing = ZN_CLOSE_PREPARE;
    /* 1. cancel all operations */
    znS_clear(S);
    /* 2. wait for uncompleted operations */
    while (zn_requests(S))
        zn_run(S, ZN_RUN_ONCE);
    /* 3. cleanup reources and free */
    CloseHandle(zn_iocp(S));
    free(S);
}

ZN_API unsigned zn_time(void) {
    DWORD dwTick = GetTickCount();
    return (unsigned)dwTick;
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *ps = (zn_Post*)malloc(sizeof(zn_Post));
    if (ps == NULL) return 0;
    ps->handler = cb;
    ps->ud = ud;
    PostQueuedCompletionStatus(zn_iocp(S), 0, 0, (LPOVERLAPPED)ps);
    return 1;
}

static int znS_poll(zn_State *S, int checkonly) {
    DWORD dwBytes = 0;
    ULONG_PTR upComKey = (ULONG_PTR)0;
    LPOVERLAPPED pOverlapped = NULL;
    zn_Request *req;
    BOOL bRet;

    S->closing = ZN_CLOSE_IN_RUN;
    znT_updatetimer(S, zn_time());
    bRet = GetQueuedCompletionStatus(zn_iocp(S),
            &dwBytes, &upComKey, &pOverlapped,
            checkonly ? 0 : znT_getnexttime(S, zn_time()));
    znT_updatetimer(S, zn_time());
    if (!bRet && !pOverlapped) /* time out */
        goto out;

    if (upComKey == 0) {
        zn_Post *ps = (zn_Post*)pOverlapped;
        if (ps->handler)
            ps->handler(ps->ud, S);
        free(ps);
        goto out;
    }

    req = (zn_Request*)pOverlapped;
    znL_remove(req);
    znL_init(req); /* for debug purpose */
    switch (req->type) {
        case ZN_TACCEPT:   zn_onaccept((zn_Accept*)upComKey, bRet); break;
        case ZN_TRECV:     zn_onrecv((zn_Tcp*)upComKey, bRet, dwBytes); break;
        case ZN_TSEND:     zn_onsend((zn_Tcp*)upComKey, bRet, dwBytes); break;
        case ZN_TCONNECT:  zn_onconnect((zn_Tcp*)upComKey, bRet); break;
        case ZN_TRECVFROM: zn_onrecvfrom((zn_Udp*)upComKey, bRet, dwBytes); break;
        case ZN_TSENDTO: /* do not have this operation */
        default: ;
    }

out:
    if (S->closing == ZN_CLOSE_PREPARE_IN_RUN) {
        S->closing = ZN_CLOSE_NORMAL; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->closing = ZN_CLOSE_NORMAL;
    return S->timers != NULL || zn_requests(S) != NULL;
}


#elif defined(__linux__) /* epoll implementations */

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>


#define ZN_MAX_EVENTS 4096

typedef struct zn_Post {
    struct zn_Post *next;
    struct zn_Post **pprev;
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

typedef struct zn_Result {
    struct zn_Result *next; 
    struct zn_Result **pprev; 
    int err;
    zn_Tcp *tcp;
} zn_Result;

typedef struct zn_EPollState {
    zn_State base;
    int epoll;
    int eventfd;
    pthread_spinlock_t post_lock;
    znL_type(zn_Post) posts;
    znL_type(zn_Result) results;
} zn_EPollState;

#define zn_state(S) ((zn_EPollState*)(S))
#define zn_epoll(S) (zn_state(S)->epoll)

/* post queue */

static void znP_init(zn_EPollState *S) {
    pthread_spin_init(&S->post_lock, 0);
    znL_init(&S->posts);
}

static void znP_add(zn_EPollState *S, zn_Post *ps) {
    pthread_spin_lock(&S->post_lock);
    znL_insert(S->posts.pprev, ps);
    S->posts.pprev = &ps->next;
    pthread_spin_unlock(&S->post_lock);
}

static void znP_process(zn_EPollState *S) {
    zn_Post *ps = S->posts.next;
    if (ps == NULL) return;
    pthread_spin_lock(&S->post_lock);
    znL_init(&S->posts);
    pthread_spin_unlock(&S->post_lock);
    while (ps) {
        zn_Post *next = ps->next;
        if (ps->handler)
            ps->handler(ps->ud, &S->base);
        free(ps);
        ps = next;
    }
}

/* result queue */

static void zn_onresult(zn_Result *result);

static void znR_init(zn_EPollState *S) {
    znL_init(&S->results);
}

static void znR_add(zn_EPollState *S, int err, zn_Result *result) {
    result->err = err;
    znL_insert(S->results.pprev, result);
    S->results.pprev = &result->next;
}

static void znR_process(zn_EPollState *S) {
    while (S->results.next) {
        zn_Result *results = S->results.next;
        znL_init(&S->results);
        while (results) {
            zn_Result *next = results->next;
            znL_remove(results);
            znL_init(results); /* for debug */
            zn_onresult(results);
            results = next;
        }
    }
}

/* uniform socket info */

typedef enum zn_SocketType {
    ZN_SOCK_ACCEPT,
    ZN_SOCK_TCP,
    ZN_SOCK_UDP,
} zn_SocketType;

typedef struct zn_SocketInfo {
    int type;
    void *head;
} zn_SocketInfo;

/* tcp */

typedef struct zn_DataBuffer {
    size_t len;
    char  *buff;
} zn_DataBuffer;

struct zn_Tcp {
    struct zn_Tcp *next;
    struct zn_Tcp **pprev;
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    struct epoll_event event;
    int fd;
    int status;
    zn_SocketInfo info;
    zn_Result send_result;
    zn_Result recv_result;
    zn_PeerInfo peer_info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned port) {
    strcpy(tcp->peer_info.addr, addr);
    tcp->peer_info.port = port;
}

static zn_Tcp *zn_tcpfromfd(zn_State *S, int fd, struct sockaddr_in *remote_addr) {
    int flag = 1;
    zn_Tcp *tcp = zn_newtcp(S);
    tcp->fd = fd;

    if (fcntl(tcp->fd, F_SETFL, fcntl(tcp->fd, F_GETFL)|O_NONBLOCK) != 0)
    { /* XXX */ }

    tcp->status = EPOLLIN|EPOLLOUT;
    tcp->event.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP;
    if (epoll_ctl(zn_epoll(tcp->S), EPOLL_CTL_ADD,
                tcp->fd, &tcp->event) != 0)
    {
        zn_deltcp(tcp);
        return NULL;
    }

    if (setsockopt(tcp->fd, IPPROTO_TCP, TCP_NODELAY,
                (char*)&flag, sizeof(flag)) != 0)
    { /* XXX */ }

    zn_setinfo(tcp, 
            inet_ntoa(remote_addr->sin_addr),
            ntohs(remote_addr->sin_port));
    return tcp;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    zn_Tcp *tcp;
    if (S->closing > ZN_CLOSE_NORMAL
            || (tcp = (zn_Tcp*)malloc(sizeof(zn_Tcp))) == NULL)
        return NULL;
    memset(tcp, 0, sizeof(*tcp));
    tcp->S = S;
    tcp->event.data.ptr = &tcp->info;
    tcp->fd = -1;
    tcp->status = EPOLLIN|EPOLLOUT;
    tcp->info.type = ZN_SOCK_TCP;
    tcp->info.head = tcp;
    tcp->send_result.tcp = tcp;
    tcp->recv_result.tcp = tcp;
    znL_insert(&S->tcps, tcp);
    return tcp;
}

ZN_API int zn_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    tcp->event.events = 0;
    epoll_ctl(zn_epoll(tcp->S), EPOLL_CTL_DEL,
            tcp->fd, &tcp->event);
    if (tcp->fd != -1) {
        if (close(tcp->fd) != 0)
            ret = ZN_ERROR;
        tcp->fd = -1;
    }
    return ret;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    znL_remove(tcp);
    free(tcp);
}

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info) {
    *info = tcp->peer_info;
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, zn_ConnectHandler *cb, void *ud) {
    struct sockaddr_in remoteAddr;
    int fd, ret;
    if (tcp->S == NULL)               return ZN_ESTATE;
    if (tcp->fd != -1)                return ZN_ESTATE;
    if (tcp->connect_handler != NULL) return ZN_EBUSY;
    if (cb == NULL)                   return ZN_EPARAM;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return ZN_ESOCKET;

    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL)|O_NONBLOCK) != 0)
    { /* XXX */ }

    memset(&remoteAddr, 0, sizeof(remoteAddr));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_addr.s_addr = inet_addr(addr);
    remoteAddr.sin_port = htons(port);
    zn_setinfo(tcp, addr, port);

    ret = connect(fd, (struct sockaddr *)&remoteAddr,
            sizeof(remoteAddr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(fd);
        return ZN_ECONNECT;
    }

    tcp->event.events = EPOLLOUT;
    if (epoll_ctl(zn_epoll(tcp->S), EPOLL_CTL_ADD,
                fd, &tcp->event) != 0) {
        close(fd);
        return ZN_EPOLL;
    }

    tcp->fd = fd;
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    return ZN_OK;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len, zn_SendHandler *cb, void *ud) {
    if (tcp->S == NULL)            return ZN_ESTATE;
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->send_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;
    tcp->send_buffer.buff = (char*)buff;
    tcp->send_buffer.len = len;
    tcp->send_handler = cb;
    tcp->send_ud = ud;
    if ((tcp->status & EPOLLOUT) != 0) {
        int bytes = send(tcp->fd, buff, len, 0);
        if (bytes >= 0) {
            tcp->send_buffer.len = bytes;
            znR_add(zn_state(tcp->S), ZN_OK, &tcp->send_result);
        }
        else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(zn_state(tcp->S), ZN_ERROR, &tcp->send_result);
        else
            tcp->status &= ~EPOLLOUT;
    }
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    if (tcp->S == NULL)            return ZN_ESTATE;
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;
    tcp->recv_buffer.buff = buff;
    tcp->recv_buffer.len = len;
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    if ((tcp->status & EPOLLIN) != 0) {
        int bytes = recv(tcp->fd, buff, len, 0);
        if (bytes > 0) {
            tcp->recv_buffer.len = bytes;
            znR_add(zn_state(tcp->S), ZN_OK, &tcp->recv_result);
        }
        else if (bytes == 0)
            znR_add(zn_state(tcp->S), ZN_ECLOSED, &tcp->recv_result);
        else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(zn_state(tcp->S), ZN_ERROR, &tcp->recv_result);
        else
            tcp->status &= ~EPOLLIN;
    }

    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int eventmask) {
    int flag = 1;
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    tcp->connect_handler = NULL;

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    tcp->status = EPOLLIN|EPOLLOUT;
    tcp->event.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP;
    if (epoll_ctl(zn_epoll(tcp->S), EPOLL_CTL_MOD,
                tcp->fd, &tcp->event) != 0)
    {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    if (setsockopt(tcp->fd, IPPROTO_TCP, TCP_NODELAY,
                (char*)&flag, sizeof(flag)) != 0)
    { /* XXX */ }

    cb(tcp->connect_ud, tcp, ZN_OK);
}

static void zn_onresult(zn_Result *result) {
    zn_Tcp *tcp = result->tcp;
    if (result == &tcp->send_result) {
        zn_DataBuffer buff = tcp->send_buffer;
        zn_SendHandler *cb = tcp->send_handler;
        assert(tcp->send_handler != NULL);
        tcp->send_handler = NULL;
        tcp->send_buffer.buff = NULL;
        tcp->send_buffer.len = 0;
        assert(tcp->fd != -1);
        if (result->err == ZN_OK)
            cb(tcp->send_ud, tcp, ZN_OK, buff.len);
        else if (tcp->recv_handler == NULL) {
            zn_closetcp(tcp);
            cb(tcp->send_ud, tcp, result->err, 0);
        }
    }
    else {
        zn_DataBuffer buff = tcp->recv_buffer;
        zn_RecvHandler *cb = tcp->recv_handler;
        assert(tcp->recv_handler != NULL);
        tcp->recv_handler = NULL;
        tcp->recv_buffer.buff = NULL;
        tcp->recv_buffer.len = 0;
        assert(tcp->fd != -1);
        if (result->err == ZN_OK)
            cb(tcp->recv_ud, tcp, ZN_OK, buff.len);
        else if (tcp->send_handler == NULL) {
            zn_closetcp(tcp);
            cb(tcp->recv_ud, tcp, result->err, 0);
        }
    }
}

static void zn_onevent(zn_Tcp *tcp, int eventmask) {
    int status = tcp->status;
    if ((eventmask & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) != 0)
        tcp->status |= EPOLLIN|EPOLLOUT;
    if ((eventmask & EPOLLIN) != 0 && (status & EPOLLIN) == 0) {
        zn_RecvHandler *cb = tcp->recv_handler;
        tcp->recv_handler = NULL;
        tcp->status |= EPOLLIN;
        if (cb == NULL) return;
        zn_recv(tcp, tcp->recv_buffer.buff, tcp->recv_buffer.len,
                cb, tcp->recv_ud);
    }
    if ((eventmask & EPOLLOUT) != 0 && (status & EPOLLOUT) == 0) {
        zn_SendHandler *cb = tcp->send_handler;
        tcp->send_handler = NULL;
        tcp->status |= EPOLLOUT;
        if (cb == NULL) return;
        zn_send(tcp, tcp->send_buffer.buff, tcp->send_buffer.len,
                cb, tcp->send_ud);
    }
}

/* accept */

struct zn_Accept {
    zn_Accept *next;
    zn_Accept **pprev;
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    struct epoll_event event;
    int fd;
    zn_SocketInfo info;
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    zn_Accept *accept;
    if (S->closing > ZN_CLOSE_NORMAL
            || (accept = (zn_Accept*)malloc(sizeof(zn_Accept))) == NULL)
        return NULL;
    memset(accept, 0, sizeof(*accept));
    accept->S = S;
    accept->event.data.ptr = &accept->info;
    accept->fd = -1;
    accept->info.type = ZN_SOCK_ACCEPT;
    accept->info.head = accept;
    znL_insert(&S->accepts, accept);
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    zn_closeaccept(accept);
    znL_remove(accept);
    free(accept);
}

ZN_API int zn_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    accept->event.events = 0;
    epoll_ctl(zn_epoll(accept->S), EPOLL_CTL_DEL,
            accept->fd, &accept->event);
    if (accept->fd != -1) {
        if (close(accept->fd) != 0)
            ret = ZN_ERROR;
        accept->fd = -1;
    }
    return ret;
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port) {
    struct sockaddr_in sock_addr;
    int reuse_addr = 1;
    int fd;
    if (accept->fd != -1)               return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return ZN_ESOCKET;

    accept->event.events = EPOLLIN;
    if (epoll_ctl(zn_epoll(accept->S), EPOLL_CTL_ADD,
                fd, &accept->event) != 0)
    {
        close(fd);
        return ZN_EPOLL;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                (char*)&reuse_addr, sizeof(reuse_addr)) != 0)
    { /* XXX */ }

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(addr);
    sock_addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) != 0) {
        close(fd);
        return ZN_EBIND;
    }

    if (listen(fd, SOMAXCONN) != 0) {
        close(fd);
        return ZN_ERROR;
    }

    accept->fd = fd;
    return ZN_OK;
}

ZN_API int zn_accept(zn_Accept *accept, zn_AcceptHandler *cb, void *ud) {
    if (accept->accept_handler != NULL) return ZN_EBUSY;
    if (accept->fd == -1)               return ZN_ESTATE;
    if (cb == NULL)                     return ZN_EPARAM;
    accept->accept_handler = cb;
    accept->accept_ud = ud;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *a, int eventmask) {
    zn_AcceptHandler *cb = a->accept_handler;
    a->accept_handler = NULL;
    if (cb == NULL) return;

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        zn_closeaccept(a);
        cb(a->accept_ud, a, ZN_ERROR, NULL);
        return;
    }

    if ((eventmask & EPOLLIN) != 0) {
        struct sockaddr_in remote_addr;
        socklen_t addr_size;
        int ret = accept(a->fd, (struct sockaddr*)&remote_addr, &addr_size);
        if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (tcp != NULL) cb(a->accept_ud, a, ZN_OK, tcp);
        }
        if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            zn_closeaccept(a);
            cb(a->accept_ud, a, ZN_ERROR, NULL);
        }
    }
}

/* udp */

struct zn_Udp {
    zn_Udp *next;
    zn_Udp **pprev;
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
    struct epoll_event event;
    int fd;
    zn_SocketInfo info;
    zn_DataBuffer recv_buffer;
    struct sockaddr_in recvFrom;
    socklen_t recvFromLen;
};

static int zn_initudp(zn_Udp *udp, const char *addr, unsigned port) {
    int fd;
    struct sockaddr_in sock_addr;

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(addr);
    sock_addr.sin_port = htons(port);

    if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        return ZN_ESOCKET;

    if (bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) != 0)
        return ZN_EBIND;

    udp->event.events = EPOLLIN;
    if (epoll_ctl(zn_epoll(udp->S), EPOLL_CTL_ADD,
                fd, &udp->event) != 0) {
        close(fd);
        return ZN_EPOLL;
    }

    udp->fd = fd;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    zn_Udp *udp;
    if (S->closing > ZN_CLOSE_NORMAL
            || (udp = (zn_Udp*)malloc(sizeof(zn_Udp))) == NULL)
        return NULL;
    memset(udp, 0, sizeof(*udp));
    udp->S = S;
    udp->event.data.ptr = &udp->info;
    udp->fd = -1;
    udp->info.type = ZN_SOCK_UDP;
    udp->info.head = udp;
    if (!zn_initudp(udp, addr, port)) {
        free(udp);
        return NULL;
    }
    znL_insert(&S->udps, udp);
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    udp->event.events = 0;
    epoll_ctl(zn_epoll(udp->S), EPOLL_CTL_DEL, udp->fd, &udp->event);
    close(udp->fd);
    znL_remove(udp);
    free(udp);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned port) {
    struct sockaddr_in dst;
    if (udp->fd == -1)         return ZN_ESTATE;
    if (len == 0 || len >1200) return ZN_EPARAM;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(addr);
    dst.sin_port = htons(port);
    sendto(udp->fd, buff, len, 0, (struct sockaddr*)&dst, sizeof(dst));
    return ZN_OK;
}

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len, zn_RecvFromHandler *cb, void *ud) {
    if (udp->fd == -1)          return ZN_ESTATE;
    if (udp->recv_handler)      return ZN_EBUSY;
    if (len == 0 || cb == NULL) return ZN_EPARAM;
    udp->recv_buffer.buff = buff;
    udp->recv_buffer.len = len;
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, int eventmask) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    if (cb == NULL) return;
    udp->recv_handler = NULL;
    udp->recv_buffer.buff = NULL;
    udp->recv_buffer.len = 0;

    if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
        cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
        return;
    }

    if ((eventmask & EPOLLIN) != 0) {
        int bytes;
        memset(&udp->recvFrom, 0, sizeof(udp->recvFrom));
        udp->recvFromLen = sizeof(udp->recvFrom);
        bytes = recvfrom(udp->fd, buff.buff, buff.len, 0,
                (struct sockaddr*)&udp->recvFrom, &udp->recvFromLen);
        if (bytes >= 0)
            cb(udp->recv_ud, udp, ZN_OK, bytes,
                    inet_ntoa(((struct sockaddr_in*)&udp->recvFrom)->sin_addr),
                    ntohs(udp->recvFrom.sin_port));
        else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
    }
}

/* poll */

ZN_API void zn_initialize(void) { }
ZN_API void zn_deinitialize(void) { }

ZN_API zn_State *zn_newstate(void) {
    struct epoll_event event;
    zn_EPollState *S = (zn_EPollState*)malloc(sizeof(zn_EPollState));
    if (S == NULL) return NULL;
    event.events = EPOLLIN;
    event.data.ptr = NULL;
    S->epoll = epoll_create(ZN_MAX_EVENTS);
    S->eventfd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    if (S->epoll == -1
            || S->eventfd == -1
            || epoll_ctl(S->epoll, EPOLL_CTL_ADD, S->eventfd, &event) != 0)
    {
        if (S->epoll != -1) close(S->epoll);
        if (S->eventfd != -1) close(S->eventfd);
        free(S);
        return NULL;
    }

    znP_init(S);
    znR_init(S);
    return znS_init(&S->base);
}

ZN_API void zn_close(zn_State *S) {
    int closing = S->closing;
    if (closing == ZN_CLOSE_IN_RUN || closing == ZN_CLOSE_PREPARE_IN_RUN) {
        S->closing = ZN_CLOSE_PREPARE_IN_RUN;
        return;
    }
    S->closing = ZN_CLOSE_PREPARE;
    znS_clear(S);
    close(zn_epoll(S));
    free(S);
}

ZN_API unsigned zn_time(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1)
        return 0;
    return (unsigned)(tv.tv_sec*1000+tv.tv_usec/1000);
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *ps = (zn_Post*)malloc(sizeof(zn_Post));
    if (ps == NULL) return 0;
    ps->handler = cb;
    ps->ud = ud;
    znP_add(zn_state(S), ps);
    eventfd_write(zn_state(S)->eventfd, (eventfd_t)1);
    return 1;
}

static void zn_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_SocketInfo *info = (zn_SocketInfo*)evt->data.ptr;
    zn_Tcp *tcp;
    int eventmask = evt->events;
    if (info == NULL) { /* post */
        eventfd_t value;
        eventfd_read(zn_state(S)->eventfd, &value);
        znP_process(zn_state(S));
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
        if (tcp->send_handler || tcp->recv_handler)
            zn_onevent(tcp, eventmask);
        break;
    case ZN_SOCK_UDP:
        zn_onrecvfrom((zn_Udp*)info->head, eventmask);
        break;
    default: ;
    }
}

static int znS_poll(zn_State *S, int checkonly) {
    int i, ret;
    struct epoll_event events[ZN_MAX_EVENTS];
    S->closing = ZN_CLOSE_IN_RUN;
    znT_updatetimer(S, zn_time());
    ret = epoll_wait(zn_epoll(S), events, ZN_MAX_EVENTS,
            checkonly ? 0 : znT_getnexttime(S, zn_time()));
    if (ret == -1 && errno != EINTR) /* error out */
        goto out;
    znT_updatetimer(S, zn_time());
    for (i = 0; i < ret; ++i)
        zn_dispatch(S, &events[i]);
    znR_process(zn_state(S));

out:
    if (S->closing == ZN_CLOSE_PREPARE_IN_RUN) {
        S->closing = ZN_CLOSE_NORMAL; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->closing = ZN_CLOSE_NORMAL;
    return S->timers != NULL;
}


#else /* select implementations */


#endif /* System specified routines */


static void znS_clear(zn_State *S) {
    zn_Accept *accept = S->accepts;
    zn_Tcp *tcp = S->tcps;
    zn_Udp *udp = S->udps;
    znT_cleartimers(S);
    while (accept) {
        zn_Accept *next = accept->next;
        zn_delaccept(accept);
        accept = next;
    }
    while (tcp) {
        zn_Tcp *next = tcp->next;
        zn_deltcp(tcp);
        tcp = next;
    }
    while (udp) {
        zn_Udp *next = udp->next;
        zn_deludp(udp);
        udp = next;
    }
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-lpthread' output='znet.so' */
