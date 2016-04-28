#if !defined(znet_h)
# include "znet.h"
#endif


#if defined(ZN_USE_SELECT) && !defined(znet_select_h)
#define znet_select_h 

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
#include <poll.h>
#include <sys/select.h>

# ifdef __APPLE__ /* apple don't have spinlock :( */
#   include <mach/mach.h>
#   include <mach/mach_time.h>
#   define pthread_spinlock_t  pthread_mutex_t
#   define pthread_spin_init   pthread_mutex_init
#   define pthread_spin_lock   pthread_mutex_lock
#   define pthread_spin_unlock pthread_mutex_unlock
# endif /* __APPLE__ */

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
    char  *buff;
} zn_DataBuffer;

typedef struct zn_Post {
    znQ_entry(struct zn_Post);
    zn_State *S;
    zn_PostHandler *handler;
    void *ud;
} zn_Post;

struct zn_State {
    ZN_STATE_FIELDS
    pthread_spinlock_t post_lock;
    znQ_type(zn_Post) post_queue;
    int sockpairs[2];
    int nfds;
    fd_set readfds, writefds, exceptfds;
    zn_SocketInfo *infos[FD_SETSIZE];
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

static int znU_set_nosigpipe(int socket) {
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    return setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
            (char*)&no_sigpipe, sizeof(no_sigpipe)) == 0;
#else
    return 0;
#endif /* SO_NOSIGPIPE */
}

/* post queue */

static void znP_init(zn_State *S) {
    pthread_spin_init(&S->post_lock, 0);
    znQ_init(&S->post_queue);
}

static void znP_process(zn_State *S) {
    zn_Post *post;
    pthread_spin_lock(&S->post_lock);
    post = S->post_queue.first;
    znQ_init(&S->post_queue);
    pthread_spin_unlock(&S->post_lock);
    znQ_apply(zn_Post, post,
        if (cur->handler) cur->handler(cur->ud, cur->S);
        znP_putobject(&S->posts, cur));
}

/* fds/info operations */

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
    if (fd < FD_SETSIZE) {
        FD_CLR(fd, &S->readfds);
        FD_CLR(fd, &S->exceptfds);
    }
}

static void znF_unregister_out(zn_State *S, int fd) {
    if (fd < FD_SETSIZE) {
        FD_CLR(fd, &S->writefds);
        FD_CLR(fd, &S->exceptfds);
    }
}

static void znF_unregister(zn_State *S, int fd) {
    if (fd < FD_SETSIZE) {
        S->infos[fd] = NULL;
        FD_CLR(fd, &S->readfds);
        FD_CLR(fd, &S->writefds);
        FD_CLR(fd, &S->exceptfds);
    }
}

/* tcp */

struct zn_Tcp {
    znL_entry(zn_Tcp);
    zn_State *S;
    void *connect_ud; zn_ConnectHandler *connect_handler;
    void *send_ud; zn_SendHandler *send_handler;
    void *recv_ud; zn_RecvHandler *recv_handler;
    int fd;
    zn_SocketInfo info;
    zn_PeerInfo peer_info;
    zn_DataBuffer send_buffer;
    zn_DataBuffer recv_buffer;
};

static void zn_setinfo(zn_Tcp *tcp, const char *addr, unsigned port)
{ strcpy(tcp->peer_info.addr, addr); tcp->peer_info.port = port; }

ZN_API void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)
{ *info = tcp->peer_info; }

ZN_API void zn_deltcp(zn_Tcp *tcp)
{ zn_closetcp(tcp); ZN_PUTOBJECT(tcp); }

static zn_Tcp *zn_tcpfromfd(zn_State *S, int fd, struct sockaddr_in *remote_addr) {
    zn_Tcp *tcp;
    if (fd >= FD_SETSIZE)
        return NULL;
   
    tcp = zn_newtcp(S);
    tcp->fd = fd;

    znU_set_nosigpipe(tcp->fd);
    znU_set_nonblock(tcp->fd);
    znU_set_nodelay(tcp->fd);
    zn_setinfo(tcp, 
            inet_ntoa(remote_addr->sin_addr),
            ntohs(remote_addr->sin_port));
    return tcp;
}

ZN_API zn_Tcp* zn_newtcp(zn_State *S) {
    ZN_GETOBJECT(S, zn_Tcp, tcp);
    tcp->fd = -1;
    tcp->info.type = ZN_SOCK_TCP;
    tcp->info.head = tcp;
    return tcp;
}

ZN_API int zn_closetcp(zn_Tcp *tcp) {
    int ret = ZN_OK;
    if (tcp->connect_handler) --tcp->S->waitings;
    if (tcp->send_handler) --tcp->S->waitings;
    if (tcp->recv_handler) --tcp->S->waitings;
    if (tcp->fd != -1) {
        znF_unregister(tcp->S, tcp->fd);
        if (close(tcp->fd) != 0)
            ret = ZN_ERROR;
        tcp->fd = -1;
    }
    return ret;
}

ZN_API int zn_connect(zn_Tcp *tcp, const char *addr, unsigned port, zn_ConnectHandler *cb, void *ud) {
    struct sockaddr_in remoteAddr;
    int fd, ret;
    if (tcp->fd != -1)                return ZN_ESTATE;
    if (tcp->connect_handler != NULL) return ZN_EBUSY;
    if (cb == NULL)                   return ZN_EPARAM;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return ZN_ESOCKET;

    if (fd >= FD_SETSIZE) {
        close(fd);
        return ZN_ESOCKET;
    }

    if (tcp->S->nfds < fd)
        tcp->S->nfds = fd;
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

    ++tcp->S->waitings;
    tcp->fd = fd;
    tcp->connect_handler = cb;
    tcp->connect_ud = ud;
    znF_register_out(tcp->S, fd, &tcp->info);
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
    znF_register_out(tcp->S, tcp->fd, &tcp->info);
    return ZN_OK;
}

ZN_API int zn_recv(zn_Tcp *tcp, char *buff, unsigned len, zn_RecvHandler *cb, void *ud) {
    if (tcp->fd == -1)             return ZN_ESTATE;
    if (tcp->recv_handler != NULL) return ZN_EBUSY;
    if (cb == NULL || len == 0)    return ZN_EPARAM;
    ++tcp->S->waitings;
    tcp->recv_buffer.buff = buff;
    tcp->recv_buffer.len = len;
    tcp->recv_handler = cb;
    tcp->recv_ud = ud;
    znF_register_in(tcp->S, tcp->fd, &tcp->info);
    return ZN_OK;
}

static void zn_onconnect(zn_Tcp *tcp, int err) {
    zn_ConnectHandler *cb = tcp->connect_handler;
    assert(tcp->connect_handler);
    --tcp->S->waitings;
    tcp->connect_handler = NULL;
    znF_unregister_out(tcp->S, tcp->fd);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->connect_ud, tcp, ZN_ERROR);
        return;
    }

    znU_set_nosigpipe(tcp->fd);
    znU_set_nodelay(tcp->fd);
    cb(tcp->connect_ud, tcp, ZN_OK);
}

static void zn_onsend(zn_Tcp *tcp, int err) {
    zn_SendHandler *cb = tcp->send_handler;
    zn_DataBuffer buff = tcp->send_buffer;
    int bytes;
    assert(tcp->send_handler);
    if (tcp->fd == -1) return;
    --tcp->S->waitings;
    tcp->send_handler = NULL;
    tcp->send_buffer.buff = NULL;
    tcp->send_buffer.len = 0;
    znF_unregister_out(tcp->S, tcp->fd);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->send_ud, tcp, ZN_ERROR, 0);
        return;
    }

#ifdef MSG_NOSIGNAL 
    if ((bytes = send(tcp->fd, buff.buff, buff.len, MSG_NOSIGNAL)) >= 0)
#else
    if ((bytes = send(tcp->fd, buff.buff, buff.len, 0)) >= 0)
#endif /* MSG_NOSIGNAL */
        cb(tcp->send_ud, tcp, ZN_OK, bytes);
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        zn_closetcp(tcp);
        cb(tcp->send_ud, tcp, ZN_ERROR, 0);
    }
}

static void zn_onrecv(zn_Tcp *tcp, int err) {
    zn_RecvHandler *cb = tcp->recv_handler;
    zn_DataBuffer buff = tcp->recv_buffer;
    int bytes;
    if (tcp->fd == -1) return;
    assert(tcp->recv_handler);
    --tcp->S->waitings;
    tcp->recv_handler = NULL;
    tcp->recv_buffer.buff = NULL;
    tcp->recv_buffer.len = 0;
    znF_unregister_in(tcp->S, tcp->fd);

    if (err) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ERROR, 0);
        return;
    }

    if ((bytes = recv(tcp->fd, buff.buff, buff.len, 0)) >= 0)
        cb(tcp->recv_ud, tcp, ZN_OK, bytes);
    else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        zn_closetcp(tcp);
        cb(tcp->recv_ud, tcp, ZN_ERROR, 0);
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
        znF_unregister(accept->S, accept->fd);
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
    if (accept->S->nfds < fd)
        accept->S->nfds = fd;

    znU_set_nonblock(fd);
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
    if (accept->fd == -1)               return ZN_ESTATE;
    if (accept->accept_handler != NULL) return ZN_EBUSY;
    if (cb == NULL)                     return ZN_EPARAM;
    ++accept->S->waitings;
    accept->accept_handler = cb;
    accept->accept_ud = ud;
    znF_register_in(accept->S, accept->fd, &accept->info);
    return ZN_OK;
}

static void zn_onaccept(zn_Accept *a, int err) {
    zn_AcceptHandler *cb = a->accept_handler;
    --a->S->waitings;
    if (a->accept_handler == NULL) return;
    a->accept_handler = NULL;
    znF_unregister_in(a->S, a->fd);

    if (err) {
        zn_closeaccept(a);
        cb(a->accept_ud, a, ZN_ERROR, NULL);
    }
    else {
        struct sockaddr_in remote_addr;
        socklen_t addr_size = sizeof(struct sockaddr_in);
        int ret = accept(a->fd, (struct sockaddr*)&remote_addr, &addr_size);
        if (ret >= FD_SETSIZE)
            close(ret);
        else if (ret >= 0) {
            zn_Tcp *tcp = zn_tcpfromfd(a->S, ret, &remote_addr);
            if (a->S->nfds < ret)
                a->S->nfds = ret;
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
    znF_unregister(udp->S, udp->fd);
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
    znF_register_in(udp->S, udp->fd, &udp->info);
    if (udp->S->nfds < udp->fd)
        udp->S->nfds = udp->fd;
    return ZN_OK;
}

static void zn_onrecvfrom(zn_Udp *udp, int err) {
    zn_DataBuffer buff = udp->recv_buffer;
    zn_RecvFromHandler *cb = udp->recv_handler;
    assert(udp->recv_handler != NULL);
    --udp->S->waitings;
    udp->recv_handler = NULL;
    udp->recv_buffer.buff = NULL;
    udp->recv_buffer.len = 0;
    znF_unregister_in(udp->S, udp->fd);

    if (err)
        cb(udp->recv_ud, udp, ZN_ERROR, 0, "0.0.0.0", 0);
    else {
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

ZN_API const char *zn_engine(void) { return "select"; }

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
    zn_Post *post;
    int ret = ZN_OK;
    char data = 0;
    pthread_spin_lock(&S->post_lock);
    post = (zn_Post*)znP_getobject(&S->posts);
    post->S = S;
    post->handler = cb;
    post->ud = ud;
    znQ_enqueue(&S->post_queue, post);
    if (send(S->sockpairs[0], &data, 1, 0) != 1) {
        znP_putobject(&S->posts, post);
        ret = ZN_ERROR;
    }
    pthread_spin_unlock(&S->post_lock);
    return ret;
}

static void zn_dispatch(zn_State *S, int fd, int setno) {
    zn_SocketInfo *info;
    zn_Tcp *tcp;
    int err = setno == 2;
    if (fd < 0 || fd >= FD_SETSIZE) return;
    if (fd == S->sockpairs[1]) { /* post */
        char buff[8192];
        while (recv(fd, buff, 8192, 0) > 0)
            ;
        znP_process(S);
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

static int znS_poll(zn_State *S, int checkonly) {
    int i, ret;
    fd_set readfds = S->readfds;
    fd_set writefds = S->writefds;
    fd_set exceptfds = S->exceptfds;
    struct timeval timeout = { 0, 0 };
    zn_Time current;
    S->status = ZN_STATUS_IN_RUN;
    znT_updatetimers(S, current = zn_time());
    if (!checkonly) {
        zn_Time ms = znT_gettimeout(S, current);
        timeout.tv_sec = ms/1000;
        timeout.tv_usec = ms%1000*1000;
    }
    ret = select(S->nfds+1, &readfds, &writefds, &exceptfds, &timeout);
    if (ret < 0 && errno != EINTR) /* error out */
        goto out;
    znT_updatetimers(S, zn_time());
    for (i = 0; i <= S->nfds; ++i) {
        if (FD_ISSET(i, &readfds))
            zn_dispatch(S, i, 0);
        if (FD_ISSET(i, &writefds))
            zn_dispatch(S, i, 1);
        if (FD_ISSET(i, &exceptfds))
            zn_dispatch(S, i, 2);
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

static int znS_init(zn_State *S) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, S->sockpairs) != 0)
        return 0;
    if (S->sockpairs[1] >= FD_SETSIZE) {
        close(S->sockpairs[0]);
        close(S->sockpairs[1]);
        return 0;
    }
    znP_init(S);
    FD_ZERO(&S->readfds);
    FD_ZERO(&S->writefds);
    FD_ZERO(&S->exceptfds);
    FD_SET(S->sockpairs[1], &S->readfds);
    znU_set_nonblock(S->sockpairs[1]);
    return 1;
}

static void znS_close(zn_State *S) {
    znP_process(S);
    close(S->sockpairs[0]);
    close(S->sockpairs[1]);
}


#endif /* ZN_USE_SELECT */
/* win32cc: flags+='-s -O3 -mdll -DZN_IMPLEMENTATION -xc'
 * win32cc: libs+='-lws2_32' output='znet.dll' */
/* unixcc: flags+='-s -O3 -shared -fPIC -DZN_USE_SELECT -DZN_IMPLEMENTATION -xc'
 * unixcc: libs+='-pthread -lrt' output='znet.so' */
