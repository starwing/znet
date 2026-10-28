/* tcp_test.c -- TCP state guards, lifecycle and error paths.
 *
 * Covers: parameter/state guards, the four TCP callbacks (connect, accept,
 * send, recv), IPv4 and IPv6 loopback echo, repeated timer-driven echo,
 * refused/immediate connect failures, per-direction EBUSY, send/recv after
 * close, close with a pending recv, reconnect after close, close from each
 * callback, peer close (ZN_ECLOSED), 10 back-to-back messages, the L2
 * parked-send regressions, and the select/kqueue-visible I/O error paths
 * (EPIPE, RST, OOB).
 *
 * All tests are expected to pass.  The L2 tests sit in their own guarded
 * section because they are white box and reactive-only; run them through the
 * per-test runner (`just check` / the justfile "run" recipe) so one abort
 * cannot hide the rest.
 *
 * Backend selection: znet.h #undefs its internal zn_use_* selectors after
 * using them, so a test file cannot test those.  The result-queue (reactive)
 * backends are "POSIX and no ZN_USE_SELECT"; zn_have_kqueue and ZN_USE_SELECT
 * both survive znet.h and misc/gen_entries.lua is given the same platform
 * defines as the compiler (see tests/README.md, "conditional compilation").
 */

#include "ztest.h"

/* ---- callbacks for tests that only exercise synchronous guards ---- */

static void cb_noop_accept(void *ud, zn_Accept *accept, unsigned err,
                           zn_Tcp *tcp) {
    (void)ud;
    (void)accept;
    (void)err;
    (void)tcp;
}

static void cb_noop_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    (void)ud;
    (void)tcp;
    (void)err;
}

static void cb_noop_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
}

static void cb_noop_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
}

/* set an int flag; the ud is the flag itself, so tcp_params needs no ctx */
static void cb_flag_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    int *flag = (int *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    *flag = 1;
}

/* ---- parameter and state guards ---- */

TEST(tcp_params) {
    zn_State  *S = zt_open();
    zt_Loop    L;
    zn_Tcp    *tcp;
    zn_Accept *accept;
    char       buf[16];
    int        connected = 0;
    unsigned   port;

    /* NULL object guards */
    asserteq(zn_connect(NULL, "127.0.0.1", 1, 0, cb_noop_connect, NULL),
             ZN_EPARAM);
    asserteq(zn_send(NULL, "x", 1, cb_noop_send, NULL), ZN_EPARAM);
    asserteq(zn_recv(NULL, buf, 1, cb_noop_recv, NULL), ZN_EPARAM);
    asserteq(zn_listen(NULL, "127.0.0.1", 0), ZN_EPARAM);
    asserteq(zn_accept(NULL, cb_noop_accept, NULL), ZN_EPARAM);

    tcp = zn_newtcp(S);
    accept = zn_newaccept(S, 0);
    assertok(tcp != NULL && accept != NULL);

    /* NULL callback and zero length guards */
    asserteq(zn_connect(tcp, "127.0.0.1", 1, 0, NULL, NULL), ZN_EPARAM);
    asserteq(zn_send(tcp, "x", 1, NULL, NULL), ZN_EPARAM);
    asserteq(zn_send(tcp, "x", 0, cb_noop_send, NULL), ZN_EPARAM);
    asserteq(zn_recv(tcp, buf, sizeof(buf), NULL, NULL), ZN_EPARAM);
    asserteq(zn_recv(tcp, buf, 0, cb_noop_recv, NULL), ZN_EPARAM);
    asserteq(zn_accept(accept, NULL, NULL), ZN_EPARAM);

    /* unparsable address strings */
    asserteq(zn_connect(tcp, "", 1, 0, cb_noop_connect, NULL), ZN_EPARAM);
    asserteq(zn_listen(accept, "", 0), ZN_EPARAM);

    /* accept before listen */
    asserteq(zn_accept(accept, cb_noop_accept, NULL), ZN_ESTATE);

    /* listen twice */
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(accept);
    assertok(port != 0);
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_ESTATE);

    /* zn_connect on an established tcp is a state error.  The connect has
     * to complete for this: while it is still in flight the same guard
     * returns ESTATE too, but the contract there is EBUSY (see
     * tcp_connect_twice). */
    asserteq(zn_connect(tcp, "127.0.0.1", port, 0, cb_flag_connect,
                        &connected), ZN_OK);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, connected == 1);
    zt_end(&L);
    asserteq(zn_connect(tcp, "127.0.0.1", port, 0, cb_noop_connect, NULL),
             ZN_ESTATE);

    /* the delete helpers are no-ops on NULL */
    zn_deltcp(NULL);
    zn_delaccept(NULL);

    zn_deltcp(tcp);
    zn_delaccept(accept);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* ---- loopback echo, IPv4 and IPv6 ---- */

typedef struct echo_ctx {
    zn_State   *S;
    zn_Accept  *listener;
    zn_Tcp     *client;
    zn_Tcp     *server;
    const char *addr;
    unsigned    port;        /* listener port */
    unsigned    client_port; /* client's ephemeral local port */
    zn_PeerInfo client_peer; /* what the client sees: the listener */
    zn_PeerInfo server_peer; /* what the accepted side sees: the client */
    char        cbuf[16];
    char        sbuf[16];
    int         connected;
    int         accepted;
    int         client_sent;
    int         server_recv;
    int         server_sent;
    int         client_recv;
} echo_ctx;

static void echo_srv_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    c->server_sent = 1;
    /* the echo is out: the server side is done, close and delete it */
    zn_deltcp(tcp);
    c->server = NULL;
}

static void echo_srv_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    assertok(memcmp(c->sbuf, "ping", 4) == 0);
    c->server_recv = 1;
    asserteq(zn_send(tcp, c->sbuf, cnt, echo_srv_send, c), ZN_OK);
}

static void echo_accept(void *ud, zn_Accept *accept, unsigned err,
                        zn_Tcp *tcp) {
    echo_ctx *c = (echo_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    zn_getpeerinfo(tcp, &c->server_peer);
    asserteq(zn_recv(tcp, c->sbuf, sizeof(c->sbuf), echo_srv_recv, c), ZN_OK);
}

static void echo_cli_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    c->client_sent = 1;
}

static void echo_cli_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    assertok(memcmp(c->cbuf, "ping", 4) == 0);
    c->client_recv = 1;
    /* both peer infos are known now: the accepted side must have seen the
     * client's ephemeral port and the same loopback address. */
    asserteq(c->server_peer.port, c->client_port);
    assertstreq(c->server_peer.addr, c->addr);
    assertstreq(c->client_peer.addr, c->addr);
    asserteq(c->client_peer.port, c->port);
    zn_deltcp(tcp);
    c->client = NULL;
    zn_delaccept(c->listener);
    c->listener = NULL;
}

static void echo_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    c->client_port = zt_tcp_port(tcp);
    assertok(c->client_port != 0);
    zn_getpeerinfo(tcp, &c->client_peer);
    /* the client's peer info is written by zn_connect itself */
    assertok(zt_peer_is(&tcp->peer_info, c->addr, c->port));
    asserteq(zn_send(tcp, "ping", 4, echo_cli_send, c), ZN_OK);
    asserteq(zn_recv(tcp, c->cbuf, sizeof(c->cbuf), echo_cli_recv, c), ZN_OK);
}

/* returns 0 when the host cannot listen on `addr` (no IPv6 loopback) */
static int echo_run(const char *addr) {
    echo_ctx c;
    int      ret;

    memset(&c, 0, sizeof(c));
    c.addr = addr;
    c.S = zt_open();
    c.listener = zn_newaccept(c.S, 0);
    assertok(c.listener != NULL);

    ret = zn_listen(c.listener, addr, 0);
    if (ret != ZN_OK) {
        zn_delaccept(c.listener);
        c.listener = NULL;
        zt_close(c.S);
        return 0;
    }
    c.port = zt_accept_port(c.listener);
    assertok(c.port != 0);
    asserteq(zn_accept(c.listener, echo_accept, &c), ZN_OK);

    c.client = zn_newtcp(c.S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, addr, c.port, 0, echo_connect, &c), ZN_OK);

    asserteq(zt_run(c.S), 0);
    asserteq(c.connected, 1);
    asserteq(c.accepted, 1);
    asserteq(c.client_sent, 1);
    asserteq(c.server_recv, 1);
    asserteq(c.server_sent, 1);
    asserteq(c.client_recv, 1);
    asserteq(c.S->waitings, 0);
    zt_close(c.S);
    return 1;
}

TEST(tcp_connect_echo) { assertok(echo_run("127.0.0.1") == 1); }

TEST(tcp_ipv6_echo) {
    if (!echo_run("::1")) test_skip("no IPv6 loopback");
}

/* ---- repeated echo server (migrated from the old test.c smoke program) ----
 * A timer opens one client connection per tick; the server re-arms accept
 * after every connection and echoes the payload back.  When the last
 * connection has been started the timer deletes the listener while an accept
 * is still pending -- exactly what test.c did before it was retired.  The
 * loop can only return once every echo has been delivered and every object
 * has been deleted, so this also asserts the waitings accounting. */

#define ECHO_LOOP_ROUNDS 4

typedef struct echo_loop echo_loop;
typedef struct echo_conn echo_conn;

struct echo_conn {
    echo_loop *loop;
    zn_Tcp    *tcp;
    char       sbuf[128];
    char       rbuf[128];
};

struct echo_loop {
    zn_State  *S;
    zn_Accept *listener;
    zn_Timer  *timer;
    unsigned   port;
    int        started;
    int        connects;
    int        accepts;
    int        sends;
    int        echoes;
};

static void echo_loop_cli_recv(void *ud, zn_Tcp *tcp, unsigned err,
                               unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->sbuf));
    assertok(memcmp(c->rbuf, c->sbuf, sizeof(c->sbuf)) == 0);
    c->loop->echoes++;
    zn_deltcp(tcp);
    free(c);
}

static void echo_loop_cli_send(void *ud, zn_Tcp *tcp, unsigned err,
                               unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->sbuf));
    c->loop->sends++;
}

static void echo_loop_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    c->loop->connects++;
    asserteq(zn_send(tcp, c->sbuf, sizeof(c->sbuf), echo_loop_cli_send, c),
             ZN_OK);
    asserteq(zn_recv(tcp, c->rbuf, sizeof(c->rbuf), echo_loop_cli_recv, c),
             ZN_OK);
}

static void echo_loop_srv_send(void *ud, zn_Tcp *tcp, unsigned err,
                               unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->rbuf));
    zn_deltcp(tcp);
    free(c);
}

static void echo_loop_srv_recv(void *ud, zn_Tcp *tcp, unsigned err,
                               unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->rbuf));
    asserteq(zn_send(tcp, c->rbuf, cnt, echo_loop_srv_send, c), ZN_OK);
}

static void echo_loop_accept(void *ud, zn_Accept *accept, unsigned err,
                             zn_Tcp *tcp) {
    echo_loop *l = (echo_loop *)ud;
    echo_conn *c;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c = (echo_conn *)malloc(sizeof(*c));
    assertok(c != NULL);
    memset(c, 0, sizeof(*c));
    c->loop = l;
    c->tcp = tcp;
    l->accepts++;
    /* the timer may already have deleted the listener with this accept in
     * flight; in that case there is nothing left to re-arm */
    if (l->listener != NULL)
        asserteq(zn_accept(l->listener, echo_loop_accept, l), ZN_OK);
    asserteq(zn_recv(tcp, c->rbuf, sizeof(c->rbuf), echo_loop_srv_recv, c),
             ZN_OK);
}

static zn_Time echo_loop_tick(void *ud, zn_Timer *timer, zn_Time elapsed) {
    echo_loop *l = (echo_loop *)ud;
    echo_conn *c;
    (void)elapsed;

    if (l->started == ECHO_LOOP_ROUNDS) {
        if (l->listener != NULL) {
            zn_delaccept(l->listener);
            l->listener = NULL;
        }
        zn_deltimer(timer);
        l->timer = NULL;
        return 0;
    }

    c = (echo_conn *)malloc(sizeof(*c));
    assertok(c != NULL);
    memset(c, 0, sizeof(*c));
    c->loop = l;
    memset(c->sbuf, 'a' + l->started, sizeof(c->sbuf));
    c->tcp = zn_newtcp(l->S);
    if (c->tcp == NULL) {
        free(c);
        zt_fail("zn_newtcp() returned NULL");
    }
    asserteq(zn_connect(c->tcp, "127.0.0.1", l->port, 0, echo_loop_connect,
                        c), ZN_OK);
    l->started++;
    return 20;
}

TEST(tcp_echo_loop) {
    echo_loop l;

    memset(&l, 0, sizeof(l));
    l.S = zt_open();
    l.listener = zn_newaccept(l.S, 0);
    assertok(l.listener != NULL);
    asserteq(zn_listen(l.listener, "127.0.0.1", 0), ZN_OK);
    l.port = zt_accept_port(l.listener);
    assertok(l.port != 0);
    asserteq(zn_accept(l.listener, echo_loop_accept, &l), ZN_OK);

    l.timer = zn_newtimer(l.S, echo_loop_tick, &l);
    assertok(l.timer != NULL);
    asserteq(zn_starttimer(l.timer, 10), ZN_OK);

    asserteq(zt_run(l.S), 0);
    asserteq(l.started, ECHO_LOOP_ROUNDS);
    asserteq(l.connects, ECHO_LOOP_ROUNDS);
    asserteq(l.accepts, ECHO_LOOP_ROUNDS);
    asserteq(l.sends, ECHO_LOOP_ROUNDS);
    asserteq(l.echoes, ECHO_LOOP_ROUNDS);
    asserteq(l.listener == NULL, 1);
    asserteq(l.timer == NULL, 1);
    asserteq(l.S->waitings, 0);
    zt_close(l.S);
}

/* ---- I/O error paths (POSIX: RST / half-close) -------------------------
 * The API's cleanup branches after a failed send/recv are only reachable on a
 * socket that already errored.  A peer that closes with SO_LINGER 0 makes the
 * kernel deliver RST; shutdown(SHUT_WR) makes the next send fail immediately.
 * Both are deterministic, unlike a timeout. */

#if !defined(_WIN32)

typedef struct ioerr_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[8];
    int        accepted;
    int        connected;
    int        done;
    unsigned   err;
} ioerr_ctx;

static void ioerr_accept(void *ud, zn_Accept *accept, unsigned err,
                         zn_Tcp *tcp) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
}

/* linger 0 turns close() into RST instead of FIN.  Called only after the
 * client's connect completed: resetting from the accept callback races with
 * the connect result (Linux select can then report the connect as failed). */
static void ioerr_rst_peer(ioerr_ctx *c) {
    struct linger lg;

    if (c->server == NULL) return;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    assertok(setsockopt(c->server->socket, SOL_SOCKET, SO_LINGER,
                        (const char *)&lg, sizeof(lg)) == 0);
    zn_deltcp(c->server);
    c->server = NULL;
}

static void ioerr_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    c->connected = 1;
}

static void ioerr_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

static void ioerr_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

/* spin the loop until the client fd has a pending event, without consuming
 * it: this is how the test waits for the RST to arrive */
static int ioerr_wait_readable(zn_State *S, int fd, unsigned ms) {
    zn_Time end = zn_time() + ms;
    while (zn_time() < end) {
        fd_set         rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0) return 1;
        (void)zn_run(S, ZN_RUN_ONCE);
    }
    return 0;
}

static void ioerr_open(ioerr_ctx *c, zn_AcceptHandler *accept_cb,
                       zn_ConnectHandler *connect_cb) {
    memset(c, 0, sizeof(*c));
    c->S = zt_open();
    c->listener = zn_newaccept(c->S, 0);
    assertok(c->listener != NULL);
    asserteq(zn_listen(c->listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c->listener, accept_cb, c), ZN_OK);
    c->client = zn_newtcp(c->S);
    assertok(c->client != NULL);
    asserteq(zn_connect(c->client, "127.0.0.1", zt_accept_port(c->listener),
                        0, connect_cb, c), ZN_OK);
}

static void ioerr_close(ioerr_ctx *c) {
    if (c->client != NULL) zn_deltcp(c->client);
    if (c->server != NULL) zn_deltcp(c->server);
    zn_delaccept(c->listener);
    zt_close(c->S);
}

/* A send after the peer is gone must reach the callback with an error, not
 * with ZN_OK/count.  shutdown(SHUT_WR) makes send() fail with EPIPE on the
 * next attempt, so no timing is involved. */
TEST(tcp_send_error) {
    ioerr_ctx        c;
    zt_Loop          L;
    struct sigaction sa, old;

    /* the suite compiles with -D_XOPEN_SOURCE=600, which hides
     * SO_NOSIGPIPE on macOS, so the library's SIGPIPE guard is compiled out;
     * ignore it locally or the expected EPIPE would kill the test process */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    assertok(sigaction(SIGPIPE, &sa, &old) == 0);

    ioerr_open(&c, ioerr_accept, ioerr_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);

    assertok(shutdown(c.client->socket, SHUT_WR) == 0);
    asserteq(zn_send(c.client, "x", 1, ioerr_send, &c), ZN_OK);
    ZT_WAIT(L, c.done == 1);
    zt_end(&L);

    assertok(sigaction(SIGPIPE, &old, NULL) == 0);
    test_log("  send after shutdown: err=%u (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* After an RST, recv() reports ECONNRESET; that must surface through the
 * callback's error argument. */
TEST(tcp_recv_error) {
    ioerr_ctx c;
    zt_Loop   L;

    ioerr_open(&c, ioerr_accept, ioerr_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);

    /* reset only now, so the connect result is already delivered */
    ioerr_rst_peer(&c);
    assertok(ioerr_wait_readable(c.S, c.client->socket, 2000));

    asserteq(zn_recv(c.client, c.buf, sizeof(c.buf), ioerr_recv, &c), ZN_OK);
    ZT_WAIT(L, c.done == 1);
    zt_end(&L);

    test_log("  recv after RST: err=%u (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* select reports TCP urgent data through exceptfds, not readfds.  A pending
 * recv must be completed with an error instead of hanging (select-only: the
 * kqueue/epoll paths get EV_OOBAND/EPOLLPRI and are not modelled here). */
#if defined(ZN_USE_SELECT)

static void oob_accept(void *ud, zn_Accept *accept, unsigned err, zn_Tcp *tcp) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
}

static void oob_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

static void oob_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), oob_recv, c), ZN_OK);
}

TEST(tcp_select_oob_recv_error) {
    ioerr_ctx c;
    zt_Loop   L;

    ioerr_open(&c, oob_accept, oob_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);

    assertok(send(c.server->socket, "!", 1, MSG_OOB) == 1);
    ZT_WAIT(L, c.done == 1);
    zt_end(&L);

    test_log("  recv with OOB pending: err=%u (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* The same except set must also complete a pending *send* with an error.
 * Reaching this through select() needs a full kernel send buffer plus urgent
 * data (timing-dependent); dispatch the except set directly instead.  The
 * transport half is already proven by tcp_select_oob_recv_error. */
static void oobsend_accept(void *ud, zn_Accept *accept, unsigned err,
                           zn_Tcp *tcp) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
}

static void oobsend_sent(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    c->server = NULL;
    zn_deltcp(tcp);
}

TEST(tcp_select_except_send_error) {
    ioerr_ctx c;
    zt_Loop   L;

    ioerr_open(&c, oobsend_accept, ioerr_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);

    asserteq(zn_send(c.server, "x", 1, oobsend_sent, &c), ZN_OK);
    /* setno 2 == exceptfds: the send must fail loudly, not vanish */
    znP_dispatch(c.S, c.server->socket, 2);

    asserteq(c.done, 1);
    test_log("  send dispatched from exceptfds: err=%u (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    assertok(c.server == NULL);
    asserteq(c.S->waitings, 0);
    zt_end(&L);
    ioerr_close(&c);
}

#endif /* ZN_USE_SELECT */

#endif /* !_WIN32 */

/* ---- per-direction single in-flight operation ---- */

typedef struct busy_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       rbuf[8];
    int        accepted;
    int        connected;
    int        send_armed;
    int        send_busy;
    int        recv_armed;
    int        recv_busy;
    int        client_sent;
    int        client_recv;
} busy_ctx;

static void busy_srv_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
}

static void busy_accept(void *ud, zn_Accept *accept, unsigned err,
                        zn_Tcp *tcp) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    /* one byte so the client's pending recv completes and can be freed */
    asserteq(zn_send(tcp, "x", 1, busy_srv_send, c), ZN_OK);
}

static void busy_cli_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    assertok(memcmp(c->rbuf, "x", 1) == 0);
    c->client_recv = 1;
}

static void busy_cli_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    c->client_sent = 1;
}

static void busy_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    busy_ctx *c = (busy_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    c->send_armed = (zn_send(tcp, "y", 1, busy_cli_send, c) == ZN_OK);
    c->send_busy = (zn_send(tcp, "z", 1, cb_noop_send, NULL) == ZN_EBUSY);
    c->recv_armed = (zn_recv(tcp, c->rbuf, sizeof(c->rbuf), busy_cli_recv, c)
                     == ZN_OK);
    c->recv_busy = (zn_recv(tcp, c->rbuf, sizeof(c->rbuf), cb_noop_recv, NULL)
                    == ZN_EBUSY);
}

TEST(tcp_send_recv_twice) {
    busy_ctx  c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, busy_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener),
                        0, busy_connect, &c), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    assertok(c.send_armed);
    assertok(c.send_busy);
    assertok(c.recv_armed);
    assertok(c.recv_busy);
    asserteq(c.client_sent, 1);
    asserteq(c.client_recv, 1);
    asserteq(S->waitings, 0);

    zn_deltcp(c.client);
    zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

/* ---- close and pending operations ---- */

TEST(tcp_send_after_close) {
    zn_State  *S = zt_open();
    zn_Accept *listener;
    zn_Tcp    *tcp;
    char       buf[8];
    unsigned   port;

    listener = zn_newaccept(S, 0);
    assertok(listener != NULL);
    asserteq(zn_listen(listener, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(listener);
    assertok(port != 0);

    tcp = zn_newtcp(S);
    assertok(tcp != NULL);
    asserteq(zn_connect(tcp, "127.0.0.1", port, 0, cb_noop_connect, NULL),
             ZN_OK);
    asserteq(zn_closetcp(tcp), ZN_OK);

    /* after a close the socket is gone: send and recv are state errors */
    asserteq(zn_send(tcp, "x", 1, cb_noop_send, NULL), ZN_ESTATE);
    asserteq(zn_recv(tcp, buf, sizeof(buf), cb_noop_recv, NULL), ZN_ESTATE);

    /* A closed tcp may be opened again (invariant 6: only zn_deltcp() or a
     * new zn_connect() is legal afterwards), so zn_connect is deliberately
     * not ESTATE here; tcp_reconnect_after_close proves it succeeds. */
    asserteq(zn_connect(tcp, "127.0.0.1", port, 0, cb_noop_connect, NULL),
             ZN_OK);

    zn_deltcp(tcp);
    zn_delaccept(listener);
    asserteq(S->waitings, 0);
    zt_close(S);
}

typedef struct pend_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[8];
    int        accepted;
    int        connected;
    int        recv_called;
} pend_ctx;

static void pend_accept(void *ud, zn_Accept *accept, unsigned err,
                        zn_Tcp *tcp) {
    pend_ctx *c = (pend_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    /* keep the accepted side open and silent: closing it here could race
     * with the client arming its recv and turn the pending recv into EOF */
    c->server = tcp;
}

static void pend_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    pend_ctx *c = (pend_ctx *)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
    c->recv_called = 1;
}

static void pend_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    pend_ctx *c = (pend_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), pend_recv, c), ZN_OK);
    /* close with the recv still pending: the operation is discarded, the
     * callback must never run, and the loop must still terminate. */
    asserteq(zn_closetcp(tcp), ZN_OK);
    zn_deltcp(tcp);
    c->client = NULL;
}

TEST(tcp_close_pending_recv) {
    pend_ctx  c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, pend_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener),
                        0, pend_connect, &c), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    asserteq(c.recv_called, 0); /* the close cancelled it */
    assertok(c.client == NULL);
    asserteq(S->waitings, 0);
    if (c.server != NULL) zn_deltcp(c.server);
    if (c.listener != NULL) zn_delaccept(c.listener);
    zt_close(S);
}

typedef struct reconn_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    unsigned   port;
    int        first;
    int        second;
    int        accepted;
} reconn_ctx;

static void reconn_accept(void *ud, zn_Accept *accept, unsigned err,
                          zn_Tcp *tcp) {
    reconn_ctx *c = (reconn_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted++;
    c->server = tcp;
    zn_deltcp(tcp); /* one accept only: never re-armed */
    c->server = NULL;
}

static void reconn_connect2(void *ud, zn_Tcp *tcp, unsigned err) {
    reconn_ctx *c = (reconn_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    c->second = 1;
}

static void reconn_connect1(void *ud, zn_Tcp *tcp, unsigned err) {
    reconn_ctx *c = (reconn_ctx *)ud;
    asserteq(err, ZN_OK);
    c->first = 1;
    /* close but keep the object, then connect again on the same object */
    asserteq(zn_closetcp(tcp), ZN_OK);
    asserteq(zn_connect(tcp, "127.0.0.1", c->port, 0, reconn_connect2, c),
             ZN_OK);
}

TEST(tcp_reconnect_after_close) {
    reconn_ctx c;
    zn_State  *S;

    memset(&c, 0, sizeof(c));
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    c.port = zt_accept_port(c.listener);
    assertok(c.port != 0);
    asserteq(zn_accept(c.listener, reconn_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", c.port, 0, reconn_connect1, &c),
             ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(c.first, 1);
    asserteq(c.second, 1); /* the reconnect really completed */
    asserteq(c.accepted, 1); /* only one accept was armed */
    asserteq(S->waitings, 0);

    if (c.client != NULL) zn_deltcp(c.client);
    if (c.server != NULL) zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

/* ---- close from inside each callback ---- */

typedef struct close_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[8];
    int        mode; /* 0 = connect cb, 1 = send cb, 2 = recv cb */
    int        accepted;
    int        connected;
} close_ctx;

static void close_srv_send(void *ud, zn_Tcp *tcp, unsigned err,
                           unsigned cnt) {
    (void)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
}

static void close_accept(void *ud, zn_Accept *accept, unsigned err,
                         zn_Tcp *tcp) {
    close_ctx *c = (close_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    /* mode 2 needs data so the client's recv is not left pending */
    if (c->mode == 2)
        asserteq(zn_send(tcp, "x", 1, close_srv_send, c), ZN_OK);
}

static void close_cli_send(void *ud, zn_Tcp *tcp, unsigned err,
                           unsigned cnt) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    if (c->mode == 1) {
        asserteq(zn_closetcp(tcp), ZN_OK);
        zn_deltcp(tcp);
        c->client = NULL;
    }
}

static void close_cli_recv(void *ud, zn_Tcp *tcp, unsigned err,
                           unsigned cnt) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    assertok(memcmp(c->buf, "x", 1) == 0);
    if (c->mode == 2) {
        asserteq(zn_closetcp(tcp), ZN_OK);
        zn_deltcp(tcp);
        c->client = NULL;
    }
}

static void close_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    if (c->mode == 0) {
        asserteq(zn_closetcp(tcp), ZN_OK);
        zn_deltcp(tcp);
        c->client = NULL;
    } else if (c->mode == 1) {
        asserteq(zn_send(tcp, "y", 1, close_cli_send, c), ZN_OK);
    } else {
        asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), close_cli_recv, c),
                 ZN_OK);
    }
}

static void close_phase(int mode) {
    close_ctx c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    c.mode = mode;
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, close_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener), 0,
                        close_connect, &c), ZN_OK);

    asserteq(zt_run(S), 0); /* the delete inside the callback ends the loop */
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    asserteq(S->waitings, 0);

    if (c.client != NULL) zn_deltcp(c.client);
    if (c.server != NULL) zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

TEST(tcp_close_in_connect_cb) { close_phase(0); }
TEST(tcp_close_in_send_cb) { close_phase(1); }
TEST(tcp_close_in_recv_cb) { close_phase(2); }

/* ---- peer closes its side ---- */

typedef struct eof_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[8];
    int        accepted;
    int        connected;
    int        first_recv;
    int        eof;
    unsigned   eof_err;
} eof_ctx;

static void eof_srv_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    eof_ctx *c = (eof_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 3);
    /* everything is out; closing the server side is what the client must
     * observe as ZN_ECLOSED on its next recv */
    zn_deltcp(tcp);
    c->server = NULL;
}

static void eof_accept(void *ud, zn_Accept *accept, unsigned err,
                       zn_Tcp *tcp) {
    eof_ctx *c = (eof_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    asserteq(zn_send(tcp, "bye", 3, eof_srv_send, c), ZN_OK);
}

static void eof_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    eof_ctx *c = (eof_ctx *)ud;
    if (!c->first_recv) {
        asserteq(err, ZN_OK);
        asserteq(cnt, 3);
        assertok(memcmp(c->buf, "bye", 3) == 0);
        c->first_recv = 1;
        /* the peer is closing now: this recv must report ZN_ECLOSED */
        asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), eof_recv, c), ZN_OK);
        return;
    }
    c->eof = 1;
    c->eof_err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

static void eof_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    eof_ctx *c = (eof_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), eof_recv, c), ZN_OK);
}

TEST(tcp_peer_echo_peer_close) {
    eof_ctx   c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, eof_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener),
                        0, eof_connect, &c), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    asserteq(c.first_recv, 1);
    asserteq(c.eof, 1);
    asserteq(c.eof_err, ZN_ECLOSED);
    asserteq(S->waitings, 0);

    if (c.client != NULL) zn_deltcp(c.client);
    if (c.server != NULL) zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

/* ---- ten messages back to back ---- */

#define MANY_COUNT 10

typedef struct many_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       byte;
    char       rbuf[1];
    int        accepted;
    int        connected;
    int        sends;
    int        recvs;
    unsigned   received;
} many_ctx;

static void many_srv_recv(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    many_ctx *c = (many_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1); /* one byte per armed recv, so events are countable */
    c->recvs++;
    c->received += cnt;
    if (c->recvs >= MANY_COUNT) {
        zn_deltcp(tcp);
        c->server = NULL;
        return;
    }
    asserteq(zn_recv(tcp, c->rbuf, 1, many_srv_recv, c), ZN_OK);
}

static void many_accept(void *ud, zn_Accept *accept, unsigned err,
                        zn_Tcp *tcp) {
    many_ctx *c = (many_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    asserteq(zn_recv(tcp, c->rbuf, 1, many_srv_recv, c), ZN_OK);
}

static void many_cli_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    many_ctx *c = (many_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    c->sends++;
    if (c->sends >= MANY_COUNT) {
        zn_deltcp(tcp);
        c->client = NULL;
        return;
    }
    /* re-arm exactly one send per completed send */
    asserteq(zn_send(tcp, &c->byte, 1, many_cli_send, c), ZN_OK);
}

static void many_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    many_ctx *c = (many_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    asserteq(zn_send(tcp, &c->byte, 1, many_cli_send, c), ZN_OK);
}

TEST(tcp_many_messages) {
    many_ctx  c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    c.byte = 'z';
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, many_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener),
                        0, many_connect, &c), ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    asserteq(c.sends, MANY_COUNT);
    asserteq(c.recvs, MANY_COUNT);
    asserteq(c.received, MANY_COUNT);
    asserteq(S->waitings, 0);

    if (c.client != NULL) zn_deltcp(c.client);
    if (c.server != NULL) zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

/* TODO (new, not in TODO.md; proposed B13): the select backend never maps a
 * failed connect to an error.  znP_dispatch() (znet.h:2079) derives the
 * error from the fd set only ("setno == 2") and zn_onconnect() (znet.h:1854)
 * treats readiness as success without reading SO_ERROR, so a connection to a
 * closed port calls back with ZN_OK while SO_ERROR is ECONNREFUSED.  kqueue
 * reports ZN_ERROR (EV_EOF/EV_ERROR) and passes.  Desired: the callback must
 * never report success; check SO_ERROR in zn_onconnect() for both backends.
 * Kept with the expected failures at the end of the file. */
typedef struct refused_ctx {
    int      called;
    unsigned err;
} refused_ctx;

static void refused_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    refused_ctx *c = (refused_ctx *)ud;
    (void)tcp;
    c->called = 1;
    c->err = err;
}

TEST(tcp_connect_refused) {
    zn_State    *S = zt_open();
    zn_Accept   *listener;
    zn_Tcp      *tcp;
    refused_ctx  c;
    unsigned     port;
    int          ret;

    memset(&c, 0, sizeof(c));
    listener = zn_newaccept(S, 0);
    assertok(listener != NULL);
    asserteq(zn_listen(listener, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(listener);
    assertok(port != 0);
    zn_delaccept(listener); /* nothing listens on that port any more */

    tcp = zn_newtcp(S);
    assertok(tcp != NULL);
    ret = zn_connect(tcp, "127.0.0.1", port, 0, refused_connect, &c);
    if (ret == ZN_OK) {
        /* loopback usually refuses asynchronously; the loop delivers it */
        asserteq(zt_run(S), 0);
        test_log("  refused connect: cb=%d err=%d (%s)\n", c.called,
                 (int)c.err, zn_strerror((int)c.err));
        asserteq(c.called, 1);
        assertok(c.err != ZN_OK);
    } else {
        /* the kernel refused synchronously: zn_connect reports it itself */
        asserteq(ret, ZN_ECONNECT);
        asserteq(c.called, 0);
    }
    zn_deltcp(tcp);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* A target the kernel rejects synchronously (broadcast has no TCP route, so
 * connect() fails instead of returning EINPROGRESS) exercises the backend's
 * immediate-failure cleanup, not the asynchronous refusal path above. */
#if !defined(_WIN32)
TEST(tcp_connect_unreachable) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    int       ret;

    assertok(tcp != NULL);
    ret = zn_connect(tcp, "255.255.255.255", 80, 0, cb_noop_connect, NULL);
    test_log("  connect broadcast: ret=%d (%s)\n", ret, zn_strerror(ret));
    asserteq(ret, ZN_ECONNECT);
    asserteq(S->waitings, 0);
    zn_deltcp(tcp);
    zt_close(S);
}
#endif /* !_WIN32 */

/* The ESTATE guard for an open socket (znet.h:644) runs before the EBUSY
 * guard (znet.h:645) and znP_connect() has already stored the socket, so a
 * second zn_connect() while the first is in flight reports ZN_ESTATE and the
 * EBUSY branch is unreachable.  AGENTS invariant 2 says connect behaves like
 * send/recv, i.e. one in-flight connect per object and ZN_EBUSY for the
 * second one.  Desired: check connect_handler before the socket state.
 * Not in TODO.md: reported as a new finding (proposed L12). */
TEST(tcp_connect_twice) {
    zn_State  *S = zt_open();
    zn_Accept *listener;
    zn_Tcp    *tcp;
    unsigned   port;
    int        verified = 0;
    int        conn_ok = 0;
    int        ret;

    listener = zn_newaccept(S, 0);
    assertok(listener != NULL);
    asserteq(zn_listen(listener, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(listener);
    assertok(port != 0);

    tcp = zn_newtcp(S);
    assertok(tcp != NULL);
    asserteq(zn_connect(tcp, "127.0.0.1", port, 0, cb_flag_connect,
                        &conn_ok), ZN_OK);

    ret = zn_connect(tcp, "127.0.0.1", port, 0, cb_noop_connect, NULL);
    test_log("  connect twice: second zn_connect -> %d (%s), waitings=%u\n",
             ret, zn_strerror(ret), S->waitings);
    asserteq(ret, ZN_EBUSY);

    /* only reached once the guard order is fixed: the first connect must
     * still complete, and a connect on the connected object is ESTATE */
    asserteq(zt_run(S), 0);
    asserteq(conn_ok, 1);
    verified = (zn_connect(tcp, "127.0.0.1", port, 0, cb_noop_connect, NULL)
                == ZN_ESTATE);
    asserteq(verified, 1);
    asserteq(S->waitings, 0);
    zn_deltcp(tcp);
    zn_delaccept(listener);
    zt_close(S);
}

/* A second listener cannot bind an address:port that is already taken;
 * zn_listen() must report EBIND and leave the accept object unusable. */
TEST(tcp_listen_conflict) {
    zn_State  *S = zt_open();
    zn_Accept *a = zn_newaccept(S, 0);
    zn_Accept *b = zn_newaccept(S, 0);
    unsigned   port;

    assertok(a != NULL && b != NULL);
    asserteq(zn_listen(a, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(a);
    assertok(port != 0);
    asserteq(zn_listen(b, "127.0.0.1", port), ZN_EBIND);
    asserteq(zn_accept(b, cb_noop_accept, NULL), ZN_ESTATE);
    zn_delaccept(a);
    zn_delaccept(b);
    zt_close(S);
}

/* L2 regression (fixed): the reactive znP_closetcp() used to guess "is a
 * result queued?" from can_*, so closing a send parked by EAGAIN left its
 * retain unreleased; S->waitings never reached 0 and zn_run(ZN_RUN_LOOP)
 * never returned.  close now cancels exactly the parked directions.
 * Only the result-queue backends have can_write and the parked-send state,
 * hence the guard; znet.h #undefs its zn_use_* selectors, so the reactive
 * backend is identified the portable way: POSIX without ZN_USE_SELECT. */
#if !defined(ZN_USE_SELECT) && !defined(_WIN32)

#define BLOCKED_SEND_SIZE 65536
#define BLOCKED_MAX_CBS 200

typedef struct blocked_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    int        accepted;
    int        connected;
    int        blocked;
    int        send_cbs;
    int        too_many;
    unsigned   bytes;
} blocked_ctx;

static char blocked_buf[BLOCKED_SEND_SIZE];

/* close and remove both ends; the loop can only finish once every retain of
 * the closed client has been released by the backend */
static void blocked_teardown(blocked_ctx *c, zn_Tcp *tcp) {
    zn_closetcp(tcp);
    zn_deltcp(tcp);
    c->client = NULL;
    if (c->server != NULL) {
        zn_deltcp(c->server);
        c->server = NULL;
    }
}

/* after a zn_send() that returned ZN_OK, !can_write means the backend
 * parked the send with no result queued, i.e. the L2 state */
static void blocked_park(blocked_ctx *c, zn_Tcp *tcp) {
    if (tcp->can_write || tcp->send_handler == NULL) return;
    c->blocked = 1;
    /* log before closing: zn_run(LOOP) does not return today, so the log in
     * the test body below is never reached while the bug is present */
    test_log("  parked send: cbs=%d bytes=%u can_write=%d handler=%d\n",
             c->send_cbs, c->bytes, (int)tcp->can_write,
             tcp->send_handler != NULL);
    blocked_teardown(c, tcp);
}

static void blocked_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned cnt) {
    blocked_ctx *c = (blocked_ctx *)ud;
    c->send_cbs++;
    asserteq(err, ZN_OK);
    c->bytes += cnt;
    if (c->send_cbs >= BLOCKED_MAX_CBS) {
        c->too_many = 1;
        blocked_teardown(c, tcp);
        return;
    }
    asserteq(zn_send(tcp, blocked_buf, BLOCKED_SEND_SIZE, blocked_send, c),
             ZN_OK);
    blocked_park(c, tcp);
}

static void blocked_accept(void *ud, zn_Accept *accept, unsigned err,
                           zn_Tcp *tcp) {
    blocked_ctx *c = (blocked_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    /* deliberately never read: the client's send must run into EAGAIN */
    c->server = tcp;
}

static void blocked_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    blocked_ctx *c = (blocked_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    zt_tcp_set_sndbuf(tcp, 1024);
    asserteq(zn_send(tcp, blocked_buf, BLOCKED_SEND_SIZE, blocked_send, c),
             ZN_OK);
    blocked_park(c, tcp);
}

TEST(tcp_close_blocked_send) {
    blocked_ctx c;
    zn_State   *S;
    int         ret;

    memset(&c, 0, sizeof(c));
    memset(blocked_buf, 'x', sizeof(blocked_buf));
    S = zt_open();
    c.S = S;
    c.listener = zn_newaccept(S, 0);
    assertok(c.listener != NULL);
    asserteq(zn_listen(c.listener, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(c.listener, blocked_accept, &c), ZN_OK);
    c.client = zn_newtcp(S);
    assertok(c.client != NULL);
    asserteq(zn_connect(c.client, "127.0.0.1", zt_accept_port(c.listener),
                        0, blocked_connect, &c), ZN_OK);

    /* regressed by L2: close did not cancel the parked send, so its retain
     * never came back and the loop could not return */
    ret = zt_run(S);
    test_log("  blocked send: ret=%d blocked=%d cbs=%d bytes=%u waitings=%u "
             "accepted=%d\n", ret, c.blocked, c.send_cbs, c.bytes,
             S->waitings, c.accepted);
    asserteq(ret, 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    if (c.too_many) {
        test_skip("could not fill socket buffer");
    } else {
        asserteq(c.blocked, 1);
        asserteq(S->waitings, 0);
    }

    if (c.client != NULL) zn_deltcp(c.client);
    if (c.server != NULL) zn_deltcp(c.server);
    zn_delaccept(c.listener);
    zt_close(S);
}

/* A parked direction (can_* == 0) owns its own retain; a completing direction
 * (can_* == 1) has its result queued and that result owns the retain.  Close
 * must cancel exactly the first kind: cancelling a completing one releases
 * the retain twice (the queued result releases again), and cancelling none
 * leaks it.  White box because a real socket cannot be put into both states
 * deterministically; the fd is real so the close path has something to shut. */
TEST(tcp_close_parked_and_queued) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    char      buf[8];
    int       fd;

    assertok(tcp != NULL);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    tcp->socket = fd;

    tcp->recv_handler = cb_noop_recv;   /* parked recv */
    tcp->recv_buffer.buf = buf;
    tcp->recv_buffer.len = sizeof(buf);
    tcp->can_read = 0;
    zn_retain(S);

    tcp->send_handler = cb_noop_send;   /* completing send */
    tcp->send_buffer.buf = buf;
    tcp->send_buffer.len = 1;
    tcp->can_write = 1;
    zn_retain(S);
    znR_add(S, ZN_OK, &tcp->send_result);

    asserteq(S->waitings, 2);
    asserteq(zn_closetcp(tcp), ZN_OK);
    asserteq(S->waitings, 1);           /* only the parked recv was cancelled */
    asserteq(tcp->recv_handler == NULL, 1);
    assertok(tcp->send_handler != NULL);

    /* the queued result releases the last retain, the callback is suppressed
     * (socket == -1) and the object is reclaimed */
    asserteq(znR_process(S, 1), 0);
    asserteq(S->waitings, 0);
    /* znL_empty() cannot be called (it takes &(h) of an already expanded
     * head argument), so compare the head ring directly */
    assertok(S->active_tcps.next == (zn_Tcp*)(void*)&S->active_tcps);
    zt_close(S);
}

/* The recv mirror of the queued-completion case above: the result owns the
 * retain, close leaves it queued, and its completion must reclaim the object
 * once the send side is already gone. */
TEST(tcp_recv_result_after_close) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    char      buf[8];
    int       fd;

    assertok(tcp != NULL);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    tcp->socket = fd;

    tcp->recv_handler = cb_noop_recv;   /* completing recv */
    tcp->recv_buffer.buf = buf;
    tcp->recv_buffer.len = sizeof(buf);
    tcp->can_read = 1;
    zn_retain(S);
    znR_add(S, ZN_OK, &tcp->recv_result);

    asserteq(S->waitings, 1);
    asserteq(zn_closetcp(tcp), ZN_OK);
    asserteq(S->waitings, 1);           /* the queued result still owns it */

    asserteq(znR_process(S, 1), 0);
    asserteq(S->waitings, 0);
    assertok(S->active_tcps.next == (zn_Tcp*)(void*)&S->active_tcps);
    zt_close(S);
}

/* Both directions parked: close cancels two retains, so zn_deltcp() can
 * reclaim the object immediately. */
TEST(tcp_close_both_parked) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    char      buf[8];
    int       fd;

    assertok(tcp != NULL);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    tcp->socket = fd;

    tcp->send_handler = cb_noop_send;
    tcp->send_buffer.buf = buf;
    tcp->send_buffer.len = 1;
    tcp->can_write = 0;
    zn_retain(S);
    tcp->recv_handler = cb_noop_recv;
    tcp->recv_buffer.buf = buf;
    tcp->recv_buffer.len = sizeof(buf);
    tcp->can_read = 0;
    zn_retain(S);

    asserteq(S->waitings, 2);
    asserteq(zn_closetcp(tcp), ZN_OK);
    asserteq(S->waitings, 0);
    asserteq(tcp->send_handler == NULL, 1);
    asserteq(tcp->recv_handler == NULL, 1);
    asserteq(zn_closetcp(tcp), ZN_OK);  /* idempotent: no second release */
    asserteq(S->waitings, 0);
    zn_deltcp(tcp);
    assertok(S->active_tcps.next == (zn_Tcp*)(void*)&S->active_tcps);
    zt_close(S);
}

/* Closing with a completion still queued must not let zn_connect() reuse the
 * object: the queued result owns the old connection's result slot, and a new
 * connect would enqueue the same node twice (D's failure surface #4). */
TEST(tcp_connect_after_close_queued) {
    zn_State  *S = zt_open();
    zn_Accept *listener = zn_newaccept(S, 0);
    zn_Tcp    *tcp = zn_newtcp(S);
    char       buf[8];
    int        fd;

    assertok(tcp != NULL && listener != NULL);
    asserteq(zn_listen(listener, "127.0.0.1", 0), ZN_OK);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    tcp->socket = fd;

    tcp->send_handler = cb_noop_send;   /* completing: result owns the retain */
    tcp->send_buffer.buf = buf;
    tcp->send_buffer.len = 1;
    tcp->can_write = 1;
    zn_retain(S);
    znR_add(S, ZN_OK, &tcp->send_result);

    asserteq(zn_closetcp(tcp), ZN_OK);
    asserteq(zn_connect(tcp, "127.0.0.1", zt_accept_port(listener), 0,
                        cb_noop_connect, NULL), ZN_EBUSY);

    /* draining the queue releases the last retain and reclaims the object */
    asserteq(znR_process(S, 1), 0);
    asserteq(S->waitings, 0);
    assertok(S->active_tcps.next == (zn_Tcp*)(void*)&S->active_tcps);
    zn_delaccept(listener);
    zt_close(S);
}

/* A send parked by EAGAIN must complete once the socket is writable again:
 * znP_retry_send() releases the parked retain, re-issues through zn_send()
 * and the completion is delivered by zn_onresult().  White box: a socketpair
 * filled to EAGAIN makes the park deterministic. */
TEST(tcp_retry_send_after_eagain) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    static char big[262144];
    int      sv[2];

    assertok(tcp != NULL);
    assertok(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    znU_set_nonblock(sv[0]);
    znU_set_nonblock(sv[1]);
    tcp->socket = sv[0];

    /* fill the socket buffer so the very first zn_send() parks */
    memset(big, 'x', sizeof(big));
    while (write(sv[0], big, sizeof(big)) > 0)
        ;
    asserteq(zn_send(tcp, big, sizeof(big), cb_noop_send, NULL), ZN_OK);
    assertok(tcp->send_handler != NULL);
    asserteq(tcp->can_write, 0);
    asserteq(S->waitings, 1);

    /* the peer drains: this is the edge the parked send is waiting for */
    while (read(sv[1], big, sizeof(big)) > 0)
        ;
    znP_retry_send(tcp);
    assertok(tcp->can_write == 1 || tcp->send_handler == NULL);
    asserteq(znR_process(S, 1), 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

/* reactive backend only (kqueue/epoll); select has no parked-send state */
#endif /* !ZN_USE_SELECT && !_WIN32 */

/* L5 regression (fixed): zn_accept() used to have no ZN_EBUSY check.  A
 * second call while the first accept was in flight overwrote the handler and
 * leaked one zn_retain (waitings == 2, first accept never completes).  Now the
 * second call returns ZN_EBUSY and changes nothing.  The pending accept is
 * deliberately not pumped, so the state is torn down with zn_close() only. */
TEST(tcp_accept_twice) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_newaccept(S, 0);
    int        ret;

    assertok(accept != NULL);
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(accept, cb_noop_accept, NULL), ZN_OK);
    asserteq(S->waitings, 1);

    ret = zn_accept(accept, cb_noop_accept, NULL);
    test_log("  accept twice: second zn_accept -> %d (%s), waitings=%u\n",
             ret, zn_strerror(ret), S->waitings);
    asserteq(ret, ZN_EBUSY);
    asserteq(S->waitings, 1); /* one accept, exactly one retain */
    zn_close(S);
}

/* ---- L6/L7: a readiness report the accept queue cannot back up -----------
 * "Readable" for a listening socket is derived from the accept queue itself,
 * so `accept()` returning EAGAIN means the report and the queue disagreed:
 * another owner drained the queue, or the kernel dropped the connection.
 * Before the fix that state consumed the armed accept silently (select) or
 * blocked the whole loop in accept() (epoll/kqueue: the listener was never
 * set non-blocking), so these tests inject the readiness by hand -- the
 * kernel will not produce it on demand. */

#ifndef _WIN32

# include <fcntl.h>       /* fcntl(), O_NONBLOCK */
# include <netinet/in.h>  /* struct sockaddr_in */
# include <arpa/inet.h>   /* htons(), htonl() */

typedef struct sp_ctx {
    int     ok;
    int     err;
    zn_Tcp *tcp;
} sp_ctx;

static void sp_on_accept(void *ud, zn_Accept *accept, unsigned err,
                         zn_Tcp *tcp) {
    sp_ctx *c = (sp_ctx *)ud;
    (void)accept;
    if (err == ZN_OK) {
        c->ok++;
        c->tcp = tcp;
    } else
        c->err++;
}

/* re-arm after every accepted connection: the callback is consumed by each
 * completion, so a listener that is not re-armed stops accepting */
static void sp_on_accept_rearm(void *ud, zn_Accept *accept, unsigned err,
                               zn_Tcp *tcp) {
    sp_ctx *c = (sp_ctx *)ud;
    if (err == ZN_OK) {
        c->ok++;
        zn_deltcp(tcp); /* the payload is irrelevant: close and keep serving */
    } else
        c->err++;
    asserteq(zn_accept(accept, sp_on_accept_rearm, c), ZN_OK);
}

/* a plain blocking client: these tests only need something in the queue */
static int sp_connect(unsigned port) {
    struct sockaddr_in sa;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = htonl(0x7f000001u);
    assertok(connect(fd, (struct sockaddr *)&sa, (socklen_t)sizeof(sa)) == 0);
    return fd;
}

static void sp_connect_and_abort(unsigned port) {
    struct linger lg;
    int fd = sp_connect(port);
    lg.l_onoff = 1;
    lg.l_linger = 0;
    assertok(setsockopt(fd, SOL_SOCKET, SO_LINGER, (const void *)&lg,
                        (socklen_t)sizeof(lg)) == 0);
    close(fd); /* RST: the completed connection dies before it is accepted */
}

/* the reactive backends handed a blocking listen socket to accept(), so this
 * used to hang the loop; the guard turns that into a failure, not a stall */
TEST(tcp_listener_is_nonblocking) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_newaccept(S, 0);

    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    assertok((fcntl(accept->socket, F_GETFL) & O_NONBLOCK) != 0);
    zn_delaccept(accept);
    zt_close(S);
}

TEST(tcp_accept_spurious_ready) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_newaccept(S, 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        fd;

    memset(&c, 0, sizeof(c));
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(accept);
    asserteq(zn_accept(accept, sp_on_accept, &c), ZN_OK);
    asserteq(S->waitings, 1);

    zt_guard_start(1000);
    zt_dispatch_accept(accept, 0);
    zt_guard_stop();

    asserteq(c.err, 1); /* surfaced exactly once, not retried in silence */
    asserteq(c.ok, 0);
    asserteq(S->waitings, 0);
    asserteq(zn_accept(accept, sp_on_accept, &c), ZN_OK); /* still usable */

    fd = sp_connect(port); /* a real connection still gets through */
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, c.ok == 1);
    zt_end(&L);
    asserteq(c.ok, 1);

    zn_deltcp(c.tcp);
    close(fd);
    zn_delaccept(accept);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* an error event on the listener (select exceptfds / EPOLLERR / EV_ERROR)
 * used to close the socket; it must only disarm it, because a listener error
 * is not proof that the listening socket is unusable. */
TEST(tcp_accept_error_event) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_newaccept(S, 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        fd;

    memset(&c, 0, sizeof(c));
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(accept);
    asserteq(zn_accept(accept, sp_on_accept, &c), ZN_OK);

    zt_dispatch_accept(accept, 1);
    asserteq(c.err, 1);
    asserteq(c.ok, 0);
    asserteq(S->waitings, 0);
    asserteq(zn_accept(accept, sp_on_accept, &c), ZN_OK); /* still usable */

    fd = sp_connect(port);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, c.ok == 1);
    zt_end(&L);
    asserteq(c.ok, 1);

    zn_deltcp(c.tcp);
    close(fd);
    zn_delaccept(accept);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* a stream of clients that die right after the handshake must not cost the
 * listener: no error callback, no closed socket, and the next healthy client
 * is still accepted.  (A "give up after N spurious wakeups" retry budget
 * would fail this test, which is one reason there is none.) */
TEST(tcp_accept_after_aborted_clients) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_newaccept(S, 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        before, fd, i;

    memset(&c, 0, sizeof(c));
    asserteq(zn_listen(accept, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(accept);
    asserteq(zn_accept(accept, sp_on_accept_rearm, &c), ZN_OK);

    for (i = 0; i < 20; ++i)
        sp_connect_and_abort(port);

    zt_begin(&L, S, 3000);
    zt_wait(&L, 200); /* drain the dead connections */
    before = c.ok;
    fd = sp_connect(port);
    ZT_WAIT(L, c.ok > before);
    zt_end(&L);

    close(fd);
    zn_delaccept(accept);
    asserteq(S->waitings, 0);
    zt_close(S);
}

#endif /* !_WIN32 */

/* ---- B9: the select TCP directions must park, not drop -----------------
 * zn_onsend/zn_onrecv consumed the operation before the syscall, so a
 * readiness report with no room/data dropped the send/recv silently (no
 * callback, retain gone).  TCP directions do have a park state -- the
 * reactive backends use it for exactly this case (znP_retry_send -> znP_send
 * -> EAGAIN -> can_write = 0), so select parks the same way instead of
 * reporting an error that the other backends do not report. */

#if defined(ZN_USE_SELECT)

typedef struct b9_ctx {
    int ok;
    int err;
} b9_ctx;

static void b9_on_io(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    b9_ctx *c = (b9_ctx *)ud;
    (void)tcp;
    (void)count;
    if (err == ZN_OK) c->ok++;
    else c->err++;
}

static void b9_socketpair(zn_Tcp *tcp, int sv[2]) {
    assertok(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    znU_set_nonblock(sv[0]);
    znU_set_nonblock(sv[1]);
    tcp->socket = sv[0];
}

TEST(tcp_select_send_spurious_ready) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    b9_ctx    c;
    static char big[262144];
    int       sv[2];

    memset(&c, 0, sizeof(c));
    assertok(tcp != NULL);
    b9_socketpair(tcp, sv);

    /* fill the kernel send buffer so the first send() has to return EAGAIN */
    memset(big, 'x', sizeof(big));
    while (write(sv[0], big, sizeof(big)) > 0)
        ;
    asserteq(zn_send(tcp, big, sizeof(big), b9_on_io, &c), ZN_OK);
    asserteq(S->waitings, 1);

    zn_onsend(tcp, 0); /* write-ready, but the send buffer is full */

    /* parked, not dropped and not failed: the operation is still the
     * caller's, the retain is still held, and no callback ran */
    asserteq(c.ok, 0);
    asserteq(c.err, 0);
    assertok(tcp->send_handler != NULL);
    asserteq(S->waitings, 1);

    /* drain the peer: the same readiness now completes the parked send */
    while (read(sv[1], big, sizeof(big)) > 0)
        ;
    zn_onsend(tcp, 0);
    asserteq(c.ok, 1);
    asserteq(c.err, 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

TEST(tcp_select_recv_spurious_ready) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp = zn_newtcp(S);
    b9_ctx    c;
    char      buf[16];
    int       sv[2];

    memset(&c, 0, sizeof(c));
    assertok(tcp != NULL);
    b9_socketpair(tcp, sv);

    asserteq(zn_recv(tcp, buf, (unsigned)sizeof(buf), b9_on_io, &c), ZN_OK);
    asserteq(S->waitings, 1);

    zn_onrecv(tcp, 0); /* read-ready, but nothing to read */

    asserteq(c.ok, 0);
    asserteq(c.err, 0);
    assertok(tcp->recv_handler != NULL);
    asserteq(S->waitings, 1);

    assertok(write(sv[1], "x", 1) == 1);
    zn_onrecv(tcp, 0); /* the next report completes the parked recv */
    asserteq(c.ok, 1);
    asserteq(c.err, 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

#endif /* ZN_USE_SELECT */

#include "tcp_test.gen.inc"
