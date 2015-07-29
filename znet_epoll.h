#if !defined(znet_h) && !defined(ZN_USE_EPOLL)
# include "znet.h"
#endif


#if defined(__linux__) && defined(ZN_USE_EPOLL) && !defined(zn_epoll_imcluded)
#define zn_epoll_imcluded 

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
#include <sys/epoll.h>
#include <sys/eventfd.h>

#define ZN_MAX_EVENTS 4096

typedef struct zn_DataBuffer {
    size_t len;
    char  *buff;
} zn_DataBuffer;

typedef struct zn_Post {
    znL_entry(struct zn_Post);
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

typedef struct zn_Result {
    struct zn_Result *next; 
    int err;
    zn_Tcp *tcp;
} zn_Result;

struct zn_State {
    ZN_STATE_FIELDS
    pthread_spinlock_t post_lock;
    znQ_type(zn_Post)   posts;
    znQ_type(zn_Result) results;
    int epoll;
    int eventfd;
};

/* utils */

static int znU_set_nodelay(int socket) {
    int enable = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
            (char*)&enable, sizeof(enable)) == 0;
}

static int znU_set_nonblock(int socket) {
    return fcntl(socket, F_SETFL,
            fcntl(socket, F_GETFL)|O_NONBLOCK) == 0;
}

static int znU_set_reuseaddr(int socket) {
    int reuse_addr = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            (char*)&reuse_addr, sizeof(reuse_addr)) == 0;
}

/* post queue */

static void znP_init(zn_State *S) {
    pthread_spin_init(&S->post_lock, 0);
    znQ_init(&S->posts);
}

static void znP_add(zn_State *S, zn_Post *ps) {
    pthread_spin_lock(&S->post_lock);
    znQ_enqueue(&S->posts, ps);
    pthread_spin_unlock(&S->post_lock);
}

static void znP_process(zn_State *S) {
    zn_Post *ps = S->posts.first;
    if (ps == NULL) return;
    pthread_spin_lock(&S->post_lock);
    znQ_init(&S->posts);
    pthread_spin_unlock(&S->post_lock);
    while (ps) {
        zn_Post *next = ps->next;
        if (ps->handler)
            ps->handler(ps->ud, S);
        free(ps);
        ps = next;
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
    while ((results = S->results.first) != NULL) {
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
    zn_Tcp *tcp = zn_newtcp(S);
    tcp->fd = fd;

    znU_set_nonblock(tcp->fd);
    tcp->status = EPOLLIN|EPOLLOUT;
    tcp->event.events = EPOLLET|EPOLLIN|EPOLLOUT|EPOLLRDHUP;
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_ADD,
                tcp->fd, &tcp->event) != 0)
    {
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
    tcp->event.data.ptr = &tcp->info;
    tcp->fd = -1;
    tcp->status = EPOLLIN|EPOLLOUT;
    tcp->info.type = ZN_SOCK_TCP;
    tcp->info.head = tcp;
    tcp->send_result.tcp = tcp;
    tcp->recv_result.tcp = tcp;
    return tcp;
}

ZN_API void zn_deltcp(zn_Tcp *tcp) {
    zn_closetcp(tcp);
    ZN_PUTOBJECT(tcp);
}

ZN_API int zn_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    tcp->event.events = 0;
    epoll_ctl(tcp->S->epoll, EPOLL_CTL_DEL,
            tcp->fd, &tcp->event);
    if (tcp->fd != -1) {
        if (close(tcp->fd) != 0)
            ret = ZN_ERROR;
        tcp->fd = -1;
    }
    return ret;
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

    tcp->event.events = EPOLLOUT;
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_ADD,
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
            znR_add(tcp->S, ZN_OK, &tcp->send_result);
        }
        else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, ZN_ERROR, &tcp->send_result);
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
            znR_add(tcp->S, ZN_OK, &tcp->recv_result);
        }
        else if (bytes == 0)
            znR_add(tcp->S, ZN_ECLOSED, &tcp->recv_result);
        else if (bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            znR_add(tcp->S, ZN_ERROR, &tcp->recv_result);
        else
            tcp->status &= ~EPOLLIN;
    }

    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int eventmask) {
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
    if (epoll_ctl(tcp->S->epoll, EPOLL_CTL_MOD,
                tcp->fd, &tcp->event) != 0)
    {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    znU_set_nodelay(tcp->fd);
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
    znL_entry(zn_Accept);
    zn_State *S;
    void *accept_ud; zn_AcceptHandler *accept_handler;
    struct epoll_event event;
    int fd;
    zn_SocketInfo info;
};

ZN_API zn_Accept* zn_newaccept(zn_State *S) {
    ZN_GETOBJECT(S, zn_Accept, accept);
    accept->event.data.ptr = &accept->info;
    accept->fd = -1;
    accept->info.type = ZN_SOCK_ACCEPT;
    accept->info.head = accept;
    return accept;
}

ZN_API void zn_delaccept(zn_Accept *accept) {
    zn_closeaccept(accept);
    ZN_PUTOBJECT(accept);
}

ZN_API int zn_closeaccept(zn_Accept *accept) {
    int ret = ZN_OK;
    accept->event.events = 0;
    epoll_ctl(accept->S->epoll, EPOLL_CTL_DEL,
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
    int fd;
    if (accept->fd != -1)               return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
        return ZN_ESOCKET;

    accept->event.events = EPOLLIN;
    if (epoll_ctl(accept->S->epoll, EPOLL_CTL_ADD,
                fd, &accept->event) != 0)
    {
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
    znL_entry(zn_Udp);
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
    if (epoll_ctl(udp->S->epoll, EPOLL_CTL_ADD,
                fd, &udp->event) != 0) {
        close(fd);
        return ZN_EPOLL;
    }

    udp->fd = fd;
    return ZN_OK;
}

ZN_API zn_Udp* zn_newudp(zn_State *S, const char *addr, unsigned port) {
    ZN_GETOBJECT(S, zn_Udp, udp);
    udp->event.data.ptr = &udp->info;
    udp->fd = -1;
    udp->info.type = ZN_SOCK_UDP;
    udp->info.head = udp;
    if (!zn_initudp(udp, addr, port)) {
        free(udp);
        return NULL;
    }
    return udp;
}

ZN_API void zn_deludp(zn_Udp *udp) {
    udp->event.events = 0;
    epoll_ctl(udp->S->epoll, EPOLL_CTL_DEL, udp->fd, &udp->event);
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

static int zn_initstate(zn_State *S, int epoll) {
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = NULL;
    S->epoll = epoll;
    S->eventfd = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    if (S->epoll == -1
            || S->eventfd == -1
            || epoll_ctl(S->epoll, EPOLL_CTL_ADD, S->eventfd, &event) != 0)
    {
        if (S->eventfd != -1) close(S->eventfd);
        return 0;
    }
    znP_init(S);
    znR_init(S);
    return 1;
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
    znP_add(S, ps);
    eventfd_write(S->eventfd, (eventfd_t)1);
    return 1;
}

static void zn_dispatch(zn_State *S, struct epoll_event *evt) {
    zn_SocketInfo *info = (zn_SocketInfo*)evt->data.ptr;
    zn_Tcp *tcp;
    int eventmask = evt->events;
    if (info == NULL) { /* post */
        eventfd_t value;
        eventfd_read(S->eventfd, &value);
        znP_process(S);
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
    S->status = ZN_STATUS_IN_RUN;
    znT_updatetimers(S, zn_time());
    ret = epoll_wait(S->epoll, events, ZN_MAX_EVENTS,
            checkonly ? 0 : znT_gettimeout(S, zn_time()));
    if (ret == -1 && errno != EINTR) /* error out */
        goto out;
    znT_updatetimers(S, zn_time());
    for (i = 0; i < ret; ++i)
        zn_dispatch(S, &events[i]);
    znR_process(S);

out:
    if (S->status == ZN_STATUS_CLOSING_IN_RUN) {
        S->status = ZN_STATUS_READY; /* trigger real close */
        zn_close(S);
        return 0;
    }
    S->status = ZN_STATUS_READY;
    return znT_hastimers(S);
}

static int znS_init(zn_State *S) {
    int epoll = epoll_create(ZN_MAX_EVENTS);
    if (epoll == -1 || !zn_initstate(S, epoll)) {
        if (epoll != -1) close(epoll);
        return 0;
    }
    return 1;
}

ZN_API int znS_clone(zn_State *NS, zn_State *S) {
    return zn_initstate(NS, S->epoll);
}

static void znS_close(zn_State *S) {
    close(S->epoll);
}


#endif /* ZN_USE_EPOLL */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-lpthread' output='znet.so' */
