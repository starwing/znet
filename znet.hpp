#ifndef ZNET_HPP_INCLUDED
#define ZNET_HPP_INCLUDED


#if ZNET_HPP_HEADER_ONLY
# define ZN_IMPLEMENTATION
# define ZN_API static inline
#endif
#include "znet.h"


#undef ZN_NS_BEGIN
#undef ZN_NS_END
#define ZN_NS_BEGIN namespace zsummer { namespace network {
#define ZN_NS_END   }                   }


#include <functional>
#include <memory>
#include <string>


ZN_NS_BEGIN


enum NetErrorCode
{
    NEC_SUCCESS = ZN_OK,
    NEC_ERROR   = ZN_ERROR,
    NEC_REMOTE_CLOSED = ZN_ECLOSED,
    NEC_REMOTE_HANGUP = ZN_EHANGUP,
};

/* declarations */
class EventLoop;
class TcpAccept;
class TcpSocket;
class UdpSocket;

/* pointers */
using EventLoopPtr = std::shared_ptr<EventLoop>;
using TcpAcceptPtr = std::shared_ptr<TcpAccept>;
using TcpSocketPtr = std::shared_ptr<TcpSocket>;
using UdpSocketPtr = std::shared_ptr<UdpSocket>;

/* callbacks */
using OnPostHandler     = std::function<void()>;
using OnTimerHandler    = std::function<void()>;
using OnAcceptHandler   = std::function<void(NetErrorCode, TcpSocketPtr)>;
using OnConnectHandler  = std::function<void(NetErrorCode)>;
using OnSendHandler     = std::function<void(NetErrorCode, unsigned)>;
using OnRecvHandler     = std::function<void(NetErrorCode, unsigned)>;
using OnRecvFromHandler = std::function<void(NetErrorCode, char const*, unsigned short, unsigned)>;


class EventLoop : public std::enable_shared_from_this<EventLoop>
{
public:
    zn_State *S = nullptr;

    bool initialize();
    void runOnce(bool isImeediately = false);
    void post(OnPostHandler&& h);
    unsigned long long createTimer(unsigned int delayms, OnTimerHandler&& h);
    bool cancelTimer(unsigned long long timerID);
};


class TcpAccept : public std::enable_shared_from_this<TcpAccept>
{
public:
    TcpAccept(zn_Accept *accept = nullptr)
        : accept(accept)
    { }

    zn_Accept *accept;
    TcpSocketPtr client;
    OnAcceptHandler acceptHandler;

    bool initialize(EventLoopPtr const& summer);
    bool openAccept(std::string const& ip, unsigned short port);
    bool doAccept(TcpSocketPtr const& s, OnAcceptHandler&& h);
};


class TcpSocket : public std::enable_shared_from_this<TcpSocket>
{
public:
    TcpSocket(zn_Tcp *tcp = nullptr)
        : tcp(tcp)
    { }

    zn_Tcp *tcp = nullptr;
    OnConnectHandler connectHandler;
    OnSendHandler sendHandler;
    OnRecvHandler recvHandler;

    bool initialize(EventLoopPtr const& summer);
    bool doClose();

    bool getPeerInfo(std::string& remoteIP, unsigned short& remotePort) const;
    bool doConnect(std::string const& remoteIP, unsigned short remotePort, OnConnectHandler&& h);
    bool doSend(char const* buf, unsigned len, OnSendHandler&& h);
    bool doRecv(char* buf, unsigned len, OnRecvHandler&& h);
};


class UdpSocket : public std::enable_shared_from_this<UdpSocket>
{
public:
    UdpSocket(zn_Udp *udp = nullptr)
        : udp(udp)
    { }

    zn_Udp *udp = nullptr;
    OnRecvFromHandler recvFromHandler;

    bool initialize(EventLoopPtr const& summer, std::string const& ip, unsigned short port);
    bool doSendTo(char const* buf, unsigned len, std::string const& remoteIP, unsigned short remotePort);;
    bool doRecv(char* buf, unsigned len, OnRecvFromHandler&& h);
};


/* implements */

inline bool EventLoop::initialize()
{ return (S = zn_newstate()) != nullptr; }

inline void EventLoop::runOnce(bool isImeediately/* = false*/)
{ zn_run(S, isImeediately ? ZN_RUN_CHECK : ZN_RUN_ONCE); }

inline void post_cb(void *ud, zn_State *) 
{
    std::unique_ptr<OnPostHandler> h
        (reinterpret_cast<OnPostHandler*>(ud));
    (*h)();
}

inline void EventLoop::post(OnPostHandler&& h)
{
    auto newh = new OnPostHandler(h);
    zn_post(S, post_cb, newh);
}

static inline void timer_cb(void *ud, zn_Timer *timer, unsigned elapsed)
{
    std::unique_ptr<OnTimerHandler> h
        (reinterpret_cast<OnTimerHandler*>(ud));
    (*h)();
}

inline unsigned long long EventLoop::createTimer(unsigned int delayms, OnTimerHandler&& h)
{
    auto newh = new OnTimerHandler(h);
    zn_Timer *t = zn_newtimer(S, timer_cb, newh);
    zn_starttimer(t, delayms);
    return (unsigned long long)(uintptr_t)t;
}

inline bool EventLoop::cancelTimer(unsigned long long timerID)
{
    zn_canceltimer(reinterpret_cast<zn_Timer*>(timerID));
    return true;
}


inline bool TcpAccept::initialize(EventLoopPtr const& summer)
{ return (accept = zn_newaccept(summer->S)) != nullptr; }

inline bool TcpAccept::openAccept(std::string const& ip, unsigned short port)
{ return zn_listen(accept, ip.c_str(), port) == ZN_OK; }

static inline void accept_ud(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp)
{
    TcpAccept *ta = static_cast<TcpAccept*>(ud);
    OnAcceptHandler h(std::move(ta->acceptHandler));
    ta->client->tcp = tcp;
    h(static_cast<NetErrorCode>(err), std::move(ta->client));
}

inline bool TcpAccept::doAccept(TcpSocketPtr const& s, OnAcceptHandler&& h)
{
    client = s;
    acceptHandler = h;
    return zn_accept(accept, accept_ud, this) == ZN_OK;
}


inline bool TcpSocket::initialize(EventLoopPtr const& summer)
{ return (tcp = zn_newtcp(summer->S)) != nullptr; }

inline bool TcpSocket::doClose()
{ return zn_closetcp(tcp) == ZN_OK; }

inline bool TcpSocket::getPeerInfo(std::string& remoteIP, unsigned short& remotePort) const
{ 
    zn_PeerInfo info;
    zn_getpeerinfo(tcp, &info);
    remoteIP = info.addr;
    remotePort = info.port;
    return true;
}

static inline void connect_cb(void *ud, zn_Tcp *tcp, unsigned err)
{
    TcpSocket *ts = static_cast<TcpSocket*>(ud);
    OnConnectHandler h(std::move(ts->connectHandler));
    h(static_cast<NetErrorCode>(err));
}

inline bool TcpSocket::doConnect(std::string const& remoteIP, unsigned short remotePort, OnConnectHandler&& h)
{
    connectHandler = h;
    return zn_connect(tcp, remoteIP.c_str(), remotePort, connect_cb, this) == ZN_OK;
}

static inline void send_cb(void *ud, zn_Tcp *tcp, unsigned err, unsigned count)
{
    TcpSocket *ts = static_cast<TcpSocket*>(ud);
    OnSendHandler h(std::move(ts->sendHandler));
    h(static_cast<NetErrorCode>(err), count);
}

inline bool TcpSocket::doSend(char const* buf, unsigned len, OnSendHandler&& h)
{
    sendHandler = h;
    return zn_send(tcp, buf, len, send_cb, this) == ZN_OK;
}

static inline void recv_cb(void *ud, zn_Tcp *tcp, unsigned err, unsigned count)
{
    TcpSocket *ts = static_cast<TcpSocket*>(ud);
    OnRecvHandler h(std::move(ts->recvHandler));
    h(static_cast<NetErrorCode>(err), count);
}

inline bool TcpSocket::doRecv(char* buf, unsigned len, OnRecvHandler&& h)
{
    recvHandler = h;
    return zn_recv(tcp, buf, len, recv_cb, this) == ZN_OK;
}


    
inline bool UdpSocket::initialize(EventLoopPtr const& summer, std::string const& ip, unsigned short port)
{ return (udp = zn_newudp(summer->S, ip.c_str(), port)) != nullptr; }

inline bool UdpSocket::doSendTo(char const* buf, unsigned len, std::string const& remoteIP, unsigned short remotePort)
{ return zn_sendto(udp, buf, len, remoteIP.c_str(), remotePort) == ZN_OK; }

static inline void recvfrom_cb(void *ud, zn_Udp *udp, unsigned err, unsigned count, const char *ip, unsigned port)
{
    UdpSocket *us = static_cast<UdpSocket*>(ud);
    OnRecvFromHandler h(std::move(us->recvFromHandler));
    h(static_cast<NetErrorCode>(err), ip, port, count);
}

inline bool UdpSocket::doRecv(char* buf, unsigned len, OnRecvFromHandler&& h)
{
    recvFromHandler = h;
    return zn_recvfrom(udp, buf, len, recvfrom_cb, this) == ZN_OK;
}


ZN_NS_END

#endif /* ZNET_HPP_INCLUDED */
/* cc: flags+='-std=c++11' */
