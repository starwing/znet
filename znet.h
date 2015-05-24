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
    char           addr[ZN_MAX_ADDRLEN];
    unsigned short port;
} zn_PeerInfo;

typedef void zn_PostHandler    (void *ud, zn_State *S);
typedef void zn_TimerHandler   (void *ud, zn_Timer *timer, unsigned delayed);
typedef void zn_AcceptHandler  (void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp);
typedef void zn_ConnectHandler (void *ud, zn_Tcp *tcp, unsigned err);
typedef void zn_SendHandler    (void *ud, zn_Tcp *tcp, unsigned err, unsigned count);
typedef void zn_RecvHandler    (void *ud, zn_Tcp *tcp, unsigned err, unsigned count);
typedef void zn_URecvHandler   (void *ud, zn_Udp *udp, unsigned err, unsigned count,
                                const char *addr, unsigned short port);


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
ZN_API void       zn_closeaccept (zn_Accept *accept);
ZN_API void       zn_delaccept   (zn_Accept *accept);

ZN_API int zn_listen (zn_Accept *accept, const char *addr, unsigned short port);
ZN_API int zn_accept (zn_Accept *accept, zn_AcceptHandler *cb, void *ud);


/* znet tcp socket routines */

ZN_API zn_Tcp* zn_newtcp   (zn_State *S);
ZN_API void    zn_closetcp (zn_Tcp *tcp);
ZN_API void    zn_deltcp   (zn_Tcp *tcp);

ZN_API void zn_getpeerinfo (zn_Tcp *tcp, zn_PeerInfo *info);

ZN_API int zn_connect (zn_Tcp *tcp, const char *addr, unsigned short port,
                       zn_ConnectHandler *cb, void *ud);

ZN_API int zn_send (zn_Tcp *tcp, const char *buff, unsigned len,
                    zn_SendHandler *cb, void *ud);
ZN_API int zn_recv (zn_Tcp *tcp,       char *buff, unsigned len,
                    zn_RecvHandler *cb, void *ud);


/* znet udp socket routines */

ZN_API zn_Udp* zn_newudp (zn_State *S, const char *addr, unsigned short port);
ZN_API void    zn_deludp (zn_Udp *udp);

ZN_API int zn_sendto   (zn_Udp *udp, const char *buff, unsigned len,
                        const char *addr, unsigned short port);
ZN_API int zn_recvfrom (zn_Udp *udp,       char *buff, unsigned len,
                        zn_URecvHandler *cb, void *ud);

ZN_NS_END


#endif /* znet_h */


/* implementations */
#ifdef ZN_IMPLEMENTATION

ZN_NS_BEGIN


#include <assert.h>
#include <stdlib.h>
#include <string.h>


/* half list routines */

#define znL_init(n) ((void)((n)->pprev = NULL, (n)->next = NULL ))

#define znL_insert(h, n)                          ((void)( \
            (n)->pprev = (h), (n)->next = *(h),            \
            (void)(*(h) && ((*(h))->pprev = &(n)->next)),  \
            *(h) = (n)                                   ))

#define znL_remove(n)                             ((void)( \
            (void)((n)->next && ((n)->next->pprev = (n)->pprev)),\
            (void)((n)->pprev && (*(n)->pprev = (n)->next))))


/* timer routines */

typedef struct zn_TimerState {
    zn_Timer *unused_timers;
    zn_Timer *timers;
} zn_TimerState;

struct zn_Timer {
    zn_Timer *next;
    zn_Timer **pprev;
    zn_State *S;
    void *ud;
    zn_TimerHandler *handler;
    unsigned starttime;
    unsigned time;
};

static void znT_inittimerstate(zn_TimerState *TS) {
    TS->unused_timers = NULL;
    TS->timers = NULL;
}

static void znT_cleartimers(zn_TimerState *TS) {
    while (TS->unused_timers) {
        zn_Timer *next = TS->unused_timers->next;
        free(TS->unused_timers);
        TS->unused_timers = next;
    }
    while (TS->timers) {
        zn_Timer *next = TS->timers->next;
        free(TS->timers);
        TS->timers = next;
    }
}

static zn_Timer *znT_newtimer(zn_State *S, zn_TimerState *TS) {
    zn_Timer *t = (zn_Timer*)malloc(sizeof(zn_Timer));
    t->S = S;
    t->ud = NULL;
    t->handler = NULL;
    t->starttime = 0;
    t->time = 0;
    znL_insert(&TS->unused_timers, t);
    return t;
}

static void znT_inserttimer(zn_TimerState *TS, zn_Timer *t) {
    zn_Timer **head = &TS->timers;
    while (*head != NULL && (*head)->time <= t->time)
        head = &(*head)->next;
    /* detach timer and insert into active linked list */
    znL_remove(t);
    znL_insert(head, t);
}

static void znT_updatetimer(zn_TimerState *TS, unsigned current) {
    zn_Timer *nextticks = NULL;
    while (TS->timers && TS->timers->time <= current) {
        zn_Timer *cur = TS->timers;
        znL_remove(cur);
        cur->pprev = NULL;
        cur->next = NULL;
        if (cur->handler) {
            unsigned elapsed = current - cur->starttime;
            cur->starttime = 0;
            cur->time = ~(unsigned)0;
            cur->handler(cur->ud, cur, elapsed);
        }
        if (!cur->pprev)
            znL_insert(&TS->unused_timers, cur);
        else if (cur->time <= current) { /* avoid forever loop */
            znL_remove(cur);
            znL_insert(&nextticks, cur);
        }
    }
    while (nextticks != NULL) {
        zn_Timer *next = nextticks->next;
        znT_inserttimer(TS, nextticks);
        nextticks = next;
    }
}

static unsigned znT_getnexttime(zn_TimerState *TS, unsigned time) {
    if (TS->timers == NULL)
        return ~(unsigned)0; /* no timer, wait forever */
    if (TS->timers->time <= time)
        return 0; /* immediately */
    return TS->timers->time - time;
}


/* system specified routines */
#ifdef _WIN32 /* CPIO (Completion Port) implementations */

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif /* WIN32_LEAN_AND_MEAN */
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
# define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>

#ifdef _MSC_VER
# pragma comment(lib, "ws2_32")
# pragma comment(lib, "mswsock")
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
    OVERLAPPED overlapped; /* must be first field */
    struct zn_Request *next;
    struct zn_Request **pprev;
    union {
        zn_Accept* accept;
        zn_Tcp* tcp;
        zn_Udp* udp;
    } u;
    zn_RequestType type;
} zn_Request;

typedef struct zn_PostState {
    zn_PostHandler *handler;
    void *ud;
} zn_PostState;

struct zn_State {
    HANDLE iocp;
    int closing;
    zn_TimerState TS;
    zn_Accept *accepts;
    zn_Tcp *tcps;
    zn_Udp *udps;
    zn_Request *requests;
};

/* tcp */

struct zn_Tcp {
    zn_Tcp *next;
    zn_Tcp **pprev;
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    SOCKET socket;
    zn_Request connect_request;
    zn_Request send_request;
    zn_Request recv_request;
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

    if (CreateIoCompletionPort((HANDLE)socket, tcp->S->iocp,
                (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    tcp->socket = socket;
    return ZN_OK;
}

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned short port) {
    strcpy(tcp->info.addr, addr);
    tcp->info.port = port;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    zn_Tcp *tcp;
    if (S->closing
            || (tcp = (zn_Tcp*)malloc(sizeof(zn_Tcp))) == NULL)
        return NULL;
    memset(tcp, 0, sizeof(*tcp));
    tcp->S = S;
    tcp->socket = INVALID_SOCKET;
    tcp->connect_request.u.tcp = tcp;
    tcp->connect_request.type = ZN_TCONNECT;
    tcp->send_request.u.tcp = tcp;
    tcp->send_request.type = ZN_TSEND;
    tcp->recv_request.u.tcp = tcp;
    tcp->recv_request.type = ZN_TRECV;
    znL_insert(&S->tcps, tcp);
    return tcp;
}

ZN_API void zn_closetcp(zn_Tcp *tcp) {
    if (tcp->socket != INVALID_SOCKET) {
        closesocket(tcp->socket);
        tcp->socket = INVALID_SOCKET;
    }
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

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned short port, zn_ConnectHandler *cb, void *ud) {
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

    znL_insert(&tcp->S->requests, &tcp->connect_request);
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

    znL_insert(&tcp->S->requests, &tcp->send_request);
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

    znL_insert(&tcp->S->requests, &tcp->recv_request);
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
    void *ud;
    zn_AcceptHandler *handler;
    SOCKET socket;
    SOCKET client;
    zn_Request request;
    DWORD recv_length;
    char recv_buffer[(sizeof(SOCKADDR_IN)+16)*2];
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    zn_Accept *accept;
    if (S->closing
            || (accept = (zn_Accept*)malloc(sizeof(zn_Accept))) == NULL)
        return NULL;
    memset(accept, 0, sizeof(*accept));
    accept->S = S;
    accept->socket = INVALID_SOCKET;
    accept->client = INVALID_SOCKET;
    accept->request.u.accept = accept;
    accept->request.type = ZN_TACCEPT;
    znL_insert(&S->accepts, accept);
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    if (accept->S == NULL) return;
    zn_closeaccept(accept);
    if (accept->handler != NULL) {
        accept->S = NULL; /* mark dead */
        return;
    }
    znL_remove(accept);
    free(accept);
}

ZN_API void zn_closeaccept(zn_Accept *accept) {
    if (accept->socket != INVALID_SOCKET) {
        closesocket(accept->socket);
        accept->socket = INVALID_SOCKET;
    }
    if (accept->client != INVALID_SOCKET) {
        closesocket(accept->client);
        accept->client = INVALID_SOCKET;
    }
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned short port) {
    SOCKADDR_IN sockAddr;
    BOOL bReuseAddr = TRUE;
    SOCKET socket;
    if (accept->socket != INVALID_SOCKET) return ZN_ESTATE;
    if (accept->handler != NULL)          return ZN_EBUSY;

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

    if (CreateIoCompletionPort((HANDLE)socket, accept->S->iocp,
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
                &accept->recv_length, &accept->request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(socket);
        return ZN_ERROR;
    }

    znL_insert(&accept->S->requests, &accept->request);
    accept->handler = cb;
    accept->ud = ud;
    accept->client = socket;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *accept, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID gid = WSAID_GETACCEPTEXSOCKADDRS;
    zn_AcceptHandler *cb = accept->handler;
    BOOL bEnable = 1;
    zn_Tcp *tcp;
    struct sockaddr *paddr1 = NULL;
    struct sockaddr *paddr2 = NULL;
    int tmp1 = 0;
    int tmp2 = 0;

    if (accept->S == NULL) {
        /* cb(accept->ud, accept, ZN_ECLOSED, NULL); */
        znL_remove(accept);
        free(accept);
        return;
    }

    accept->handler = NULL;
    if (!bSuccess) {
        /* zn_closeaccept(accept); */
        cb(accept->ud, accept, ZN_ERROR, NULL);
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
                tcp->S->iocp,
                (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(tcp->socket);
        znL_remove(tcp);
        free(tcp);
        return;
    }

    zn_setinfo(tcp, 
            inet_ntoa(((struct sockaddr_in*)paddr2)->sin_addr),
            ntohs(((struct sockaddr_in*)paddr2)->sin_port));
    cb(accept->ud, accept, ZN_OK, tcp);
}

/* udp */

struct zn_Udp {
    zn_Udp *next;
    zn_Udp **pprev;
    zn_State *S;
    void *recv_ud; zn_URecvHandler *recv_handler;
    SOCKET socket;
    zn_Request request;
    WSABUF recvBuffer;
    SOCKADDR_IN recvFrom;
    INT recvFromLen;
};

static int zn_initudp(zn_Udp *udp, const char *addr, unsigned short port) {
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

    if (CreateIoCompletionPort((HANDLE)socket, udp->S->iocp,
                (ULONG_PTR)udp, 1) == NULL)
    {
        closesocket(socket);
        return ZN_EPOLL;
    }

    udp->socket = socket;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned short port) {
    zn_Udp *udp;
    if (S->closing
            || (udp = (zn_Udp*)malloc(sizeof(zn_Udp))) == NULL)
        return NULL;
    memset(udp, 0, sizeof(*udp));
    udp->S = S;
    udp->socket = INVALID_SOCKET;
    udp->request.u.udp = udp;
    udp->request.type = ZN_TRECVFROM;
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

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned short port) {
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

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len, zn_URecvHandler *cb, void *ud) {
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
                &udp->request.overlapped, NULL) != 0)
            && WSAGetLastError() != WSA_IO_PENDING)
    {
        udp->recvBuffer.buf = NULL;
        udp->recvBuffer.len = 0;
        return ZN_ERROR;
    }

    znL_insert(&udp->S->requests, &udp->request);
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, BOOL bSuccess, DWORD dwBytes) {
    zn_URecvHandler *cb = udp->recv_handler;
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

ZN_API void zn_initialize(void) {
    WORD version = MAKEWORD(2, 2);
    WSADATA d;
    if (WSAStartup(version, &d) != 0) {
        abort();
    }
}

ZN_API void zn_deinitialize(void) {
    WSACleanup();
}

ZN_API zn_State *zn_newstate(void) {
    zn_State *S = (zn_State*)malloc(sizeof(zn_State));
    if (S == NULL) return NULL;
    S->iocp = CreateIoCompletionPort(
            INVALID_HANDLE_VALUE,
            NULL,
            (ULONG_PTR)0,
            1);
    if (S->iocp == NULL) {
        free(S);
        return NULL;
    }
    znT_inittimerstate(&S->TS);
    S->accepts = NULL;
    S->tcps = NULL;
    S->udps = NULL;
    S->requests = NULL;
    S->closing = 0;
    return S;
}

ZN_API void zn_close(zn_State *S) {
    zn_Accept *accept = S->accepts;
    zn_Tcp *tcp       = S->tcps;
    zn_Udp *udp       = S->udps;
    /* 0. doesn't allow create new objects */
    S->closing = 1;
    /* 1. remove timers */
    znT_cleartimers(&S->TS);
    /* 2. cancel all operations */
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
    /* 3. wait for uncompleted operations */
    while (S->requests)
        zn_run(S, ZN_RUN_ONCE);
    /* 4. clear resources and exit */
    CloseHandle(S->iocp);
    free(S);
}

ZN_API unsigned zn_time(void) {
    DWORD dwTick = GetTickCount();
    return (unsigned)dwTick;
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_PostState *ps = (zn_PostState*)malloc(sizeof(zn_PostState));
    if (ps == NULL) return 0;
    ps->handler = cb;
    ps->ud = ud;
    PostQueuedCompletionStatus(S->iocp, 0, 0, (LPOVERLAPPED)ps);
    return 1;
}

static int znS_checknext(zn_State *S) {
    return S->TS.timers != NULL
        || S->requests != NULL; /* still have operations? */
}

static int zn_poll(zn_State *S, int check) {
    DWORD dwBytes = 0;
    ULONG_PTR upComKey = (ULONG_PTR)0;
    LPOVERLAPPED pOverlapped = NULL;
    zn_Request *req;
    BOOL bRet;

    znT_updatetimer(&S->TS, zn_time());
    bRet = GetQueuedCompletionStatus(S->iocp,
            &dwBytes,
            &upComKey,
            &pOverlapped,
            check ? 0 : znT_getnexttime(&S->TS, zn_time()));
    znT_updatetimer(&S->TS, zn_time());
    if (!bRet && !pOverlapped) /* time out */
        return znS_checknext(S);

    if (upComKey == 0) {
        zn_PostState *ps = (zn_PostState*)pOverlapped;
        if (ps->handler)
            ps->handler(ps->ud, S);
        free(ps);
        return znS_checknext(S);
    }

    req = (zn_Request*)pOverlapped;
    znL_remove(req);
    znL_init(req); /* for debug purpose */
    switch (req->type) {
        case ZN_TACCEPT:   zn_onaccept(req->u.accept, bRet); break;
        case ZN_TRECV:     zn_onrecv(req->u.tcp, bRet, dwBytes); break;
        case ZN_TSEND:     zn_onsend(req->u.tcp, bRet, dwBytes); break;
        case ZN_TCONNECT:  zn_onconnect(req->u.tcp, bRet); break;
        case ZN_TRECVFROM: zn_onrecvfrom(req->u.udp, bRet, dwBytes); break;
        case ZN_TSENDTO: /* do not have this operation */
        default: ;
    }

    return znS_checknext(S);
}


#elif defined(__linux__) /* epoll implementations */

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#define ZN_MIN_POSTS  256
#define ZN_MAX_EVENTS 4096

#define zn_reqtype(t)    ((t)&0x7)
#define zn_checkop(OP,t) ((t)&ZN_OP_##OP)

typedef enum zn_RequestType {
    ZN_TPOST,
    ZN_TACCEPT,
    ZN_TTCP,
    ZN_TUDP,
} zn_RequestType;

typedef enum zn_RequestOp {
    ZN_OP_ACCEPT  = (1 << 3),
    ZN_OP_CONNECT = (1 << 4),
    ZN_OP_SEND    = (1 << 5),
    ZN_OP_RECV    = (1 << 6),
    ZN_OP_URECV   = (1 << 7),
} zn_RequestOp;

typedef struct zn_Request {
    struct epoll_event event;
    struct zn_Request *next;
    struct zn_Request **pprev;
    union {
        zn_Accept* accept;
        zn_Tcp* tcp;
        zn_Udp* udp;
    } u;
    int type;
} zn_Request;

typedef struct zn_PostState {
    struct zn_PostState *next;
    struct zn_PostState **pprev;
    zn_PostHandler *handler;
    void *ud;
} zn_PostState;

typedef struct zn_PostQueue {
    pthread_mutex_t lock;
    zn_PostState *posts;
} zn_PostQueue;

struct zn_State {
    int epoll;
    int closing;
    zn_TimerState TS;
    zn_Accept *accepts;
    zn_Tcp *tcps;
    zn_Udp *udps;
    zn_Request *requests;
    struct epoll_event events[ZN_MAX_EVENTS];
    zn_PostQueue messages;
};

/* post queue */

static void znP_init(zn_PostQueue *pa) {
    pthread_mutex_init(&pa->lock, NULL);
    pa->posts = NULL;
}

static int znP_add(zn_PostQueue *pa, zn_PostState *ps) {
    pthread_mutex_lock(&pa->lock);
    znL_insert(&pa->posts, ps);
    pthread_mutex_unlock(&pa->lock);
    return 1;
}

static void znP_process(zn_State *S, zn_PostQueue *pa) {
    zn_PostState *ps = pa->posts;
    if (ps == NULL) return;
    pthread_mutex_lock(&pa->lock);
    pa->posts = NULL;
    pthread_mutex_unlock(&pa->lock);
    while (ps) {
        zn_PostState *next = ps->next;
        if (ps->handler)
            ps->handler(ps->ud, S);
        free(ps);
        ps = next;
    }
}

/* tcp */

typedef struct zn_DataBuffer {
    size_t len;
    char  *buff;
} zn_DataBuffer;

struct zn_Tcp {
    zn_Tcp *next;
    zn_Tcp **pprev;
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    int fd;
    zn_Request request;
    zn_PeerInfo info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned short port) {
    strcpy(tcp->info.addr, addr);
    tcp->info.port = port;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    zn_Tcp *tcp;
    if (S->closing
            || (tcp = (zn_Tcp*)malloc(sizeof(zn_Tcp))) == NULL)
        return NULL;
    memset(tcp, 0, sizeof(*tcp));
    tcp->S = S;
    tcp->fd = -1;
    tcp->request.event.data.ptr = &tcp->request;
    tcp->request.u.tcp = tcp;
    tcp->request.type = ZN_TTCP;
    znL_insert(&S->tcps, tcp);
    return tcp;
}

ZN_API void zn_closetcp(zn_Tcp *tcp) {
    if (tcp->fd != -1) {
        close(tcp->fd);
        tcp->fd = -1;
    }
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    znL_remove(tcp);
    free(tcp);
}

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info) {
    *info = tcp->info;
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned short port, zn_ConnectHandler *cb, void *ud) {
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

    tcp->request.event.events = EPOLLIN;
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_ADD,
                fd, &tcp->request.event) != 0) {
        close(fd);
        return ZN_EPOLL;
    }

    znL_insert(&tcp->S->requests, &tcp->request);
    tcp->request.type |= ZN_OP_CONNECT;
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

    tcp->request.event.events |= EPOLLOUT;
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_MOD,
                tcp->fd, &tcp->request.event) != 0) {
        close(tcp->fd);
        tcp->fd = -1;
        return ZN_EPOLL;
    }

    znL_insert(&tcp->S->requests, &tcp->request);
    tcp->request.type |= ZN_OP_SEND;
    tcp->send_buffer.buff = (char*)buff;
    tcp->send_buffer.len = len;
    tcp->send_handler = cb;
    tcp->send_ud = ud;
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    if (tcp->S == NULL)            return ZN_ESTATE;
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;

    tcp->request.event.events |= EPOLLIN;
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_MOD,
                tcp->fd, &tcp->request.event) != 0) {
        close(tcp->fd);
        tcp->fd = -1;
        return ZN_EPOLL;
    }

    znL_insert(&tcp->S->requests, &tcp->request);
    tcp->request.type |= ZN_OP_RECV;
    tcp->recv_buffer.buff = buff;
    tcp->recv_buffer.len = len;
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int success) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    tcp->connect_handler = NULL;
    if (!success) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
    }
    else {
        int flag = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                (char*)&flag, sizeof(flag)) != 0)
        { /* XXX */ }
        cb(tcp->connect_ud, tcp, ZN_OK);
    }
}

static void zn_onsend(zn_Tcp *tcp, int bSuccess) {
    int bytes = 0;
    zn_SendHandler *cb = tcp->send_handler;
    assert(tcp->send_handler);
    tcp->send_handler = NULL;
    if (tcp->S == NULL || tcp->fd == -1) {
        assert(tcp->fd == -1);
        /* cb(tcp->send_ud, tcp, ZN_ECLOSED, dwBytes); */
        if (tcp->recv_handler == NULL) {
            znL_remove(tcp);
            free(tcp);
        }
    }
    else if (!bSuccess)
        cb(tcp->send_ud, tcp, ZN_ERROR, bytes);
    else
        cb(tcp->send_ud, tcp, ZN_OK, bytes);
}

static void zn_onrecv(zn_Tcp *tcp, int success) {
    int bytes = 0;
    zn_RecvHandler *cb = tcp->recv_handler;
    assert(tcp->recv_handler);
    tcp->recv_handler = NULL;
    if (bytes == 0 || tcp->fd == -1) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ECLOSED, bytes);
    }
    else if (!success) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_EHANGUP, bytes);
    }
    else 
        cb(tcp->recv_ud, tcp, ZN_OK, bytes);
}

/* accept */

struct zn_Accept {
    zn_Accept *next;
    zn_Accept **pprev;
    zn_State *S;
    void *ud;
    zn_AcceptHandler *handler;
    int fd;
    int client;
    zn_Request request;
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    zn_Accept *accept;
    if (S->closing
            || (accept = (zn_Accept*)malloc(sizeof(zn_Accept))) == NULL)
        return NULL;
    memset(accept, 0, sizeof(*accept));
    accept->S = S;
    accept->fd = -1;
    accept->client = -1;
    accept->request.event.data.ptr = &accept->request;
    accept->request.u.accept = accept;
    accept->request.type = ZN_TACCEPT;
    znL_insert(&S->accepts, accept);
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    zn_closeaccept(accept);
    if (accept->handler != NULL) {
        accept->S = NULL; /* mark dead */
        return;
    }
    znL_remove(accept);
    free(accept);
}

ZN_API void zn_closeaccept(zn_Accept *accept) {
    if (accept->fd != -1) {
        close(accept->fd);
        accept->fd = -1;
    }
    if (accept->fd != -1) {
        close(accept->client);
        accept->client = -1;
    }
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned short port) {
    struct sockaddr_in sock_addr;
    int reuse_addr = 1;
    int fd;
    if (accept->fd != -1)        return ZN_ESTATE;
    if (accept->handler != NULL) return ZN_EBUSY;

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return ZN_ESOCKET;

    if (setsockopt(accept->fd, SOL_SOCKET, SO_REUSEADDR,
                (char*)&reuse_addr, sizeof(reuse_addr)) != 0)
    { /* XXX */ }

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_addr.s_addr = inet_addr(addr);
    sock_addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sock_addr,
                sizeof(sock_addr)) != 0) {
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

ZN_API int zn_accept(zn_Accept *a, zn_AcceptHandler *cb, void *ud) {
    int client;
    int ret = 0;
    if (a->fd == -1) return ZN_ESTATE;
    if (cb == NULL)  return ZN_EPARAM;

    if ((client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return ZN_ESOCKET;
    // ret = accept(a->fd, &client);
    if (ret == -1 && errno != EINPROGRESS)
    {
        close(client);
        return ZN_ERROR;
    }

    znL_insert(&a->S->requests, &a->request);
    a->request.type |= ZN_OP_ACCEPT;
    a->handler = cb;
    a->ud = ud;
    a->client = client;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *accept, int success) {
    zn_AcceptHandler *cb = accept->handler;
    zn_Tcp *tcp;
    struct sockaddr *paddr2 = NULL;

    if (accept->S == NULL) {
        /* cb(accept->ud, accept, ZN_ECLOSED, NULL); */
        znL_remove(accept);
        free(accept);
        return;
    }

    accept->handler = NULL;
    if (!success) {
        /* zn_closeaccept(accept); */
        cb(accept->ud, accept, ZN_ERROR, NULL);
        return;
    }

    tcp = zn_newtcp(accept->S);
    tcp->fd = accept->client;
    accept->client = -1;

    zn_setinfo(tcp, 
            inet_ntoa(((struct sockaddr_in*)paddr2)->sin_addr),
            ntohs(((struct sockaddr_in*)paddr2)->sin_port));
    cb(accept->ud, accept, ZN_OK, tcp);
}

/* udp */

struct zn_Udp {
    zn_Udp *next;
    zn_Udp **pprev;
    zn_State *S;
    void *recv_ud; zn_URecvHandler *recv_handler;
    int fd;
    zn_Request request;
    zn_DataBuffer recv_buffer;
    struct sockaddr_in recvFrom;
    int recvFromLen;
};

static int zn_initudp(zn_Udp *udp, const char *addr, unsigned short port) {
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

    udp->fd = fd;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned short port) {
    zn_Udp *udp;
    if (S->closing
            || (udp = (zn_Udp*)malloc(sizeof(zn_Udp))) == NULL)
        return NULL;
    memset(udp, 0, sizeof(*udp));
    udp->S = S;
    udp->fd = -1;
    udp->request.u.udp = udp;
    udp->request.type = ZN_TUDP;
    if (!zn_initudp(udp, addr, port)) {
        free(udp);
        return NULL;
    }
    znL_insert(&S->udps, udp);
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    close(udp->fd);
    znL_remove(udp);
    free(udp);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned short port) {
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

ZN_API int zn_recvfrom(zn_Udp *udp, char *buff, unsigned len, zn_URecvHandler *cb, void *ud) {
    if (udp->fd == -1)          return ZN_ESTATE;
    if (udp->recv_handler)      return ZN_EBUSY;
    if (len == 0 || cb == NULL) return ZN_EPARAM;

    /*
    memset(&udp->recvFrom, 0, sizeof(udp->recvFrom));
    udp->recvFromLen = sizeof(udp->recvFrom);
    if ((WSARecvFrom(udp->socket, &udp->recv_buffer, 1, &dwRecv, &dwFlag,
                (struct sockaddr*)&udp->recvFrom, &udp->recvFromLen,
                &udp->request.overlapped, NULL) != 0)
            && WSAGetLastError() != WSA_IO_PENDING)
    {
        udp->recv_buffer.buf = NULL;
        udp->recv_buffer.len = 0;
        return ZN_ERROR;
    }
    */

    znL_insert(&udp->S->requests, &udp->request);
    udp->recv_buffer.buff = buff;
    udp->recv_buffer.len = len;
    udp->request.type |= ZN_OP_URECV;
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, int success) {
    int bytes = 0;
    zn_URecvHandler *cb = udp->recv_handler;
    if (udp->S == NULL) {
        /* cb(udp->recv_ud, udp, ZN_ERROR, dwBytes, "0.0.0.0", 0); */
        znL_remove(udp);
        free(udp);
        return;
    }
    if (!cb) return;
    udp->recv_handler = NULL;
    if (success && bytes > 0)
        cb(udp->recv_ud, udp, ZN_OK, bytes,
                inet_ntoa(((struct sockaddr_in*)&udp->recvFrom)->sin_addr),
                ntohs(udp->recvFrom.sin_port));
    else cb(udp->recv_ud, udp, ZN_ERROR, bytes,
            "0.0.0.0", 0);
}

/* poll */

ZN_API void zn_initialize(void) { }
ZN_API void zn_deinitialize(void) { }

ZN_API zn_State *zn_newstate(void) {
    const int ZN_EVENTS_IGNORED = 1024;
    zn_State *S = (zn_State*)malloc(sizeof(zn_State));
    if (S == NULL) return NULL;
    S->epoll = epoll_create(ZN_EVENTS_IGNORED);
    if (S->epoll == -1) {
        free(S);
        return NULL;
    }
    znT_inittimerstate(&S->TS);
    S->accepts = NULL;
    S->tcps = NULL;
    S->udps = NULL;
    S->requests = NULL;
    S->closing = 0;
    znP_init(&S->messages);
    return S;
}

ZN_API void zn_close(zn_State *S) {
    zn_Accept *accept = S->accepts;
    zn_Tcp *tcp       = S->tcps;
    zn_Udp *udp       = S->udps;
    /* 0. doesn't allow create new objects */
    S->closing = 1;
    /* 1. remove timers */
    znT_cleartimers(&S->TS);
    /* 2. cancel all operations */
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
    /* 3. wait for uncompleted operations */
    while (S->requests) {
        zn_run(S, ZN_RUN_ONCE);
    }
    /* 4. clear resources and exit */
    close(S->epoll);
    free(S);
}

ZN_API unsigned zn_time(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1)
        return 0;
    return (unsigned)(tv.tv_sec*1000+tv.tv_usec/1000);
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_PostState *ps = (zn_PostState*)malloc(sizeof(zn_PostState));
    if (ps == NULL) return 0;
    ps->handler = cb;
    ps->ud = ud;
    return znP_add(&S->messages, ps);
}

static int znS_checknext(zn_State *S) {
    return S->TS.timers != NULL
        || S->requests != NULL; /* still have operations? */
}

static void zn_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_Request *req = (zn_Request*)evt->data.ptr;
    int eventmask = evt->events;
    znL_remove(req);
    znL_init(req); /* for debug purpose */
    switch (req->type) {
    case ZN_TPOST:  
        znP_process(S, &S->messages);
        break;
    case ZN_TACCEPT:
        if (!zn_checkop(ACCEPT, req->type)) break;
        if ((eventmask & EPOLLIN) != 0)
            zn_onaccept(req->u.accept, 1);
        else if ((eventmask & (EPOLLERR|EPOLLOUT)) != 0)
            zn_onaccept(req->u.accept, 0);
        break;
    case ZN_TTCP:
        if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
            if (zn_checkop(CONNECT, req->type))
                ;
            if (zn_checkop(SEND, req->type))
                ;
            if (zn_checkop(RECV, req->type))
                ;
        }
        else if ((eventmask & EPOLLIN) != 0 && zn_checkop(RECV, req->type))
            ;
        else if ((eventmask & EPOLLOUT) != 0 && zn_checkop(SEND, req->type))
            ;
        break;
    case ZN_TUDP:
        if ((eventmask & (EPOLLERR|EPOLLHUP)) != 0) {
            if (zn_checkop(CONNECT, req->type))
                ;
            if (zn_checkop(SEND, req->type))
                ;
            if (zn_checkop(RECV, req->type))
                ;
        }
        else if ((eventmask & EPOLLIN) != 0 && zn_checkop(RECV, req->type))
            ;
        else if ((eventmask & EPOLLOUT) != 0 && zn_checkop(SEND, req->type))
            ;
        break;
    default: ;
    }
}

static int zn_poll(zn_State *S, int check) {
    int i, ret;
    unsigned current = zn_time();
    znT_updatetimer(&S->TS, current);
    ret = epoll_wait(S->epoll, S->events, ZN_MAX_EVENTS,
            check ? 0 : znT_getnexttime(&S->TS, current));
    if (ret == -1 && errno != EINTR) /* error out */
        return -ZN_EPOLL;
    znT_updatetimer(&S->TS, zn_time());
    for (i = 0; i < ret; ++i)
        zn_dispatch(S, &S->events[i]);
    return znS_checknext(S);
}


#else /* select implementations */


#endif /* System specified routines */


/* common routines */

ZN_API const char *zn_strerror (int err) {
    const char *msg = "Unknown error";
    switch (err) {
#define X(name, str) case ZN_##name: msg = str; break;
        ZN_ERRORS(X)
#undef  X
    }
    return msg;
}

ZN_API zn_Timer *zn_newtimer(zn_State *S, zn_TimerHandler *cb, void *ud) {
    zn_Timer *t;
    if (S->closing
            || (t = znT_newtimer(S, &S->TS)) == NULL)
        return NULL;
    t->S = S;
    t->ud = ud;
    t->handler = cb;
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
    znT_inserttimer(&timer->S->TS, timer);
}

ZN_API void zn_canceltimer(zn_Timer *timer) {
    znL_remove(timer);
    timer->time = ~(unsigned)0;
}

ZN_API int zn_run(zn_State *S, int mode) {
    int err;
    switch (mode) {
    case ZN_RUN_CHECK:
        return zn_poll(S, 1);
    case ZN_RUN_ONCE:
        return zn_poll(S, 0);
    case ZN_RUN_LOOP:
        while ((err = zn_poll(S, 0)) > 0)
            ;
        return err;
    }
    return -1;
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */
/* cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * cc: libs+='-lws2_32' output='znet.dll' */
/* linuxcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * linuxcc: output='znet.so' */
