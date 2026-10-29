/* udp_test.c -- UDP socket semantics.
 *
 * Covers: parameter validation (unparsable address, zero length, NULL
 * callback, one in-flight recvfrom -> ZN_EBUSY), an IPv4 loopback round trip
 * with peer address/port checks, five datagrams without loss, closing while a
 * recvfrom is pending, and the ZN_MAX_UDPSEND datagram limit.
 *
 * Every socket binds "127.0.0.1"/"::1" port 0 and the bound port is read back
 * with zt_udp_port(), so the suite never uses fixed ports.  zn_sendto is
 * synchronous: only the receive path is delivered from the loop, so a test
 * arms recvfrom and sends before calling zt_run().
 *
 * The regression block at the end covers B2 (IPv6 peer address) and L3
 * (idempotent close); each test names the defect it used to reproduce.
 */

#include "ztest.h"

/* `#ifndef`, not `#if !defined(...)`: misc/gen_entries.lua evaluates #if with
 * Lua boolean semantics, where `not 0` is false, so `!defined(X)` reads as
 * never taken and would hide the test from the generated runner. */
#ifndef _WIN32
# include <fcntl.h> /* open()/fcntl() for the reused-fd regression (L3) */
#endif              /* _WIN32 */

#define MSG_COUNT 5

/* ---- shared recorder ----
 * The buffers passed to zn_recvfrom must stay valid until the deferred
 * callback runs, and the callback runs after the arming code has returned,
 * so the recorder lives at file scope (same shape as timer_test.c). */

static int         g_fired;
static unsigned    g_client_port;
static unsigned    g_server_port;
static char        g_sbuf[64];
static char        g_cbuf[64];
static const char *g_expect; /* payload expected by the connected tests */

static void reset_recorder(void) {
    g_fired = 0;
    g_client_port = 0;
    g_server_port = 0;
    memset(g_sbuf, 0, sizeof(g_sbuf));
    memset(g_cbuf, 0, sizeof(g_cbuf));
}

/* a datagram payload is not NUL terminated: compare exactly `n` bytes and
 * report the expectation at the call site */
#define assertbytes(buf, want, n)                                     \
    do {                                                              \
        if (memcmp((buf), (want), (size_t)(n)) != 0) {                \
            test_log("FAIL %s:%d: payload mismatch, expected '%s'\n", \
                     __FILE__, __LINE__, (want));                     \
            abort();                                                  \
        }                                                             \
    } while (0)

/* ---- callbacks ---- */

/* counts completions; used where the payload does not matter */
static void cb_count(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                     const char *addr, unsigned port) {
    (void)ud;
    (void)udp;
    (void)err;
    (void)count;
    (void)addr;
    (void)port;
    g_fired++;
}

static void cb_server_v4(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                         const char *addr, unsigned port) {
    (void)ud;
    asserteq(err, ZN_OK);
    asserteq(count, 5);
    assertbytes(g_sbuf, "hello", 5);
    assertstreq(addr, "127.0.0.1");
    asserteq(port, g_client_port);
    g_fired++;
    /* reply to the address the callback reported, then delete: the server has
     * nothing left to do, so the run loop is allowed to end */
    asserteq(zn_sendto(udp, "pong", 4, addr, port), ZN_OK);
    zn_deludp(udp);
}

static void cb_client_v4(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                         const char *addr, unsigned port) {
    (void)ud;
    asserteq(err, ZN_OK);
    asserteq(count, 4);
    assertbytes(g_cbuf, "pong", 4);
    assertstreq(addr, "127.0.0.1");
    asserteq(port, g_server_port);
    g_fired++;
    zn_deludp(udp);
}

/* re-arm after every datagram; the fifth one is the last */
static void cb_server_multi(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                            const char *addr, unsigned port) {
    (void)ud;
    (void)addr;
    (void)port;
    asserteq(err, ZN_OK);
    asserteq(count, 5);
    assertbytes(g_sbuf, "ding!", 5);
    g_fired++;
    if (g_fired < MSG_COUNT)
        assertok(zn_recvfrom(udp, g_sbuf, (unsigned)sizeof(g_sbuf),
                             cb_server_multi, NULL)
                 == ZN_OK);
    else
        zn_deludp(udp); /* all five arrived; nothing is left to poll */
}

/* ---- parameters ---- */

TEST(udp_params) {
    zn_State *S = zt_open();
    zn_Udp   *u;
    char      buf[64];

    reset_recorder();
    /* a string that is not an IP literal must not create an object */
    assertok(zn_newudp(S, "not-an-ip", 0) == NULL);
    /* a NULL udp is rejected before the socket is touched */
    asserteq(zn_sendto(NULL, "x", 1, "127.0.0.1", 0), ZN_EPARAM);

    u = zn_newudp(S, "127.0.0.1", 0);
    assertok(u != NULL);
    assertok(zt_udp_port(u) != 0);

    /* zero length is a parameter error in both directions */
    asserteq(zn_sendto(u, "x", 0, "127.0.0.1", 0), ZN_EPARAM);
    /* a destination that does not parse is a parameter error, not a send */
    asserteq(zn_sendto(u, "x", 1, "not-an-ip", 0), ZN_EPARAM);

    asserteq(zn_recvfrom(NULL, buf, (unsigned)sizeof(buf), cb_count, NULL),
             ZN_EPARAM);
    asserteq(zn_recvfrom(u, buf, 0, cb_count, NULL), ZN_EPARAM);
    asserteq(zn_recvfrom(u, buf, (unsigned)sizeof(buf), NULL, NULL), ZN_EPARAM);

    /* one in-flight recvfrom per socket */
    assertok(zn_recvfrom(u, buf, (unsigned)sizeof(buf), cb_count, NULL)
             == ZN_OK);
    asserteq(zn_recvfrom(u, buf, (unsigned)sizeof(buf), cb_count, NULL),
             ZN_EBUSY);

    /* zn_deludp cancels the pending receive and releases its retain, so the
     * borrowed buffer may go out of scope afterwards */
    zn_deludp(u);
    zt_close(S);
}

/* ---- IPv4 round trip ---- */

TEST(udp_send_recv_v4) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;

    reset_recorder();
    srv = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(srv != NULL && cli != NULL);
    g_server_port = zt_udp_port(srv);
    g_client_port = zt_udp_port(cli);
    assertok(g_server_port != 0 && g_client_port != 0);
    assertok(g_server_port != g_client_port);

    assertok(zn_recvfrom(srv, g_sbuf, (unsigned)sizeof(g_sbuf), cb_server_v4,
                         NULL)
             == ZN_OK);
    assertok(zn_recvfrom(cli, g_cbuf, (unsigned)sizeof(g_cbuf), cb_client_v4,
                         NULL)
             == ZN_OK);

    /* zn_sendto is synchronous, so the datagram is queued before the loop */
    asserteq(zn_sendto(cli, "hello", 5, "127.0.0.1", g_server_port), ZN_OK);

    /* both callbacks delete their udp, so the loop must terminate by itself */
    asserteq(zt_run(S), 0);
    asserteq(g_fired, 2);
    zt_close(S);
}

/* ---- several datagrams, none lost ---- */

TEST(udp_multiple_datagrams) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;
    unsigned  srv_port;
    int       i;

    reset_recorder();
    srv = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(srv != NULL && cli != NULL);
    srv_port = zt_udp_port(srv);
    assertok(srv_port != 0);

    assertok(zn_recvfrom(srv, g_sbuf, (unsigned)sizeof(g_sbuf), cb_server_multi,
                         NULL)
             == ZN_OK);
    /* every sendto completes synchronously, before the loop is entered */
    for (i = 0; i < MSG_COUNT; ++i)
        asserteq(zn_sendto(cli, "ding!", 5, "127.0.0.1", srv_port), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(g_fired, MSG_COUNT); /* no packet loss on loopback */
    /* the server saw all five, so the client may be deleted now */
    zn_deludp(cli);
    zt_close(S);
}

/* ---- connected UDP: zn_connectudp + zn_sendto(..., NULL, 0) ---- */

static void cb_conn_server(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                           const char *addr, unsigned port) {
    (void)ud;
    asserteq(err, ZN_OK);
    asserteq(count, 5);
    assertbytes(g_sbuf, "hello", 5);
    asserteq(port, g_client_port);
    g_fired++;
    asserteq(zn_sendto(udp, "pong", 4, addr, port), ZN_OK);
    zn_deludp(udp);
}

static void cb_conn_client(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                           const char *addr, unsigned port) {
    (void)ud;
    (void)addr;
    asserteq(err, ZN_OK);
    asserteq(count, 4);
    assertbytes(g_cbuf, "pong", 4);
    asserteq(port, g_server_port);
    g_fired++;
    zn_deludp(udp);
}

TEST(udp_connect_sendto_null) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;

    reset_recorder();
    srv = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(srv != NULL && cli != NULL);
    g_server_port = zt_udp_port(srv);
    g_client_port = zt_udp_port(cli);
    assertok(g_server_port != 0 && g_client_port != 0);

    assertok(zn_recvfrom(srv, g_sbuf, (unsigned)sizeof(g_sbuf), cb_conn_server,
                         NULL)
             == ZN_OK);
    assertok(zn_connectudp(cli, "127.0.0.1", g_server_port) == ZN_OK);
    assertok(zn_recvfrom(cli, g_cbuf, (unsigned)sizeof(g_cbuf), cb_conn_client,
                         NULL)
             == ZN_OK);

    /* NULL/0 is the connected send; the client's bind port is preserved */
    asserteq(zn_sendto(cli, "hello", 5, NULL, 0), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(g_fired, 2);
    zt_close(S);
}

/* ---- repeat zn_connectudp changes the default peer ---- */

static void cb_repeat_server(void *ud, zn_Udp *udp, unsigned err,
                             unsigned count, const char *addr, unsigned port) {
    (void)ud;
    (void)addr;
    (void)port;
    asserteq(err, ZN_OK);
    asserteq(count, (unsigned)strlen(g_expect));
    assertbytes(g_sbuf, g_expect, (unsigned)strlen(g_expect));
    g_fired++;
    zn_deludp(udp);
}

TEST(udp_connect_repeat) {
    zn_State *S = zt_open();
    zn_Udp   *a, *b, *cli;
    unsigned  a_port, b_port;

    reset_recorder();
    a = zn_newudp(S, "127.0.0.1", 0);
    b = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(a != NULL && b != NULL && cli != NULL);
    a_port = zt_udp_port(a);
    b_port = zt_udp_port(b);
    assertok(a_port != 0 && b_port != 0);

    g_expect = "one";
    assertok(zn_recvfrom(a, g_sbuf, (unsigned)sizeof(g_sbuf), cb_repeat_server,
                         NULL)
             == ZN_OK);
    assertok(zn_connectudp(cli, "127.0.0.1", a_port) == ZN_OK);
    asserteq(zn_sendto(cli, "one", 3, NULL, 0), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_fired, 1);

    g_expect = "two";
    assertok(zn_recvfrom(b, g_sbuf, (unsigned)sizeof(g_sbuf), cb_repeat_server,
                         NULL)
             == ZN_OK);
    assertok(zn_connectudp(cli, "127.0.0.1", b_port) == ZN_OK);
    asserteq(zn_sendto(cli, "two", 3, NULL, 0), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_fired, 2);

    zn_deludp(cli);
    zt_close(S);
}

/* ---- connected-send parameter/state errors ---- */

TEST(udp_connect_errors) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    zn_Udp   *peer = zn_newudp(S, "127.0.0.1", 0);
    unsigned  port;

    assertok(u != NULL && peer != NULL);
    port = zt_udp_port(peer);
    assertok(port != 0);

    /* no destination and not connected: send() reports the missing peer */
    asserteq(zn_sendto(u, "x", 1, NULL, 0), ZN_ESTATE);
    /* NULL address is only meaningful with port 0 */
    asserteq(zn_sendto(u, "x", 1, NULL, 1), ZN_EPARAM);
    asserteq(zn_connectudp(NULL, "127.0.0.1", port), ZN_EPARAM);
    asserteq(zn_connectudp(u, NULL, 0), ZN_EPARAM);

    /* a v4 socket cannot connect to a v6 literal; connect() rejects it */
    asserteq(zn_connectudp(u, "::1", port), ZN_EPARAM);

    assertok(zn_connectudp(u, "127.0.0.1", port) == ZN_OK);
    asserteq(zn_sendto(u, "x", 1, NULL, 0), ZN_OK);

    zn_closeudp(u);
    asserteq(zn_connectudp(u, "127.0.0.1", port), ZN_ESTATE);
    asserteq(zn_sendto(u, "x", 1, NULL, 0), ZN_ESTATE);

    zn_deludp(u);
    zn_deludp(peer);
    zt_close(S);
}

/* ---- connect while a recvfrom is armed ---- */

static void cb_pending_client(void *ud, zn_Udp *udp, unsigned err,
                              unsigned count, const char *addr, unsigned port) {
    (void)ud;
    (void)addr;
    asserteq(err, ZN_OK);
    asserteq(count, 2);
    assertbytes(g_cbuf, "hi", 2);
    asserteq(port, g_server_port);
    g_fired++;
    zn_deludp(udp);
}

TEST(udp_connect_pending_recv) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;

    reset_recorder();
    srv = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(srv != NULL && cli != NULL);
    g_server_port = zt_udp_port(srv);
    g_client_port = zt_udp_port(cli);
    assertok(g_server_port != 0 && g_client_port != 0);

    assertok(zn_recvfrom(cli, g_cbuf, (unsigned)sizeof(g_cbuf),
                         cb_pending_client, NULL)
             == ZN_OK);
    /* changing the peer while the recvfrom is pending is allowed */
    assertok(zn_connectudp(cli, "127.0.0.1", g_server_port) == ZN_OK);
    asserteq(zn_sendto(srv, "hi", 2, "127.0.0.1", g_client_port), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(g_fired, 1);
    zn_deludp(srv);
    zt_close(S);
}

/* ---- close with a pending recvfrom ---- */

TEST(udp_close_pending_recv) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    zt_Loop   L;

    reset_recorder();
    assertok(u != NULL);
    assertok(zn_recvfrom(u, g_sbuf, (unsigned)sizeof(g_sbuf), cb_count, NULL)
             == ZN_OK);

    /* close reports ZN_ERROR only when the syscall fails; the cancellation
     * of the pending recvfrom is what this test is about, so the return
     * value is not asserted here */
    zn_closeudp(u);
    zn_deludp(u);

    /* nothing may fire after close; the tick timer only bounds each poll */
    zt_begin(&L, S, 1000);
    zt_wait(&L, 60);
    zt_end(&L);

    asserteq(g_fired, 0);
    zt_close(S);
}

/* ---- zn_sendto length limit ---- */

TEST(udp_sendto_max_payload) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;
    char      payload[ZN_MAX_UDPSEND + 1];
    unsigned  srv_port;

    reset_recorder();
    memset(payload, 'x', sizeof(payload));
    srv = zn_newudp(S, "127.0.0.1", 0);
    cli = zn_newudp(S, "127.0.0.1", 0);
    assertok(srv != NULL && cli != NULL);
    srv_port = zt_udp_port(srv);
    assertok(srv_port != 0);

    /* ZN_MAX_UDPSEND is the documented compile-time limit (default 1200, one
     * IPv6-minimum-MTU datagram): a bound peer must accept it, and one byte
     * over is rejected as a parameter error before the socket is touched */
    asserteq(zn_sendto(cli, payload, ZN_MAX_UDPSEND, "127.0.0.1", srv_port),
             ZN_OK);
    asserteq(zn_sendto(cli, payload, ZN_MAX_UDPSEND + 1, "127.0.0.1", srv_port),
             ZN_EPARAM);

    zn_deludp(cli);
    zn_deludp(srv);
    zt_close(S);
}

/* =====================================================================
 * REGRESSIONS -- tests for library bugs that used to fail.
 *
 * An assert aborts the process, so these tests must stay last: the default
 * `just test udp` runs every test in one process, while `just check` (and
 * `just test udp <name>`) starts one process per test and shows all of them.
 * ===================================================================== */

/* ---- B2: kqueue reported a garbage IPv6 peer address ------------------
 * znB_onrecvfrom used to call memset(&remote_addr, 0,
 * addr_size = sizeof(&remote_addr)): addr_size became the size of a pointer
 * (8), not of zn_SockAddr (28), so recvfrom() truncated the sockaddr_in6 and
 * the peer address was uninitialized stack memory.  IPv4 survived because 8
 * bytes happen to cover sockaddr_in's family/port/address; IPv6 did not.
 * The test now pins a peer address of exactly "::1" and the client's port,
 * on every backend. */

static void cb_server_v6(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                         const char *addr, unsigned port) {
    (void)ud;
    asserteq(err, ZN_OK);
    asserteq(count, 5);
    assertbytes(g_sbuf, "hello", 5);
    asserteq(port, g_client_port);
    assertstreq(addr, "::1");
    g_fired++;
    asserteq(zn_sendto(udp, "pong", 4, addr, port), ZN_OK);
    zn_deludp(udp);
}

static void cb_client_v6(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                         const char *addr, unsigned port) {
    (void)ud;
    asserteq(err, ZN_OK);
    asserteq(count, 4);
    assertbytes(g_cbuf, "pong", 4);
    asserteq(port, g_server_port);
    assertstreq(addr, "::1");
    g_fired++;
    zn_deludp(udp);
}

TEST(udp_ipv6_loopback) {
    zn_State *S = zt_open();
    zn_Udp   *srv, *cli;

    reset_recorder();
    srv = zn_newudp(S, "::1", 0);
    if (srv == NULL) {
        test_skip("host has no IPv6 loopback: zn_newudp(\"::1\") failed");
        zt_close(S);
        return;
    }
    cli = zn_newudp(S, "::1", 0);
    if (cli == NULL) {
        test_skip("host has no IPv6 loopback: second zn_newudp(\"::1\")");
        zn_deludp(srv);
        zt_close(S);
        return;
    }
    g_server_port = zt_udp_port(srv);
    g_client_port = zt_udp_port(cli);
    assertok(g_server_port != 0 && g_client_port != 0);

    assertok(zn_recvfrom(srv, g_sbuf, (unsigned)sizeof(g_sbuf), cb_server_v6,
                         NULL)
             == ZN_OK);
    assertok(zn_recvfrom(cli, g_cbuf, (unsigned)sizeof(g_cbuf), cb_client_v6,
                         NULL)
             == ZN_OK);

    asserteq(zn_sendto(cli, "hello", 5, "::1", g_server_port), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(g_fired, 2);
    zt_close(S);
}

/* A second UDP socket cannot bind an address:port that is already taken;
 * zn_newudp() must report EBIND by returning NULL (znB_initudp failure). */
TEST(udp_bind_conflict) {
    zn_State *S = zt_open();
    zn_Udp   *a = zn_newudp(S, "127.0.0.1", 0);
    zn_Udp   *b;
    unsigned  port;

    assertok(a != NULL);
    port = zt_udp_port(a);
    assertok(port != 0);
    b = zn_newudp(S, "127.0.0.1", port);
    assertok(b == NULL);
    zn_deludp(a);
    zt_close(S);
}

/* ---- L3: close invalidates udp->socket and is idempotent ----
 * zn_closeudp() (public layer) refuses to enter the backend once
 * the fd is gone; each backend still performs its own state transition after
 * the OS close.  A second zn_closeudp(), and zn_deludp() right after the
 * first, must not close a descriptor number the process may have reused.
 * The same invalidation is what makes zn_sendto()'s ZN_ESTATE guard work
 * (see udp_sendto_after_close).  The unrelated-fd half is
 * udp_close_reused_fd, which needs open() and is POSIX only. */

TEST(udp_double_close) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);

    assertok(u != NULL);
    assertok(u->socket != ZN_INVALID_SOCKET);

    /* close may report ZN_ERROR on a real syscall failure, so only the
     * state change is asserted here */
    zn_closeudp(u);
    asserteq(u->socket, ZN_INVALID_SOCKET);

    /* close on an already closed udp is a no-op that reports success */
    asserteq(zn_closeudp(u), ZN_OK);

    /* nothing is left to close, so zn_deludp must not touch any fd */
    zn_deludp(u);
    zt_close(S);
}

/* ---- L3: a closed udp must be unusable for sending ----
 * zn_sendto() checks `udp->socket == ZN_INVALID_SOCKET` (ZN_ESTATE); close
 * now invalidates the field, so the dead descriptor is never used. */

TEST(udp_sendto_after_close) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    zn_Udp   *peer = zn_newudp(S, "127.0.0.1", 0);
    unsigned  peer_port;

    assertok(u != NULL && peer != NULL);
    peer_port = zt_udp_port(peer);
    assertok(peer_port != 0);

    zn_closeudp(u);
    asserteq(zn_sendto(u, "hello", 5, "127.0.0.1", peer_port), ZN_ESTATE);

    zn_deludp(u);
    zn_deludp(peer);
    zt_close(S);
}

/* ---- B3: a failed sendto must not come back as ZN_OK ----
 * The socket is bound to IPv4, so sending to an IPv6 address fails inside
 * sendto() (EINVAL on macOS, EAFNOSUPPORT on Linux, WSAEAFNOSUPPORT on
 * Windows) without depending on routes or on a peer. */

TEST(udp_sendto_error) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    int       ret;

    assertok(u != NULL);
    ret = zn_sendto(u, "x", 1, "::1", 9);
    test_log("  sendto v6 on a v4 socket: %d (%s)\n", ret, zn_strerror(ret));
    asserteq(ret, ZN_ERROR);

    zn_deludp(u);
    zt_close(S);
}

/* ---- L3 (POSIX): zn_deludp must not close a reused fd ----
 * After zn_closeudp() the descriptor number is free; the process reuses it
 * at once.  zn_deludp() goes through zn_closeudp() again, which sees
 * ZN_INVALID_SOCKET and leaves the new owner alone. */

#ifndef _WIN32

TEST(udp_close_reused_fd) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    int       oldfd, newfd;

    assertok(u != NULL);
    oldfd = u->socket;
    assertok(oldfd != ZN_INVALID_SOCKET);

    zn_closeudp(u);
    /* the lowest free descriptor is the one just closed */
    newfd = open("/dev/null", O_RDONLY);
    assertok(newfd >= 0);
    if (newfd != oldfd) {
        test_skip("fd %d was not reused (open returned %d)", oldfd, newfd);
        close(newfd);
        zn_deludp(u);
        zt_close(S);
        return;
    }

    zn_deludp(u); /* must leave the reused descriptor alone */
    assertok(fcntl(newfd, F_GETFD) != -1);

    close(newfd);
    zt_close(S);
}

#endif /* _WIN32 */

/* ---- B7 (POSIX): a UDP socket must not block the event loop ----
 * Both POSIX backends only poll for readiness; a spurious readiness followed
 * by a blocking recvfrom() would stall every other socket in the state. */

#ifndef _WIN32

TEST(udp_socket_is_nonblocking) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    int       flags;

    assertok(u != NULL);
    flags = fcntl(u->socket, F_GETFL);
    assertok(flags != -1);
    assertok((flags & O_NONBLOCK) != 0);

    zn_deludp(u);
    zt_close(S);
}

#endif /* _WIN32 */

/* ---- L7 (POSIX): a spurious readiness report on UDP ---------------------
 * Same rule as accept: "readable" is derived from the receive queue, so
 * recvfrom() returning EAGAIN means the report and the queue disagreed.  It
 * used to disarm the pending recvfrom silently, after which every later
 * datagram was lost. */

#ifndef _WIN32

typedef struct sp_ctx {
    int ok;
    int err;
} sp_ctx;

static void sp_on_recvfrom(void *ud, zn_Udp *udp, unsigned err, unsigned count,
                           const char *addr, unsigned port) {
    sp_ctx *c = (sp_ctx *)ud;
    (void)udp;
    (void)count;
    (void)addr;
    (void)port;
    if (err == ZN_OK)
        c->ok++;
    else
        c->err++;
}

static int sp_udp_socket(unsigned port) {
    struct sockaddr_in sa;
    int                fd = socket(AF_INET, SOCK_DGRAM, 0);
    assertok(fd >= 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = htonl(0x7f000001u);
    asserteq((int)sendto(fd, "x", 1, 0, (const struct sockaddr *)&sa,
                         (socklen_t)sizeof(sa)),
             1);
    return fd;
}

TEST(udp_recvfrom_spurious_ready) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    sp_ctx    c;
    zt_Loop   L;
    unsigned  port;
    int       s2;

    assertok(u != NULL);
    memset(&c, 0, sizeof(c));
    port = zt_udp_port(u);
    asserteq(zn_recvfrom(u, g_sbuf, (unsigned)sizeof(g_sbuf), sp_on_recvfrom,
                         &c),
             ZN_OK);
    asserteq(S->waitings, 1);

    zt_dispatch_udp(u, 0); /* no datagram pending: recvfrom() -> EAGAIN */

    asserteq(c.err, 1);
    asserteq(c.ok, 0);
    asserteq(S->waitings, 0);
    asserteq(zn_recvfrom(u, g_sbuf, (unsigned)sizeof(g_sbuf), sp_on_recvfrom,
                         &c),
             ZN_OK); /* still usable */

    s2 = sp_udp_socket(port);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, c.ok == 1);
    zt_end(&L);
    asserteq(c.ok, 1);

    close(s2);
    zn_deludp(u);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* an error event on the UDP socket: the pending recvfrom is consumed with
 * ZN_ERROR and the socket stays open for a re-arm (it was already like that,
 * this pins the shape the accept side now shares). */
TEST(udp_recvfrom_error_event) {
    zn_State *S = zt_open();
    zn_Udp   *u = zn_newudp(S, "127.0.0.1", 0);
    sp_ctx    c;
    zt_Loop   L;
    unsigned  port;
    int       s2;

    assertok(u != NULL);
    memset(&c, 0, sizeof(c));
    port = zt_udp_port(u);
    asserteq(zn_recvfrom(u, g_sbuf, (unsigned)sizeof(g_sbuf), sp_on_recvfrom,
                         &c),
             ZN_OK);

    zt_dispatch_udp(u, 1);
    asserteq(c.err, 1);
    asserteq(c.ok, 0);
    asserteq(S->waitings, 0);
    asserteq(zn_recvfrom(u, g_sbuf, (unsigned)sizeof(g_sbuf), sp_on_recvfrom,
                         &c),
             ZN_OK); /* still usable */

    s2 = sp_udp_socket(port);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, c.ok == 1);
    zt_end(&L);
    asserteq(c.ok, 1);

    close(s2);
    zn_deludp(u);
    asserteq(S->waitings, 0);
    zt_close(S);
}

#endif /* _WIN32 */

#include "udp_test.gen.inc"
