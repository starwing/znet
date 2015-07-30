#ifndef ZNET_HPP_INCLUDED
#define ZNET_HPP_INCLUDED


#ifndef ZNET_HPP_NO_IMPLEMENTATION
# define ZN_IMPLEMENTATION
# define ZN_API static inline
#endif
#include "znet.h"


#define ZNPP_NS_BEGIN namespace zsummer { namespace network {
#define ZNPP_NS_END   }                   }


#include <functional>
#include <memory>
#include <string>
#include <unordered_map>


ZNPP_NS_BEGIN


/* declarations */
struct EventLoop;
struct TcpAccept;
struct TcpSocket;
struct UdpSocket;

enum NetErrorCode
{
    NEC_SUCCESS       = ZN_OK,
    NEC_ERROR         = ZN_ERROR,
    NEC_REMOTE_CLOSED = ZN_ECLOSED,
    NEC_REMOTE_HANGUP = ZN_EHANGUP,
};

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


/* interfaces */

struct EventLoop : public std::enable_shared_from_this<EventLoop>
{
    EventLoop(zn_State *S = nullptr)
        : S(S)
    { }

    zn_State *S = nullptr;
    std::unordered_map<unsigned long long, OnTimerHandler> timers;

    bool initialize();
    void runOnce(bool isImeediately = false);
    void post(OnPostHandler&& h);
    unsigned long long createTimer(unsigned int delayms, OnTimerHandler&& h);
    bool cancelTimer(unsigned long long timerID);
};

struct TcpAccept : public std::enable_shared_from_this<TcpAccept>
{
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

struct TcpSocket : public std::enable_shared_from_this<TcpSocket>
{
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

struct UdpSocket : public std::enable_shared_from_this<UdpSocket>
{
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

inline void EventLoop::runOnce(bool isImeediately/* = false*/)
{ zn_run(S, isImeediately ? ZN_RUN_CHECK : ZN_RUN_ONCE); }

inline bool EventLoop::initialize()
{
    if (S == nullptr)
        return (S = zn_newstate()) != nullptr;
    return true;
}

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
    EventLoop* loop = static_cast<EventLoop*>(ud);
    auto it = loop->timers.find(reinterpret_cast<unsigned long long>(timer));
    if (it == loop->timers.end()) return;
    auto h = std::move(it->second);
    loop->timers.erase(it);
    h();
}

inline unsigned long long EventLoop::createTimer(unsigned int delayms, OnTimerHandler&& h)
{
    zn_Timer *t = zn_newtimer(S, timer_cb, this);
    auto timerID = reinterpret_cast<unsigned long long>(t);
    timers[timerID] = std::move(h);
    zn_starttimer(t, delayms);
    return timerID;
}

inline bool EventLoop::cancelTimer(unsigned long long timerID)
{
    auto it = timers.find(timerID);
    if (it == timers.end()) return false;
    zn_canceltimer(reinterpret_cast<zn_Timer*>(timerID));
    timers.erase(it);
    return true;
}


inline bool TcpAccept::openAccept(std::string const& ip, unsigned short port)
{ return zn_listen(accept, ip.c_str(), port) == ZN_OK; }

inline bool TcpAccept::initialize(EventLoopPtr const& summer) {
    if (accept == nullptr)
        return (accept = zn_newaccept(summer->S)) != nullptr;
    return true;
}

static inline void accept_cb(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp)
{
    TcpAccept *ta = static_cast<TcpAccept*>(ud);
    OnAcceptHandler h(std::move(ta->acceptHandler));
    ta->client->tcp = tcp;
    h(static_cast<NetErrorCode>(err), std::move(ta->client));
}

inline bool TcpAccept::doAccept(TcpSocketPtr const& s, OnAcceptHandler&& h)
{
    if (zn_accept(accept, accept_cb, this) == ZN_OK)
    {
        client = s;
        acceptHandler = h;
        return true;
    }
    return false;
}


inline bool TcpSocket::doClose()
{ return zn_closetcp(tcp) == ZN_OK; }

inline bool TcpSocket::initialize(EventLoopPtr const& summer) {
    if (tcp == nullptr)
        return (tcp = zn_newtcp(summer->S)) != nullptr;
    return true;
}

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
    if (zn_connect(tcp, remoteIP.c_str(), remotePort, connect_cb, this) == ZN_OK)
    {
        connectHandler = h;
        return true;
    }
    return false;
}

static inline void send_cb(void *ud, zn_Tcp *tcp, unsigned err, unsigned count)
{
    TcpSocket *ts = static_cast<TcpSocket*>(ud);
    OnSendHandler h(std::move(ts->sendHandler));
    h(static_cast<NetErrorCode>(err), count);
}

inline bool TcpSocket::doSend(char const* buf, unsigned len, OnSendHandler&& h)
{
    if (zn_send(tcp, buf, len, send_cb, this) == ZN_OK)
    {
        sendHandler = h;
        return true;
    }
    return false;
}

static inline void recv_cb(void *ud, zn_Tcp *tcp, unsigned err, unsigned count)
{
    TcpSocket *ts = static_cast<TcpSocket*>(ud);
    OnRecvHandler h(std::move(ts->recvHandler));
    h(static_cast<NetErrorCode>(err), count);
}

inline bool TcpSocket::doRecv(char* buf, unsigned len, OnRecvHandler&& h)
{
    if (zn_recv(tcp, buf, len, recv_cb, this) == ZN_OK)
    {
        recvHandler = h;
        return true;
    }
    return false;
}

    
inline bool UdpSocket::doSendTo(char const* buf, unsigned len, std::string const& remoteIP, unsigned short remotePort)
{ return zn_sendto(udp, buf, len, remoteIP.c_str(), remotePort) == ZN_OK; }

inline bool UdpSocket::initialize(EventLoopPtr const& summer, std::string const& ip, unsigned short port) {
    if (udp == nullptr)
        return (udp = zn_newudp(summer->S, ip.c_str(), port)) != nullptr;
    return true;
}

static inline void recvfrom_cb(void *ud, zn_Udp *udp, unsigned err, unsigned count, const char *ip, unsigned port)
{
    UdpSocket *us = static_cast<UdpSocket*>(ud);
    OnRecvFromHandler h(std::move(us->recvFromHandler));
    h(static_cast<NetErrorCode>(err), ip, port, count);
}

inline bool UdpSocket::doRecv(char* buf, unsigned len, OnRecvFromHandler&& h)
{
    if (zn_recvfrom(udp, buf, len, recvfrom_cb, this) == ZN_OK)
    {
        recvFromHandler = h;
        return true;
    }
    return false;
}


class ZSummerEnvironment {
public:
    ZSummerEnvironment() { zn_initialize(); }
    ~ZSummerEnvironment() { zn_deinitialize(); }
};

extern ZSummerEnvironment g_appEnvironment;

#ifdef ZNPP_DEFINE_ENV
ZSummerEnvironment g_appEnvironment;
#endif


ZNPP_NS_END

#endif /* ZNET_HPP_INCLUDED */
/* cc: flags+='-std=c++11' */
