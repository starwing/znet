/* examples/example.cpp -- znet.hpp (C++11) demo, lambda style.
 *
 * The same loopback demo as example.c, but written the way the wrapper wants
 * to be used: every step is a local lambda, each callback installs the next
 * one, and a shared_ptr keeps a socket or a borrowed buffer alive until its
 * completion.  An accepted connection arrives as its own TcpSocketPtr.
 *
 * Run the loop with EventLoop::runLoop(): it wraps ZN_RUN_LOOP, so it returns
 * as soon as no operation is in flight -- the demo needs no exit flag.
 *
 * A small state record per connection keeps the capture lists short; the
 * handlers themselves stay plain lambdas.
 *
 * Build it as the one translation unit that owns the implementation:
 *
 *   c++ -std=c++11 -I. -pthread examples/example.cpp -o example-cpp
 */
#define ZNPP_STATIC_API /* ZN_IMPLEMENTATION + static znet in this file */
#include "../znet.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace zsummer::network;

namespace {

unsigned short const kPort = 12346;
unsigned short const kUdpPort = 8088;
int const            kClients = 3;
unsigned const       kBufSize = 1024;
char const           kPing[] = "hello udp";

struct ServerConn {
    TcpSocketPtr sock;
    std::string  buf; /* borrowed by recv, then by the echo send */
};

struct ClientConn {
    int          idx;
    TcpSocketPtr sock;
    std::string  hello; /* borrowed by the send */
    std::string  buf;   /* borrowed by recv */
    unsigned     helloLen;
};

} /* namespace */

int main() {
    auto loop = std::make_shared<EventLoop>();
    if (!loop->initialize()) {
        std::fprintf(stderr, "example.cpp: cannot create a znet state\n");
        return 1;
    }
    std::printf("example.cpp: znet.hpp on the %s engine\n", zn_engine());

    bool failed = false;
    auto fail = [&](char const* what, NetErrorCode ec) {
        if (ec == NEC_SUCCESS)
            std::fprintf(stderr, "example.cpp: %s\n", what);
        else
            std::fprintf(stderr, "example.cpp: %s: %s\n", what,
                         zn_strerror(ec));
        failed = true;
    };

    /* ---- tcp echo server: one fresh TcpSocketPtr per accept ------------ */
    auto accept = std::make_shared<TcpAccept>();
    accept->initialize(loop);
    if (!accept->openAccept("127.0.0.1", kPort)) {
        fail("listen", static_cast<NetErrorCode>(zn_lasterror(loop->S)));
        accept.reset();
        loop.reset();
        return 1;
    }

    int             accepted = 0;
    OnAcceptHandler onAccept;
    onAccept = [&](NetErrorCode ec, TcpSocketPtr s) {
        if (!s) { /* the listener is still open, just no longer armed */
            fail("accept", ec);
            return;
        }
        std::printf("server: accepted connection #%d\n", ++accepted);

        auto conn = std::make_shared<ServerConn>();
        conn->sock = std::move(s);
        conn->buf.assign(kBufSize, '\0');

        auto onSent = [conn](NetErrorCode ec, unsigned count) {
            if (ec == NEC_SUCCESS) {
                conn->sock->doClose();
            } else {
                std::fprintf(stderr, "server: echo '%.*s': %s\n",
                             static_cast<int>(count), conn->buf.data(),
                             zn_strerror(ec));
            }
        };

        auto onRecv = [&, conn, onSent](NetErrorCode ec, unsigned count) {
            if (ec != NEC_SUCCESS) return; /* peer is gone */
            std::printf("server: echo %u bytes\n", count);
            if (!conn->sock->doSend(conn->buf.data(), count, onSent))
                fail("server: cannot echo", NEC_SUCCESS);
        };

        if (!conn->sock->doRecv(&conn->buf[0], kBufSize, onRecv))
            fail("server: cannot receive", NEC_SUCCESS);
        if (accepted < kClients) accept->doAccept(onAccept);
    };
    accept->doAccept(onAccept);

    /* ---- three clients: connect, send, check the echo, close ----------- */
    for (int i = 1; i <= kClients; ++i) {
        auto conn = std::make_shared<ClientConn>();

        conn->idx = i;
        conn->sock = std::make_shared<TcpSocket>();
        conn->hello = "hello from client " + std::to_string(i);
        conn->helloLen = static_cast<unsigned>(conn->hello.size());
        conn->buf.assign(kBufSize, '\0');

        auto onRecv = [&, conn](NetErrorCode ec, unsigned count) {
            if (ec != NEC_SUCCESS) return fail("client recv", ec);
            std::string text(conn->buf.data(), count);
            std::printf("client%d: echoed: %s\n", conn->idx, text.c_str());
            if (text != conn->hello) fail("client: echo mismatch", NEC_SUCCESS);
            conn->sock->doClose();
        };

        auto onSent = [conn](NetErrorCode ec, unsigned) {
            if (ec != NEC_SUCCESS)
                std::fprintf(stderr, "client%d: send: %s\n", conn->idx,
                             zn_strerror(ec));
        };

        auto onConnect = [&, conn, onRecv, onSent](NetErrorCode ec) {
            if (ec != NEC_SUCCESS) return fail("client connect", ec);
            if (!conn->sock->doSend(conn->hello.data(), conn->helloLen, onSent))
                return fail("client send", NEC_ERROR);
            if (!conn->sock->doRecv(&conn->buf[0], kBufSize, onRecv))
                fail("client receive", NEC_ERROR);
        };

        conn->sock->initialize(loop);
        if (!conn->sock->doConnect("127.0.0.1", kPort, onConnect))
            fail("client connect",
                 static_cast<NetErrorCode>(zn_lasterror(loop->S)));
    }

    /* ---- udp ping: the server echoes the datagram back to its sender --- */
    auto udpServer = std::make_shared<UdpSocket>();
    auto udpClient = std::make_shared<UdpSocket>();
    auto udpServerBuf = std::make_shared<std::string>(kBufSize, '\0');
    auto udpClientBuf = std::make_shared<std::string>(kBufSize, '\0');

    auto onPing = [&, udpServerBuf](NetErrorCode ec, char const* addr,
                                    unsigned short port, unsigned count) {
        if (ec != NEC_SUCCESS) return fail("udp server", ec);
        std::printf("udp server: %u bytes from %s:%hu\n", count, addr, port);
        udpServer->doSendTo(udpServerBuf->data(), count, addr, port);
    };

    auto onEcho = [&, udpClientBuf](NetErrorCode ec, char const* addr,
                                    unsigned short port, unsigned count) {
        if (ec != NEC_SUCCESS) return fail("udp client", ec);
        std::string echo(udpClientBuf->data(), count);
        std::printf("udp client: %u bytes from %s:%hu: %s\n", count, addr, port,
                    echo.c_str());
        if (echo != kPing) fail("udp: echo mismatch", NEC_SUCCESS);
    };

    bool udpOk = udpServer->initialize(loop, "127.0.0.1", kUdpPort)
              && udpClient->initialize(loop, "127.0.0.1", 0)
              && udpServer->doRecvFrom(&(*udpServerBuf)[0], kBufSize, onPing)
              && udpClient->doRecvFrom(&(*udpClientBuf)[0], kBufSize, onEcho);
    if (!udpOk) {
        fail("udp setup", static_cast<NetErrorCode>(zn_lasterror(loop->S)));
        udpServer.reset(); /* drop the armed receive so the loop can go idle */
        udpClient.reset();
    } else if (!udpClient->doSendTo(kPing,
                                    static_cast<unsigned>(sizeof(kPing) - 1),
                                    "127.0.0.1", kUdpPort)) {
        fail("udp send", NEC_ERROR);
        udpServer.reset();
        udpClient.reset();
    }

    loop->runLoop(); /* returns when nothing is in flight */

    if (failed) {
        std::fprintf(stderr, "example.cpp: FAILED\n");
        return 1;
    }
    std::printf("example.cpp: OK\n");
    return 0;
}
