#ifndef zn_addrinfo_h
#define zn_addrinfo_h


#include "znet.h"

ZN_NS_BEGIN

#define ZN_TCP     0x00
#define ZN_ACCEPT  0x01
#define ZN_UDP     0x02
#define ZN_IPV4    0x100
#define ZN_IPV6    0x200

typedef void zn_AddrInfoHandler(void *ud, unsigned err, unsigned count, zn_PeerInfo *infos);

ZN_API const char *zn_aierror (unsigned err);

ZN_API int zn_getaddrinfo (zn_State *S, const char *node, const char *service, int flags,
                           zn_AddrInfoHandler *h, void *ud);

ZN_API void zn_closeaddrinfo (zn_State *S);


ZN_NS_END

#endif /* zn_addrinfo_h */


#ifndef zn_list_h
#define zn_list_h

#define znL_entry(T) T *next; T **pprev
#define znL_head(T)  struct T##_hlist { znL_entry(T); }

#define znL_init(n)                                       do { \
    (n)->pprev = &(n)->next;                                   \
    (n)->next = NULL;                                        } while (0)

#define znL_insert(h, n)                                  do { \
    (n)->pprev = (h);                                          \
    (n)->next = *(h);                                          \
    if (*(h) != NULL)                                          \
        (*(h))->pprev = &(n)->next;                            \
    *(h) = (n);                                              } while (0)

#define znL_remove(n)                                     do { \
    if ((n)->next != NULL)                                     \
        (n)->next->pprev = (n)->pprev;                         \
    *(n)->pprev = (n)->next;                                 } while (0)

#define znL_apply(type, h, stmt)                          do { \
    type *cur = (type*)*(h);                                   \
    *(h) = NULL;                                               \
    while (cur)                                                \
    { type *next_ = cur->next; stmt; cur = next_; }          } while (0)

#define znQ_entry(T) T* next
#define znQ_type(T)  struct { T *first; T **plast; }

#define znQ_init(h)                                       do { \
    (h)->first = NULL;                                         \
    (h)->plast = &(h)->first;                                } while (0)

#define znQ_enqueue(h, n)                                 do { \
    *(h)->plast = (n);                                         \
    (h)->plast = &(n)->next;                                   \
    (n)->next = NULL;                                        } while (0)

#define znQ_dequeue(h, pn)                                do { \
    if (((pn) = (h)->first) != NULL) {                         \
        (h)->first = (h)->first->next;                         \
        if ((h)->plast == &(pn)->next)                         \
            (h)->plast = &(h)->first; }                      } while (0)

#define znQ_apply(type, h, stmt)                          do { \
    type *cur = (type*)(h);                                    \
    while (cur)                                                \
    { type *next_ = cur->next; stmt; cur = next_; }          } while (0)

#endif /* zn_list_h */


#if defined(ZN_IMPLEMENTATION) && !defined(zn_implemented)
#define zn_implemented

#include <stdlib.h>

ZN_NS_BEGIN


typedef struct znA_AddrRequest {
    znQ_entry(struct znA_AddrRequest);
    zn_AddrInfoHandler *h;
    void        *ud;
    zn_State    *S;
    int          flags;
    int          ret;
    unsigned     count;
    zn_PeerInfo *peers;
    char        *node;
    char        *service;
} znA_AddrRequest;

static int                       znA_initalize = 0;
static znA_AddrRequest          *znA_current;
static znQ_type(znA_AddrRequest) znA_queue;

static void znA_makepeers(znA_AddrRequest *req, void *info);
static void znA_makehints(znA_AddrRequest *req, void *info);

static znA_AddrRequest *znA_makereq(zn_State *S, const char *node, const char *service, int flags, zn_AddrInfoHandler *h, void *ud) {
    size_t nodelen = (node    ? strlen(node)    : 0);
    size_t svrlen  = (service ? strlen(service) : 0);
    znA_AddrRequest *req = (znA_AddrRequest*)malloc(sizeof(znA_AddrRequest)
            + nodelen + svrlen + 2);
    if (req == NULL) return NULL;
    memset(req, 0, sizeof(*req));
    req->ud      = ud;
    req->h       = h;
    req->S       = S;
    req->flags   = flags & 0xFFFF;
    req->node    = (char*)(req + 1);
    req->service = req->node + nodelen + 1;
    req->node    = node    ? strcpy(req->node,    node   ) : NULL;
    req->service = service ? strcpy(req->service, service) : NULL;
    zn_retain(S);
    return req;
}

static void znA_callback(void *ud, zn_State *S) {
    znA_AddrRequest *req = (znA_AddrRequest*)ud;
    if (req->count == 0 && req->ret == 0)
        req->ret = ZN_ERROR;
    if (req->h) req->h(req->ud, req->ret, req->count, req->peers);
    free(req->peers);
    free(req);
    zn_release(S);
}

static void znA_clearreq(zn_State *S) {
    znA_AddrRequest *req = znA_queue.first;
    znQ_init(&znA_queue);
    while (req != NULL) {
        znA_AddrRequest *next = req->next;
        if (req->S != S && S != NULL)
            znQ_enqueue(&znA_queue, req);
        else {
            req->ret = ZN_ERROR;
            zn_post(req->S, znA_callback, req);
        }
        req = next;
    }
}

static znA_AddrRequest *znA_fetchreq(void) {
    znA_AddrRequest *req = NULL;
    if (znA_current)
        zn_post(znA_current->S, znA_callback, znA_current);
    znA_current = NULL;
    znQ_dequeue(&znA_queue, req);
    return znA_current = req;
}

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif /* WIN32_LEAN_AND_MEAN */
#include <Windows.h>
#include <WinSock2.h>

#define zn_AddrInfo struct addrinfo

static CRITICAL_SECTION znA_lock;
static HANDLE           znA_event;
static HANDLE           znA_thread;

static const char *znA_ntop(int af, const void *src, char *dst, socklen_t size) {
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

static void znA_processreq(znA_AddrRequest *req) {
    zn_AddrInfo hints, *info; /* XXX use GetAddrInfoW on Windows */
    znA_makehints(req, &hints);
    req->ret = getaddrinfo(req->node, req->service, &hints, &info);
    if (req->ret == 0 && info) {
        znA_makepeers(req, info);
        freeaddrinfo(info);
    }
}

static DWORD WINAPI znA_worker(void *param) {
    for (;;) {
        znA_AddrRequest *req;
        if (WaitForSingleObject(znA_event, INFINITE) != WAIT_OBJECT_0)
            return 1;

        EnterCriticalSection(&znA_lock);
        req = znA_fetchreq();
        LeaveCriticalSection(&znA_lock);

        if (req != NULL) znA_processreq(req);
    }
    return 0;
}

ZN_API int zn_getaddrinfo(zn_State *S, const char *node, const char *service, int flags, zn_AddrInfoHandler *h, void *ud) {
    znA_AddrRequest *req = znA_makereq(S, node, service, flags, h, ud);
    if (req == NULL) return ZN_ERROR;
    EnterCriticalSection(&znA_lock);
    if (!znA_initalize) {
        znA_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (znA_event != INVALID_HANDLE_VALUE) {
            znA_thread = CreateThread(NULL, 0, znA_worker, NULL, 0, NULL);
            if (znA_thread != INVALID_HANDLE_VALUE)
                znA_initalize = 1;
            else
                CloseHandle(znA_event);
        }
        znQ_init(&znA_queue);
    }
    znQ_enqueue(&znA_queue, req);
    SetEvent(&znA_event);
    LeaveCriticalSection(&znA_lock);
    return ZN_OK;
}

ZN_API void zn_closeaddrinfo(zn_State *S) {
    EnterCriticalSection(&znA_lock);
    if (!znA_initalize) return;
    znA_clearreq(S);
    if (S == NULL) {
        TerminateThread(znA_thread, 0);
        CloseHandle(znA_event);
        CloseHandle(znA_thread);
        znA_initalize = 0;
    }
    LeaveCriticalSection(&znA_lock);
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>

#define znA_ntop    inet_ntop
#define zn_AddrInfo struct addrinfo

static pthread_mutex_t znA_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  znA_event = PTHREAD_COND_INITIALIZER;
static pthread_t       znA_thread;

static void *znA_worker(void *param) {
    for (;;) {
        zn_AddrInfo hints, *info;
        znA_AddrRequest *req;
        pthread_mutex_lock(&znA_lock);
        while ((req = znA_fetchreq()) == NULL)
            pthread_cond_wait(&znA_event, &znA_lock);
        pthread_mutex_unlock(&znA_lock);
        znA_makehints(req, &hints);
        req->ret = getaddrinfo(req->node, req->service, &hints, &info);
        if (req->ret == 0 && info) {
            znA_makepeers(req, info);
            freeaddrinfo(info);
        }
    }
    return NULL;
}

ZN_API int zn_getaddrinfo(zn_State *S, const char *node, const char *service, int flags, zn_AddrInfoHandler *h, void *ud) {
    znA_AddrRequest *req = znA_makereq(S, node, service, flags, h, ud);
    if (req == NULL) return ZN_ERROR;
    pthread_mutex_lock(&znA_lock);
    if (!znA_initalize && pthread_create(&znA_thread, NULL, znA_worker, NULL) == 0) {
        znQ_init(&znA_queue);
        znA_initalize = 1;
    }
    znQ_enqueue(&znA_queue, req);
    pthread_cond_signal(&znA_event);
    pthread_mutex_unlock(&znA_lock);
    return ZN_OK;
}

ZN_API void zn_closeaddrinfo(zn_State *S) {
    pthread_mutex_lock(&znA_lock);
    if (!znA_initalize) return;
    znA_clearreq(S);
    if (S == NULL) {
        pthread_cancel(znA_thread);
        pthread_join(znA_thread, NULL);
        znA_initalize = 0;
    }
    pthread_mutex_unlock(&znA_lock);
}

#endif

ZN_API const char *zn_aierror(unsigned err) { return gai_strerror(err); }

static void znA_makepeers(znA_AddrRequest *req, void *info) {
    zn_AddrInfo *p;
    size_t count = 0;
    for (p = (zn_AddrInfo*)info; p != NULL; p = p->ai_next)
        ++count;
    req->peers = (zn_PeerInfo*)malloc(count * sizeof(zn_PeerInfo));
    if (req->peers == NULL) return;
    for (p = (zn_AddrInfo*)info; p != NULL; p = p->ai_next) {
        zn_PeerInfo *peer = &req->peers[req->count];
        sa_family_t family = p->ai_family;
        if (family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in*)p->ai_addr;
            znA_ntop(family, &addr->sin_addr, peer->addr, ZN_MAX_ADDRLEN);
            peer->port = ntohs(addr->sin_port);
            ++req->count;
        }
        else if (family == AF_INET6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6*)p->ai_addr;
            znA_ntop(family, &addr->sin6_addr, peer->addr, ZN_MAX_ADDRLEN);
            peer->port = ntohs(addr->sin6_port);
            ++req->count;
        }
    }
}

static void znA_makehints(znA_AddrRequest *req, void *info) {
    zn_AddrInfo *hints = (zn_AddrInfo*)info;
    memset(hints, 0, sizeof(*hints));
    hints->ai_family = req->flags & ZN_IPV6 ? AF_INET6 :
                       req->flags & ZN_IPV4 ? AF_INET  : AF_UNSPEC;
    switch (req->flags & 0xFF) {
    default:
    case ZN_ACCEPT:
        hints->ai_flags = AI_PASSIVE;
        /* FALLTHROUGH */
    case ZN_TCP:
        hints->ai_socktype = SOCK_STREAM;
        hints->ai_protocol = IPPROTO_TCP;
        break;
    case ZN_UDP:
        hints->ai_socktype = SOCK_DGRAM;
        hints->ai_protocol = IPPROTO_UDP;
        break;
    }
}


ZN_NS_END

#endif /* ZN_IMPLEMENTATION */

/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='zn_addrinfo.dll'
   unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc' output='zn_addrinfo.so' */

