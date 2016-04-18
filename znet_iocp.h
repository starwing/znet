#if !defined(znet_h) && !defined(ZN_USE_IOCP)
# include "znet.h"
#endif


#if defined(_WIN32) && defined(ZN_USE_IOCP) && !defined(znet_iocp_h)
#define znet_iocp_h

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>

#ifdef _MSC_VER
# pragma warning(disable: 4996) /* deprecated stuffs */
# pragma warning(disable: 4127) /* for do {} while(0) */
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
    zn_RequestType type;
} zn_Request;

typedef struct zn_Post {
    zn_State *S;
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

struct zn_State {
    ZN_STATE_FIELDS
    HANDLE iocp;
    CRITICAL_SECTION lock;
    OVERLAPPED_ENTRY entries[ZN_MAX_EVENTS];
};

/* utils */

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

/* tcp */

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

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned port) {
    strcpy(tcp->info.addr, addr);
    tcp->info.port = port;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    ZN_GETOBJECT(S, zn_Tcp, tcp);
    tcp->socket = INVALID_SOCKET;
    tcp->connect_request.type = ZN_TCONNECT;
    tcp->send_request.type = ZN_TSEND;
    tcp->recv_request.type = ZN_TRECV;
    return tcp;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    /* if a request is on-the-go, then the IOCP will give callback, we
     * delete object on that callback.
     * same as zn_delaccept() and zn_deludp() */
    if (tcp->recv_handler == NULL
            && tcp->send_handler == NULL
            && tcp->connect_handler == NULL)
        ZN_PUTOBJECT(tcp);
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
    remoteAddr.sin_port = htons((unsigned short)port);
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

    ++tcp->S->waitings;
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    return ZN_OK;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len, zn_SendHandler *cb, void *ud) {
    DWORD dwTemp1=0;
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

    ++tcp->S->waitings;
    tcp->send_handler = cb;
    tcp->send_ud = ud;
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    DWORD dwRecv = 0;
    DWORD dwFlag = 0;
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

    ++tcp->S->waitings;
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    return ZN_OK;
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
    if (!bSuccess) zn_closetcp(tcp);
    else znU_set_nodelay(tcp->socket);
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
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    zn_Request accept_request;
    SOCKET socket;
    SOCKET client;
    DWORD recv_length;
    char recv_buffer[(sizeof(SOCKADDR_IN)+16)*2];
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    ZN_GETOBJECT(S, zn_Accept, accept);
    accept->socket = INVALID_SOCKET;
    accept->client = INVALID_SOCKET;
    accept->accept_request.type = ZN_TACCEPT;
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    zn_closeaccept(accept);
    if (accept->accept_handler == NULL)
        ZN_PUTOBJECT(accept);
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
    SOCKET socket;
    if (accept->socket != INVALID_SOCKET) return ZN_ESTATE;
    if (accept->accept_handler != NULL)   return ZN_EBUSY;

    if ((socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                    NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET)
        return ZN_ESOCKET;

    znU_set_reuseaddr(accept->socket);
    memset(&sockAddr, 0, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = inet_addr(addr);
    sockAddr.sin_port = htons((unsigned short)port);
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
                &accept->recv_length, &accept->accept_request.overlapped)
            && WSAGetLastError() != ERROR_IO_PENDING)
    {
        closesocket(socket);
        return ZN_ERROR;
    }

    ++accept->S->waitings;
    accept->accept_handler = cb;
    accept->accept_ud = ud;
    accept->client = socket;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *accept, BOOL bSuccess) {
    static LPFN_GETACCEPTEXSOCKADDRS lpGetAcceptExSockaddrs = NULL;
    static GUID gid = WSAID_GETACCEPTEXSOCKADDRS;
    zn_AcceptHandler *cb = accept->accept_handler;
    zn_Tcp *tcp;
    struct sockaddr *paddr1 = NULL, *paddr2 = NULL;
    int tmp1 = 0, tmp2 = 0;
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
    znU_set_nodelay(accept->client);

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
                tcp->S->iocp, (ULONG_PTR)tcp, 1) == NULL)
    {
        closesocket(tcp->socket);
        ZN_PUTOBJECT(tcp);
        return;
    }

    zn_setinfo(tcp, 
            inet_ntoa(((struct sockaddr_in*)paddr2)->sin_addr),
            ntohs(((struct sockaddr_in*)paddr2)->sin_port));
    cb(accept->accept_ud, accept, ZN_OK, tcp);
}

/* udp */

struct zn_Udp {
    znL_entry(zn_Udp);
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
    sockAddr.sin_port = htons((unsigned short)port);

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

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    ZN_GETOBJECT(S, zn_Udp, udp);
    udp->socket = INVALID_SOCKET;
    udp->recv_request.type = ZN_TRECVFROM;
    if (!zn_initudp(udp, addr, port)) {
        ZN_PUTOBJECT(udp);
        return NULL;
    }
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    closesocket(udp->socket);
    if (udp->recv_handler == NULL)
        ZN_PUTOBJECT(udp);
}

ZN_API int zn_sendto(zn_Udp *udp, const char *buff, unsigned len, const char *addr, unsigned port) {
    SOCKADDR_IN dst;
    if (udp->socket == INVALID_SOCKET) return ZN_ESTATE;
    if (len == 0 || len >1200)         return ZN_EPARAM;

    memset(&dst, 0, sizeof(SOCKADDR_IN));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(addr);
    dst.sin_port = htons((unsigned short)port);
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

    ++udp->S->waitings;
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, BOOL bSuccess, DWORD dwBytes) {
    zn_RecvFromHandler *cb = udp->recv_handler;
    assert(udp->recv_handler);
    udp->recv_handler = NULL;
    if (udp->socket == INVALID_SOCKET) {
        ZN_PUTOBJECT(udp);
        return;
    }
    if (bSuccess && dwBytes > 0)
        cb(udp->recv_ud, udp, ZN_OK, dwBytes,
                inet_ntoa(((struct sockaddr_in*)&udp->recvFrom)->sin_addr),
                ntohs(udp->recvFrom.sin_port));
    else cb(udp->recv_ud, udp, ZN_ERROR, dwBytes,
            "0.0.0.0", 0);
}

/* poll */

static BOOL zn_initialized = FALSE;
static LARGE_INTEGER counterFreq;
static LARGE_INTEGER startTime;

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
        if (kernel32)
            pGetQueuedCompletionStatusEx = (LPGETQUEUEDCOMPLETIONSTATUSEX)
                GetProcAddress(kernel32, "GetQueuedCompletionStatusEx");
        zn_initialized = TRUE;
    }
}

ZN_API void zn_deinitialize(void) {
    if (zn_initialized) {
        WSACleanup();
        zn_initialized = FALSE;
    }
}

ZN_API zn_Time zn_time(void) {
    LARGE_INTEGER current;
    QueryPerformanceCounter(&current);
    return (zn_Time)((current.QuadPart - startTime.QuadPart) * 1000
            / counterFreq.LowPart);
}

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    zn_Post *post;
    EnterCriticalSection(&S->lock);
    post = (zn_Post*)znP_getobject(&S->posts);
    LeaveCriticalSection(&S->lock);
    post->S = S;
    post->handler = cb;
    post->ud = ud;
    if (!PostQueuedCompletionStatus(S->iocp, 0, 0, (LPOVERLAPPED)post)) {
        EnterCriticalSection(&S->lock);
        znP_putobject(&S->posts, post);
        LeaveCriticalSection(&S->lock);
        return ZN_ERROR;
    }
    return ZN_OK;
}

static int znS_init(zn_State *S) {
    if (counterFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&counterFreq);
        QueryPerformanceCounter(&startTime);
        assert(counterFreq.HighPart == 0);
    }
    S->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
            NULL, (ULONG_PTR)0, 1);
    InitializeCriticalSection(&S->lock);
    return S->iocp != NULL;
}

static void znS_close(zn_State *S) {
    /* use IOCP to wait all closed but not deleted object,
     * and delete them. */
    while (S->waitings != 0) {
        assert(S->waitings > 0);
        zn_run(S, ZN_RUN_ONCE);
    }
    CloseHandle(S->iocp);
    DeleteCriticalSection(&S->lock);
}

static void znS_dispatch(zn_State *S, BOOL bRet, LPOVERLAPPED_ENTRY pEntry) {
    zn_Request *req = (zn_Request*)pEntry->lpOverlapped;
    ULONG_PTR upComKey = pEntry->lpCompletionKey;
    DWORD dwBytes = pEntry->dwNumberOfBytesTransferred;
    --S->waitings;
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

static int znS_poll(zn_State *S, int checkonly) {
    DWORD timeout = 0;
    zn_Time current;
    BOOL bRet;

    S->status = ZN_STATUS_IN_RUN;
    znT_updatetimers(S, current = zn_time());
    if (!checkonly) {
        zn_Time ms = znT_gettimeout(S, current);
        timeout = ms >= INFINITE ? INFINITE : (DWORD)ms;
    }
    if (pGetQueuedCompletionStatusEx) {
        ULONG i, count;
        bRet = pGetQueuedCompletionStatusEx(S->iocp,
                S->entries, ZN_MAX_EVENTS, &count, timeout, FALSE);
        if (!bRet) goto out; /* time out */
        for (i = 0; i < count; ++i) {
            DWORD transfer;
            BOOL result;
            if (S->entries[i].lpCompletionKey == 0) {
                zn_Post *post = (zn_Post*)S->entries[i].lpOverlapped;
                if (post->handler)
                    post->handler(post->ud, post->S);
                znP_putobject(&S->posts, post);
                continue;
            }
            result = GetOverlappedResult(S->iocp,
                    S->entries[i].lpOverlapped, &transfer, FALSE);
            znS_dispatch(S, result, &S->entries[i]);
        }
    }
    else {
        OVERLAPPED_ENTRY entry;
        bRet = GetQueuedCompletionStatus(S->iocp,
                &entry.dwNumberOfBytesTransferred,
                &entry.lpCompletionKey,
                &entry.lpOverlapped, timeout);
        znT_updatetimers(S, zn_time());
        if (!bRet && !entry.lpOverlapped) /* time out */
            goto out;
        znS_dispatch(S, bRet, &entry);
    }

out:
    if (S->status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_READY; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->status = ZN_STATUS_READY;
    return znT_hastimers(S) || S->waitings != 0;
}


#endif /* ZN_USE_IOCP */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-pthread -lrt' output='znet.so' */
