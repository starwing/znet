#if !defined(znet_h) && !defined(ZN_USE_KQUEUE)
# include "znet.h"
#endif


#if defined(ZN_USE_KQUEUE) && !defined(znet_kqueue_h)
#define znet_kqueue_h 

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
#include <sys/event.h>

# ifdef __APPLE__ /* apple don't have spinlock :( */
#   include <mach/mach.h>
#   include <mach/mach_time.h>
#   define pthread_spinlock_t  pthread_mutex_t
#   define pthread_spin_init   pthread_mutex_init
#   define pthread_spin_lock   pthread_mutex_lock
#   define pthread_spin_unlock pthread_mutex_unlock
# endif /* __APPLE__ */

#define ZN_MAX_RESULT_LOOPS 100

typedef struct zn_DataBuffer {
    size_t len;
    char  *buff;
} zn_DataBuffer;

typedef struct zn_Post {
    znL_entry(struct zn_Post);
    struct zn_Post *next_post;
    zn_State *S;
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

typedef struct zn_Result {
    znQ_entry(struct zn_Result);
    zn_Tcp *tcp;
    int err;
} zn_Result;

struct zn_State {
    ZN_STATE_FIELDS
    pthread_spinlock_t post_lock;
    zn_Post *first_post;
    zn_Post **last_post;
    znQ_type(zn_Result) results;
    int kqueue;
    int sockpairs[2];
    struct kevent events[ZN_MAX_EVENTS];
};

/* utils */

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

static int znU_register(int kqueue, int fd, int et, void *data) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, et|EV_ADD|EV_ENABLE, 0, 0, data);
    EV_SET(&kev[1], fd, EVFILT_WRITE, et|EV_ADD|EV_ENABLE, 0, 0, data);
    return kevent(kqueue, kev, 2, NULL, 0, NULL) == 0;
}

static int znU_unregister(int kqueue, int fd) {
    struct kevent kev[2];
    EV_SET(&kev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&kev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    return kevent(kqueue, kev, 2, NULL, 0, NULL) == 0;
}

/* post queue */

static void znP_init(zn_State *S) {
    pthread_spin_init(&S->post_lock, 0);
    S->first_post = NULL;
    S->last_post = &S->first_post;
}

static void znP_process(zn_State *S) {
    zn_Post *post;
    pthread_spin_lock(&S->post_lock);
    post = S->first_post;
    S->first_post = NULL;
    S->last_post = &S->first_post;
    pthread_spin_unlock(&S->post_lock);
    while (post) {
        zn_Post *next = post->next_post;
        if (post->handler)
            post->handler(post->ud, post->S);
        ZN_PUTOBJECT(post);
        post = next;
    }
}

/* result queue */

static void zn_onresult(zn_Result *result);
static void znR_init(zn_State *S) { znQ_init(&S->results); }

static void znR_add(zn_State *S, int err, zn_Result *result) {
    result->err = err;
    znQ_enqueue(&S->results, result);
}

static void znR_process(zn_State *S) {
    zn_Result *results;
    int count = 0;
    while ((results = S->results.first) != NULL
            && ++count <= ZN_MAX_RESULT_LOOPS) {
        znQ_init(&S->results);
        while (results) {
            zn_Result *next = results->next;
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

struct zn_Tcp {
    znL_entry(zn_Tcp);
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    int fd;
    unsigned can_read  : 1;
    unsigned can_write : 1;
    zn_SocketInfo info;
    zn_Result send_result;
    zn_Result recv_result;
    zn_PeerInfo peer_info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned port)
{ strcpy(tcp->peer_info.addr, addr); tcp->peer_info.port = port; }

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)
{ *info = tcp->peer_info; }

static zn_Tcp *zn_tcpfromfd(zn_State *S, int fd, struct sockaddr_in *remote_addr) {
    zn_Tcp *tcp = zn_newtcp(S);
    tcp->fd = fd;

    znU_set_nonblock(tcp->fd);
    tcp->can_read = tcp->can_write = 1;
    if (!znU_register(S->kqueue, tcp->fd, EV_CLEAR, &tcp->info)) {
        zn_deltcp(tcp);
        return NULL;
    }

    znU_set_nodelay(tcp->fd);
    zn_setinfo(tcp, 
            inet_ntoa(remote_addr->sin_addr),
            ntohs(remote_addr->sin_port));
    return tcp;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    ZN_GETOBJECT(S, zn_Tcp, tcp);
    tcp->fd = -1;
    tcp->can_read = tcp->can_write = 1;
    tcp->info.type = ZN_SOCK_TCP;
    tcp->info.head = tcp;
    tcp->send_result.tcp = tcp;
    tcp->recv_result.tcp = tcp;
    return tcp;
}

ZN_API int zn_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    znU_unregister(tcp->S->kqueue, tcp->fd);
    if (tcp->connect_handler) --tcp->S->waitings;
    if (tcp->send_handler) --tcp->S->waitings;
    if (tcp->recv_handler) --tcp->S->waitings;
    if (tcp->fd != -1) {
        if (close(tcp->fd) != 0)
            ret = ZN_ERROR;
        tcp->fd = -1;
    }
    return ret;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    /* We can not delete object directly since it's result may plug in
     * result queue. So we wait onresult() delete it, same with the
     * logic used in IOCP backend.
     * when recv/send, the recv/send handler have value, but result
     * queue has value depends whether tcp's status has EPOLLIN/OUT
     * bit. So if we don't have these bits, that means no result in
     * queue, so plug it to result list to wait delete */
    if (tcp->recv_handler != NULL) {
        if (!tcp->can_read)
            znR_add(tcp->S, ZN_ERROR, &tcp->recv_result);
        return;
    }
    else if (tcp->send_handler == NULL) {
        if (!tcp->can_write)
            znR_add(tcp->S, ZN_ERROR, &tcp->send_result);
        return;
    }
    ZN_PUTOBJECT(tcp);
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, zn_ConnectHandler *cb, void *ud) {
    struct sockaddr_in remoteAddr;
    int fd, ret;
    if (tcp->fd != -1)                return ZN_ESTATE;
    if (tcp->connect_handler != NULL) return ZN_EBUSY;
    if (cb == NULL)                   return ZN_EPARAM;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return ZN_ESOCKET;

    znU_set_nonblock(fd);
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

    if (!znU_register(tcp->S->kqueue, fd, EV_CLEAR, &tcp->info)) {
        close(fd);
        return ZN_EPOLL;
    }

    ++tcp->S->waitings;
    tcp->fd = fd;
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    return ZN_OK;
}

ZN_API int zn_send(zn_Tcp *tcp, const char *buff, unsigned len, zn_SendHandler *cb, void *ud) {
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->send_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;
    ++tcp->S->waitings;
    tcp->send_buffer.buff = (char*)buff;
    tcp->send_buffer.len = len;
    tcp->send_handler = cb;
    tcp->send_ud = ud;
    if (tcp->can_write) {
        ssize_t bytes = send(tcp->fd, buff, len, 0);
        if (bytes >= 0) {
            tcp->send_buffer.len = bytes;
            znR_add(tcp->S, ZN_OK, &tcp->send_result);
        }
        else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, ZN_ERROR, &tcp->send_result);
        else
            tcp->can_write = 0;
    }
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;
    tcp->recv_buffer.buff = buff;
    tcp->recv_buffer.len = len;
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    ++tcp->S->waitings;
    if (tcp->can_read) {
        ssize_t bytes = recv(tcp->fd, buff, len, 0);
        if (bytes > 0) {
            tcp->recv_buffer.len = bytes;
            znR_add(tcp->S, ZN_OK, &tcp->recv_result);
        }
        else if (bytes == 0)
            znR_add(tcp->S, ZN_ECLOSED, &tcp->recv_result);
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, ZN_ERROR, &tcp->recv_result);
        else
            tcp->can_read = 0;
    }

    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int filter, int flags) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    --tcp->S->waitings;
    tcp->connect_handler = NULL;

    if ((flags & (EV_ERROR|EV_EOF)) != 0) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    if (filter == EVFILT_WRITE) {
        tcp->can_read = tcp->can_write = 1;
        znU_set_nodelay(tcp->fd);
        cb(tcp->connect_ud, tcp, ZN_OK);
    }
}

static void zn_onresult(zn_Result *result) {
    zn_Tcp *tcp = result->tcp;
    --tcp->S->waitings;
    if (result == &tcp->send_result) {
        zn_DataBuffer buff = tcp->send_buffer;
        zn_SendHandler *cb = tcp->send_handler;
        assert(tcp->send_handler != NULL);
        tcp->send_handler = NULL;
        tcp->send_buffer.buff = NULL;
        tcp->send_buffer.len = 0;
        if (tcp->fd == -1) {
            /* cb(tcp->send_ud, tcp, ZN_ECLOSE, 0); */
            if (tcp->recv_handler == NULL)
                ZN_PUTOBJECT(tcp);
        }
        else if (result->err == ZN_OK)
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
        if (tcp->fd == -1) {
            /* cb(tcp->recv_ud, tcp, ZN_ECLOSE, 0); */
            if (tcp->send_handler == NULL)
                ZN_PUTOBJECT(tcp);
        }
        else if (result->err == ZN_OK)
            cb(tcp->recv_ud, tcp, ZN_OK, buff.len);
        else if (tcp->send_handler == NULL) {
            zn_closetcp(tcp);
            cb(tcp->recv_ud, tcp, result->err, 0);
        }
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
        if (cb == NULL) return;
        --tcp->S->waitings;
        zn_recv(tcp, tcp->recv_buffer.buff, tcp->recv_buffer.len,
                cb, tcp->recv_ud);
    }
    if (filter == EVFILT_WRITE && !can_write) {
        zn_SendHandler *cb = tcp->send_handler;
        tcp->send_handler = NULL;
        tcp->can_write = 1;
        if (cb == NULL) return;
        --tcp->S->waitings;
        zn_send(tcp, tcp->send_buffer.buff, tcp->send_buffer.len,
                cb, tcp->send_ud);
    }
}

/* accept */

struct zn_Accept {
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    int fd;
    zn_SocketInfo info;
};

ZN_API void zn_delaccept(zn_Accept *accept)
{ zn_closeaccept(accept); ZN_PUTOBJECT(accept); }

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    ZN_GETOBJECT(S, zn_Accept, accept);
    accept->fd = -1;
    accept->info.type = ZN_SOCK_ACCEPT;
    accept->info.head = accept;
    return accept;
}

ZN_API int zn_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    if (accept->accept_handler) --accept->S->waitings;
    if (accept->fd != -1) {
        znU_unregister(accept->S->kqueue, accept->fd);
        if (close(accept->fd) != 0)
            ret = ZN_ERROR;
        accept->fd = -1;
    }
    return ret;
}

ZN_API int zn_listen(zn_Accept *accept, const char *addr, unsigned port) {
    struct sockaddr_in sock_addr;
    int fd;
    if (accept->fd != -1)               return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        return ZN_ESOCKET;

    if (!znU_register(accept->S->kqueue, fd, 0, &accept->info)) {
        close(fd);
        return ZN_EPOLL;
    }

    znU_set_reuseaddr(fd);
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
    ++accept->S->waitings;
    accept->accept_handler = cb;
    accept->accept_ud = ud;
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *a, int filter, int flags) {
    zn_AcceptHandler *cb = a->accept_handler;
    --a->S->waitings;
    a->accept_handler = NULL;
    if (cb == NULL) return;

    if ((flags & (EV_EOF|EV_ERROR)) != 0) {
        zn_closeaccept(a);
        cb(a->accept_ud, a, ZN_ERROR, NULL);
        return;
    }

    if (filter == EVFILT_READ) {
        struct sockaddr_in remote_addr;
        socklen_t addr_size = sizeof(struct sockaddr_in);
        int ret = accept(a->fd, (struct sockaddr*)&remote_addr, &addr_size);
        if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (tcp != NULL) cb(a->accept_ud, a, ZN_OK, tcp);
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            zn_closeaccept(a);
            cb(a->accept_ud, a, ZN_ERROR, NULL);
        }
    }
}

/* udp */

struct zn_Udp {
    znL_entry(zn_Udp);
    zn_State *S;
    void *recv_ud; zn_RecvFromHandler *recv_handler;
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

    if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        return ZN_ESOCKET;

    if (bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) != 0)
        return ZN_EBIND;

    if (!znU_register(udp->S->kqueue, fd, 0, &udp->info)) {
        close(fd);
        return ZN_EPOLL;
    }

    udp->fd = fd;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    ZN_GETOBJECT(S, zn_Udp, udp);
    udp->fd = -1;
    udp->info.type = ZN_SOCK_UDP;
    udp->info.head = udp;
    if (!zn_initudp(udp, addr, port)) {
        ZN_PUTOBJECT(udp);
        return NULL;
    }
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    znU_unregister(udp->S->kqueue, udp->fd);
    if (udp->recv_handler) --udp->S->waitings;
    close(udp->fd);
    ZN_PUTOBJECT(udp);
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
    ++udp->S->waitings;
    udp->recv_buffer.buff = buff;
    udp->recv_buffer.len = len;
    udp->recv_handler = cb;
    udp->recv_ud = ud;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, int filter, int flags) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    if (cb == NULL) return;
    --udp->S->waitings;
    udp->recv_handler = NULL;
    udp->recv_buffer.buff = NULL;
    udp->recv_buffer.len = 0;

    if (flags & (EV_EOF|EV_ERROR)) {
        cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
        return;
    }

    if (filter == EVFILT_READ) {
        int bytes;
        memset(&udp->recvFrom, 0, sizeof(udp->recvFrom));
        udp->recvFromLen = sizeof(udp->recvFrom);
        bytes = recvfrom(udp->fd, buff.buff, buff.len, 0,
                (struct sockaddr*)&udp->recvFrom, &udp->recvFromLen);
        if (bytes >= 0)
            cb(udp->recv_ud, udp, ZN_OK, bytes,
                    inet_ntoa(((struct sockaddr_in*)&udp->recvFrom)->sin_addr),
                    ntohs(udp->recvFrom.sin_port));
        else if (errno != EAGAIN && errno != EWOULDBLOCK)
            cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
    }
}

/* poll */

ZN_API void zn_initialize(void) { }
ZN_API void zn_deinitialize(void) { }

ZN_API const char *zn_engine(void) { return "kqueue"; }

ZN_API zn_Time zn_time(void) {
#ifdef __APPLE__
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

ZN_API int zn_post(zn_State *S, zn_PostHandler *cb, void *ud) {
    int ret = ZN_OK;
    pthread_spin_lock(&S->post_lock);
    {
        char data = 0;
        ZN_GETOBJECT(S, zn_Post, post);
        post->handler = cb;
        post->ud = ud;
        *S->last_post = post;
        S->last_post = &post->next_post;
        *S->last_post = NULL;
        if (send(S->sockpairs[0], &data, 1, 0) != 1) {
            ZN_PUTOBJECT(post);
            ret = ZN_ERROR;
        }
    }
    pthread_spin_unlock(&S->post_lock);
    return ret;
}

static void zn_dispatch(zn_State *S, struct kevent *evt) {
    zn_SocketInfo *info = (zn_SocketInfo*)evt->udata;
    zn_Tcp *tcp;
    if ((int)evt->ident == S->sockpairs[1]) { /* post */
        char buff[8192];
        while (recv(evt->ident, buff, 8192, 0) > 0)
            ;
        znP_process(S);
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

static int znS_poll(zn_State *S, int checkonly) {
    int i, ret;
    S->status = ZN_STATUS_IN_RUN;
    zn_Time current;
    struct timespec timeout = { 0, 0 };
    znT_updatetimers(S, current = zn_time());
    if (!checkonly && S->results.first == NULL) {
        zn_Time ms = znT_gettimeout(S, current);
        timeout.tv_sec = ms / 1000;
        timeout.tv_nsec = (ms % 1000) * 1000000;
    }
    ret = kevent(S->kqueue, NULL, 0, S->events, ZN_MAX_EVENTS, &timeout);
    if (ret < 0) /* error out */
        goto out;
    znT_updatetimers(S, zn_time());
    for (i = 0; i < ret; ++i)
        zn_dispatch(S, &S->events[i]);
    znR_process(S);

out:
    if (S->status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_READY; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->status = ZN_STATUS_READY;
    return znT_hastimers(S) || S->waitings != 0;
}

static int znS_init(zn_State *S) {
    struct kevent kev;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0)
        return 0;
    S->kqueue = kqueue();
    EV_SET(&kev, S->sockpairs[1], EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
    if (S->kqueue < 0 ||
            kevent(S->kqueue, &kev, 1, NULL, 0, NULL) < 0) {
        if (S->kqueue >= 0) close(S->kqueue);
        close(S->sockpairs[0]);
        close(S->sockpairs[1]);
        return 0;
    }
    znP_init(S);
    znR_init(S);
    return 1;
}

static void znS_close(zn_State *S) {
    znP_process(S);
    znR_process(S);
    assert(S->results.first == NULL);
    close(S->sockpairs[0]);
    close(S->sockpairs[1]);
    close(S->kqueue);
}


#endif /* ZN_USE_KQUEUE */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-pthread' output='znet.so' */
