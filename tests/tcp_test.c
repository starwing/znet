/* tcp_test.c -- TCP state guards, lifecycle and error paths.
 *
 * Covers: factory parameter/state guards, the four TCP callbacks (connect,
 * accept, send, recv), IPv4 and IPv6 loopback echo, repeated timer-driven
 * echo, refused/immediate connect failures, per-direction EBUSY, del with a
 * pending recv, send/recv on a BROKEN tcp, del from each callback, a listen
 * retried after a bind failure, peer close (ZN_ECLOSED), 10 back-to-back
 * messages, the L2 parked-send regressions, and the select/kqueue-visible I/O
 * error paths (EPIPE, RST, OOB).
 *
 * All tests are expected to pass.  The L2 tests sit in their own guarded
 * section because they are white box and reactive-only; run them through the
 * per-test runner (`just check` / the justfile "run" recipe) so one abort
 * cannot hide the rest.
 *
 * Backend selection: znet.h's internal ZN_USE_* selectors survive the include,
 * but the portable guards below key off ZN_USE_SELECT, _WIN32 and __linux__,
 * because misc/gen_entries.lua is given the same platform defines as the
 * compiler (see tests/README.md, "conditional compilation").
 */

#include "ztest.h"

/* ---- callbacks for tests that only exercise synchronous guards ---- */

static void cb_noop_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    (void)ud;
    (void)accept;
    (void)err;
    (void)tcp;
}

static void cb_noop_connect(void *ud, zn_Tcp *tcp, int err) {
    (void)ud;
    (void)tcp;
    (void)err;
}

static void cb_noop_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
}

static void cb_noop_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
}

/* ---- established loopback pair through the public factories ----
 * White-box cases that need a real CONNECTED tcp cannot allocate an empty
 * object any more, so they start from a real connection: one listener, one
 * armed accept, one client.  zt_pair_open() runs the loop until both sides are
 * up; the accept is consumed and not re-armed, so the caller only deletes the
 * listener.  A field the test deleted early must be set to NULL, exactly as
 * with any other zn_del* caller. */

typedef struct zt_pair {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    unsigned   port;
    int        connected;
    int        accepted;
} zt_pair;

static void zt_pair_onaccept(void *ud, zn_Accept *accept, int err,
                             zn_Tcp *tcp) {
    zt_pair *p = (zt_pair *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    p->server = tcp;
    p->accepted = 1;
}

static void zt_pair_onconnect(void *ud, zn_Tcp *tcp, int err) {
    zt_pair *p = (zt_pair *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    p->connected = 1;
}

static void zt_pair_open(zt_pair *p) {
    memset(p, 0, sizeof(*p));
    p->S = zt_open();
    p->listener = zn_listen(p->S, "127.0.0.1", 0);
    assertok(p->listener != NULL);
    p->port = zt_accept_port(p->listener);
    assertok(p->port != 0);
    asserteq(zn_accept(p->listener, zt_pair_onaccept, p), ZN_OK);
    p->client = zn_connect(p->S, "127.0.0.1", p->port, zt_pair_onconnect, p);
    assertok(p->client != NULL);
    asserteq(zt_run(p->S), 0);
    asserteq(p->connected, 1);
    asserteq(p->accepted, 1);
}

static void zt_pair_close(zt_pair *p) {
    if (p->client != NULL) zn_deltcp(p->client);
    if (p->server != NULL) zn_deltcp(p->server);
    if (p->listener != NULL) zn_delaccept(p->listener);
    zt_close(p->S);
}

/* ---- parameter and state guards ---- */

TEST(tcp_params) {
    zn_State  *S = zt_open();
    zn_Accept *accept;
    zn_Tcp    *tcp;
    zt_pair    p;
    char       buf[16];
    unsigned   port;

    /* NULL object guards: the arm entries take an object */
    asserteq(zn_send(NULL, "x", 1, cb_noop_send, NULL), ZN_EPARAM);
    asserteq(zn_recv(NULL, buf, 1, cb_noop_recv, NULL), ZN_EPARAM);
    asserteq(zn_accept(NULL, cb_noop_accept, NULL), ZN_EPARAM);

    /* the factories take a state and record the reason in it */
    asserteq(zn_lasterror(NULL), ZN_EPARAM);
    assertok(zn_connect(NULL, "127.0.0.1", 1, cb_noop_connect, NULL) == NULL);
    assertok(zn_listen(NULL, "127.0.0.1", 0) == NULL);

    /* NULL callback and unparsable address/port: NULL + ZN_EPARAM */
    assertok(zn_connect(S, NULL, 1, cb_noop_connect, NULL) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_connect(S, "127.0.0.1", 1, NULL, NULL) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_connect(S, "", 1, cb_noop_connect, NULL) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_connect(S, "127.0.0.1", 70000u, cb_noop_connect, NULL) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_listen(S, NULL, 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_listen(S, "", 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_listen(S, "127.0.0.1", 70000u) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);

    /* the failed factories leaked nothing: a real listener still comes back,
     * its success clears last_error, and the arm entries keep their guards */
    accept = zn_listen(S, "127.0.0.1", 0);
    assertok(accept != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    asserteq(zn_accept(accept, NULL, NULL), ZN_EPARAM);
    port = zt_accept_port(accept);
    assertok(port != 0);

    /* a CONNECTING tcp already knows its peer, but refuses send/recv */
    tcp = zn_connect(S, "127.0.0.1", port, cb_noop_connect, NULL);
    assertok(tcp != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    {
        zn_PeerInfo info;
        zn_getpeerinfo(tcp, &info);
        assertok(zt_peer_is(&info, "127.0.0.1", port));
    }
    asserteq(zn_send(tcp, "x", 1, cb_noop_send, NULL), ZN_ESTATE);
    asserteq(zn_recv(tcp, buf, sizeof(buf), cb_noop_recv, NULL), ZN_ESTATE);
    /* a NULL buffer with a positive length is a parameter error, checked
     * before the CONNECTING guard */
    asserteq(zn_send(tcp, NULL, 1, cb_noop_send, NULL), ZN_EPARAM);
    asserteq(zn_recv(tcp, NULL, 1, cb_noop_recv, NULL), ZN_EPARAM);
    zn_deltcp(tcp);
    zn_delaccept(accept);
    zt_close(S);

    /* zero length and NULL callback guards on a CONNECTED tcp */
    zt_pair_open(&p);
    asserteq(zn_send(p.client, "x", 1, NULL, NULL), ZN_EPARAM);
    asserteq(zn_send(p.client, "x", 0, cb_noop_send, NULL), ZN_EPARAM);
    asserteq(zn_recv(p.client, buf, sizeof(buf), NULL, NULL), ZN_EPARAM);
    asserteq(zn_recv(p.client, buf, 0, cb_noop_recv, NULL), ZN_EPARAM);

    /* the delete helpers are no-ops on NULL */
    zn_deltcp(NULL);
    zn_delaccept(NULL);
    zt_pair_close(&p);
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

static void echo_srv_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    c->server_sent = 1;
    /* the echo is out: the server side is done, delete it */
    zn_deltcp(tcp);
    c->server = NULL;
}

static void echo_srv_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    assertok(memcmp(c->sbuf, "ping", 4) == 0);
    c->server_recv = 1;
    asserteq(zn_send(tcp, c->sbuf, cnt, echo_srv_send, c), ZN_OK);
}

static void echo_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    echo_ctx *c = (echo_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    zn_getpeerinfo(tcp, &c->server_peer);
    asserteq(zn_recv(tcp, c->sbuf, sizeof(c->sbuf), echo_srv_recv, c), ZN_OK);
}

static void echo_cli_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_ctx *c = (echo_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 4);
    c->client_sent = 1;
}

static void echo_cli_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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

static void echo_connect(void *ud, zn_Tcp *tcp, int err) {
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

    memset(&c, 0, sizeof(c));
    c.addr = addr;
    c.S = zt_open();
    c.listener = zn_listen(c.S, addr, 0);
    if (c.listener == NULL) {
        zt_close(c.S);
        return 0;
    }
    c.port = zt_accept_port(c.listener);
    assertok(c.port != 0);
    asserteq(zn_accept(c.listener, echo_accept, &c), ZN_OK);

    c.client = zn_connect(c.S, addr, c.port, echo_connect, &c);
    assertok(c.client != NULL);

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

static void echo_loop_cli_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->sbuf));
    assertok(memcmp(c->rbuf, c->sbuf, sizeof(c->sbuf)) == 0);
    c->loop->echoes++;
    zn_deltcp(tcp);
    free(c);
}

static void echo_loop_cli_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->sbuf));
    c->loop->sends++;
}

static void echo_loop_connect(void *ud, zn_Tcp *tcp, int err) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    c->loop->connects++;
    asserteq(zn_send(tcp, c->sbuf, sizeof(c->sbuf), echo_loop_cli_send, c),
             ZN_OK);
    asserteq(zn_recv(tcp, c->rbuf, sizeof(c->rbuf), echo_loop_cli_recv, c),
             ZN_OK);
}

static void echo_loop_srv_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->rbuf));
    zn_deltcp(tcp);
    free(c);
}

static void echo_loop_srv_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    echo_conn *c = (echo_conn *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, sizeof(c->rbuf));
    asserteq(zn_send(tcp, c->rbuf, cnt, echo_loop_srv_send, c), ZN_OK);
}

static void echo_loop_accept(void *ud, zn_Accept *accept, int err,
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
    c->tcp = zn_connect(l->S, "127.0.0.1", l->port, echo_loop_connect, c);
    if (c->tcp == NULL) {
        free(c);
        zt_fail("zn_connect() returned NULL");
    }
    l->started++;
    return 20;
}

TEST(tcp_echo_loop) {
    echo_loop l;

    memset(&l, 0, sizeof(l));
    l.S = zt_open();
    l.listener = zn_listen(l.S, "127.0.0.1", 0);
    assertok(l.listener != NULL);
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
    int        err;
} ioerr_ctx;

static void ioerr_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
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
                        (const char *)&lg, sizeof(lg))
             == 0);
    zn_deltcp(c->server);
    c->server = NULL;
}

static void ioerr_connect(void *ud, zn_Tcp *tcp, int err) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    c->connected = 1;
}

static void ioerr_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

static void ioerr_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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
    c->listener = zn_listen(c->S, "127.0.0.1", 0);
    assertok(c->listener != NULL);
    asserteq(zn_accept(c->listener, accept_cb, c), ZN_OK);
    c->client = zn_connect(c->S, "127.0.0.1", zt_accept_port(c->listener),
                           connect_cb, c);
    assertok(c->client != NULL);
}

static void ioerr_close(ioerr_ctx *c) {
    if (c->client != NULL) zn_deltcp(c->client);
    if (c->server != NULL) zn_deltcp(c->server);
    zn_delaccept(c->listener);
    zt_close(c->S);
}

/* A send after the peer is gone must reach the callback with an error, not
 * with ZN_OK/count.  shutdown(SHUT_WR) makes send() fail with EPIPE on the
 * next attempt, so no timing is involved.  The process must survive that
 * send: the library has to suppress SIGPIPE itself (B13). */
TEST(tcp_send_error) {
    ioerr_ctx c;
    zt_Loop   L;

    ioerr_open(&c, ioerr_accept, ioerr_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);

    assertok(shutdown(c.client->socket, SHUT_WR) == 0);
    asserteq(zn_send(c.client, "x", 1, ioerr_send, &c), ZN_OK);
    ZT_WAIT(L, c.done == 1);
    zt_end(&L);

    test_log("  send after shutdown: err=%d (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* B13: strict feature macros hide both MSG_NOSIGNAL and SO_NOSIGPIPE on
 * Darwin, so the socket option is the guard; assert it directly instead of
 * relying on the process surviving a SIGPIPE. */
#if defined(__APPLE__)
/* Darwin ABI value, hidden by strict feature macros */
# define ZT_SO_NOSIGPIPE 0x1022
#endif

TEST(tcp_nosigpipe_option) {
#if defined(__APPLE__)
    ioerr_ctx c;
    zt_Loop   L;
    int       no_sigpipe = 0;
    socklen_t optlen = (socklen_t)sizeof(no_sigpipe);

    ioerr_open(&c, ioerr_accept, ioerr_connect);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.connected == 1 && c.accepted == 1);
    zt_end(&L);

    asserteq(getsockopt(c.client->socket, SOL_SOCKET, ZT_SO_NOSIGPIPE,
                        (void *)&no_sigpipe, &optlen),
             0);
    asserteq(no_sigpipe, 1);
    no_sigpipe = 0;
    optlen = (socklen_t)sizeof(no_sigpipe);
    asserteq(getsockopt(c.server->socket, SOL_SOCKET, ZT_SO_NOSIGPIPE,
                        (void *)&no_sigpipe, &optlen),
             0);
    asserteq(no_sigpipe, 1);

    ioerr_close(&c);
#else
    test_skip("SO_NOSIGPIPE is a Darwin socket option");
#endif
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

    test_log("  recv after RST: err=%d (%s)\n", c.err, zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* select reports TCP urgent data through exceptfds, not readfds.  A pending
 * recv must be completed with an error instead of hanging (select-only: the
 * kqueue/epoll paths get EV_OOBAND/EPOLLPRI and are not modelled here). */
#if defined(ZN_USE_SELECT)

static void oob_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
}

static void oob_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)cnt;
    c->done = 1;
    c->err = err;
    zn_deltcp(tcp);
    c->client = NULL;
}

static void oob_connect(void *ud, zn_Tcp *tcp, int err) {
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

    test_log("  recv with OOB pending: err=%d (%s)\n", c.err,
             zn_strerror((int)c.err));
    assertok(c.err != ZN_OK);
    asserteq(c.S->waitings, 0);
    ioerr_close(&c);
}

/* The same except set must also complete a pending *send* with an error.
 * Reaching this through select() needs a full kernel send buffer plus urgent
 * data (timing-dependent); dispatch the except set directly instead.  The
 * transport half is already proven by tcp_select_oob_recv_error. */
static void oobsend_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    ioerr_ctx *c = (ioerr_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
}

static void oobsend_sent(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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
    znB_dispatch(c.S, c.server->socket, 2);

    asserteq(c.done, 1);
    test_log("  send dispatched from exceptfds: err=%d (%s)\n", c.err,
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

static void busy_srv_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
}

static void busy_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    /* one byte so the client's pending recv completes and can be freed */
    asserteq(zn_send(tcp, "x", 1, busy_srv_send, c), ZN_OK);
}

static void busy_cli_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    assertok(memcmp(c->rbuf, "x", 1) == 0);
    c->client_recv = 1;
}

static void busy_cli_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    busy_ctx *c = (busy_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    c->client_sent = 1;
}

static void busy_connect(void *ud, zn_Tcp *tcp, int err) {
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
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, busy_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          busy_connect, &c);
    assertok(c.client != NULL);

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

/* ---- BROKEN: a terminal error already closed the fd -------------------- */

typedef struct broken_ctx {
    zn_Tcp     *tcp;
    zn_PeerInfo peer;
    int         called;
    int         err;
} broken_ctx;

static void broken_connect(void *ud, zn_Tcp *tcp, int err) {
    broken_ctx *c = (broken_ctx *)ud;
    c->called = 1;
    c->err = err;
    zn_getpeerinfo(tcp, &c->peer);
}

/* A refused connect is the deterministic terminal error.  It leaves the
 * object BROKEN rather than closed-but-reusable: send/recv are state errors,
 * the peer info outlives the fd, and zn_deltcp() is the only legal call. */
TEST(tcp_send_after_close) {
    zn_State  *S = zt_open();
    zn_Accept *listener = zn_listen(S, "127.0.0.1", 0);
    broken_ctx c;
    char       buf[8];
    unsigned   port;

    memset(&c, 0, sizeof(c));
    assertok(listener != NULL);
    port = zt_accept_port(listener);
    assertok(port != 0);
    zn_delaccept(listener); /* nothing listens on that port any more */

    c.tcp = zn_connect(S, "127.0.0.1", port, broken_connect, &c);
    if (c.tcp == NULL) {
        /* the kernel refused synchronously: there is no object to inspect */
        asserteq(zn_lasterror(S), ZN_ECONNECT);
        zt_close(S);
        return;
    }
    asserteq(zt_run(S), 0);
    asserteq(c.called, 1);
    assertok(c.err != ZN_OK);

    assertok(!znS_valid(c.tcp)); /* the fd is already gone */
    assertok(c.tcp->connect_handler == NULL);
    assertok(zt_peer_is(&c.peer, "127.0.0.1", port));
    asserteq(zn_send(c.tcp, "x", 1, cb_noop_send, NULL), ZN_ESTATE);
    asserteq(zn_recv(c.tcp, buf, sizeof(buf), cb_noop_recv, NULL), ZN_ESTATE);
    asserteq(S->waitings, 0);

    zn_deltcp(c.tcp);
    assertok(znL_empty(&S->active_tcps));
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

static void pend_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    pend_ctx *c = (pend_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    /* keep the accepted side open and silent: closing it here could race
     * with the client arming its recv and turn the pending recv into EOF */
    c->server = tcp;
}

static void pend_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    pend_ctx *c = (pend_ctx *)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
    c->recv_called = 1;
}

static void pend_connect(void *ud, zn_Tcp *tcp, int err) {
    pend_ctx *c = (pend_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    asserteq(zn_recv(tcp, c->buf, sizeof(c->buf), pend_recv, c), ZN_OK);
    /* del with the recv still pending: the operation is discarded, the
     * callback must never run, and the loop must still terminate. */
    zn_deltcp(tcp);
    c->client = NULL;
}

TEST(tcp_close_pending_recv) {
    pend_ctx  c;
    zn_State *S;

    memset(&c, 0, sizeof(c));
    S = zt_open();
    c.S = S;
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, pend_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          pend_connect, &c);
    assertok(c.client != NULL);

    asserteq(zt_run(S), 0);
    asserteq(c.accepted, 1);
    asserteq(c.connected, 1);
    asserteq(c.recv_called, 0); /* the delete cancelled it */
    assertok(c.client == NULL);
    asserteq(S->waitings, 0);
    if (c.server != NULL) zn_deltcp(c.server);
    if (c.listener != NULL) zn_delaccept(c.listener);
    zt_close(S);
}

/* ---- del from inside each callback ---- */

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

static void close_srv_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
}

static void close_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    close_ctx *c = (close_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    /* mode 2 needs data so the client's recv is not left pending */
    if (c->mode == 2) asserteq(zn_send(tcp, "x", 1, close_srv_send, c), ZN_OK);
}

static void close_cli_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    if (c->mode == 1) {
        zn_deltcp(tcp);
        c->client = NULL;
    }
}

static void close_cli_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 1);
    assertok(memcmp(c->buf, "x", 1) == 0);
    if (c->mode == 2) {
        zn_deltcp(tcp);
        c->client = NULL;
    }
}

static void close_connect(void *ud, zn_Tcp *tcp, int err) {
    close_ctx *c = (close_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    if (c->mode == 0) {
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
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, close_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          close_connect, &c);
    assertok(c.client != NULL);

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

static void eof_srv_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    eof_ctx *c = (eof_ctx *)ud;
    asserteq(err, ZN_OK);
    asserteq(cnt, 3);
    /* everything is out; closing the server side is what the client must
     * observe as ZN_ECLOSED on its next recv */
    zn_deltcp(tcp);
    c->server = NULL;
}

static void eof_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    eof_ctx *c = (eof_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    asserteq(zn_send(tcp, "bye", 3, eof_srv_send, c), ZN_OK);
}

static void eof_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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

static void eof_connect(void *ud, zn_Tcp *tcp, int err) {
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
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, eof_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          eof_connect, &c);
    assertok(c.client != NULL);

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

static void many_srv_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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

static void many_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    many_ctx *c = (many_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    c->server = tcp;
    asserteq(zn_recv(tcp, c->rbuf, 1, many_srv_recv, c), ZN_OK);
}

static void many_cli_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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

static void many_connect(void *ud, zn_Tcp *tcp, int err) {
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
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, many_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          many_connect, &c);
    assertok(c.client != NULL);

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

/* A refused connect must fail, never report success: select treats writability
 * as success, so znB_onconnect() reads SO_ERROR there (kqueue gets
 * EV_EOF/EV_ERROR).  Fixed 2026-10-28, see notes/changes.md §1.2.  The kernel
 * may also refuse synchronously, hence the two branches. */
typedef struct refused_ctx {
    int called;
    int err;
} refused_ctx;

static void refused_connect(void *ud, zn_Tcp *tcp, int err) {
    refused_ctx *c = (refused_ctx *)ud;
    (void)tcp;
    c->called = 1;
    c->err = err;
}

TEST(tcp_connect_refused) {
    zn_State   *S = zt_open();
    zn_Accept  *listener;
    zn_Tcp     *tcp;
    refused_ctx c;
    unsigned    port;

    memset(&c, 0, sizeof(c));
    listener = zn_listen(S, "127.0.0.1", 0);
    assertok(listener != NULL);
    port = zt_accept_port(listener);
    assertok(port != 0);
    zn_delaccept(listener); /* nothing listens on that port any more */

    tcp = zn_connect(S, "127.0.0.1", port, refused_connect, &c);
    if (tcp != NULL) {
        /* loopback usually refuses asynchronously; the loop delivers it */
        asserteq(zn_lasterror(S), ZN_OK); /* the factory itself succeeded */
        asserteq(zt_run(S), 0);
        test_log("  refused connect: cb=%d err=%d (%s)\n", c.called, (int)c.err,
                 zn_strerror((int)c.err));
        asserteq(c.called, 1);
        assertok(c.err != ZN_OK);
        /* BROKEN: there is no fd left, and send/recv are state errors */
        assertok(!znS_valid(tcp));
        asserteq(zn_send(tcp, "x", 1, cb_noop_send, NULL), ZN_ESTATE);
        zn_deltcp(tcp);
    } else {
        /* the kernel refused synchronously: the factory reports it */
        asserteq(zn_lasterror(S), ZN_ECONNECT);
        asserteq(c.called, 0);
    }
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* A target the kernel rejects synchronously (broadcast has no TCP route, so
 * connect() fails instead of returning EINPROGRESS) exercises the factory's
 * failure path: no object, the reason in last_error, no slot or retain left
 * behind. */
#if !defined(_WIN32)
TEST(tcp_connect_unreachable) {
    zn_State  *S = zt_open();
    zn_Accept *probe;
    zn_Tcp    *tcp;

    tcp = zn_connect(S, "255.255.255.255", 80, cb_noop_connect, NULL);
    test_log("  connect broadcast: tcp=%p last_error=%d (%s)\n", (void *)tcp,
             zn_lasterror(S), zn_strerror(zn_lasterror(S)));
    assertok(tcp == NULL);
    asserteq(zn_lasterror(S), ZN_ECONNECT);
    asserteq(S->waitings, 0);

    /* the failed slot went back: a fresh factory call still succeeds */
    probe = zn_listen(S, "127.0.0.1", 0);
    asserteq(zn_lasterror(S), ZN_OK);
    assertok(probe != NULL);
    zn_delaccept(probe);
    zt_close(S);
}
#endif /* !_WIN32 */

/* A second listener cannot bind an address:port that is already taken.  The
 * failed factory returns NULL before handing anything out, so a retry on a
 * fresh port still works. */
TEST(tcp_listen_conflict) {
    zn_State  *S = zt_open();
    zn_Accept *a = zn_listen(S, "127.0.0.1", 0);
    zn_Accept *b;
    unsigned   port;

    assertok(a != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    port = zt_accept_port(a);
    assertok(port != 0);
    b = zn_listen(S, "127.0.0.1", port);
    assertok(b == NULL);
    asserteq(zn_lasterror(S), ZN_EBIND);

    b = zn_listen(S, "127.0.0.1", 0); /* the failed attempt leaked nothing */
    assertok(b != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    zn_delaccept(a);
    zn_delaccept(b);
    zt_close(S);
}

typedef struct retry_ctx {
    int accepted;
    int connected;
} retry_ctx;

static void retry_onaccept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    retry_ctx *c = (retry_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    zn_deltcp(tcp);
}

static void retry_onconnect(void *ud, zn_Tcp *tcp, int err) {
    retry_ctx *c = (retry_ctx *)ud;
    asserteq(err, ZN_OK);
    c->connected = 1;
    zn_deltcp(tcp);
}

/* a failed listen committed nothing and returned no object, so the retry is a
 * plain second factory call: it succeeds and its events are delivered. */
TEST(tcp_listen_retry_after_bind_failure) {
    zn_State  *S = zt_open();
    zn_Accept *busy = zn_listen(S, "127.0.0.1", 0);
    zn_Accept *retry;
    zn_Tcp    *client;
    retry_ctx  c;
    unsigned   port;

    memset(&c, 0, sizeof(c));
    assertok(busy != NULL);
    port = zt_accept_port(busy);
    assertok(port != 0);

    retry = zn_listen(S, "127.0.0.1", port);
    assertok(retry == NULL);
    asserteq(zn_lasterror(S), ZN_EBIND);
    retry = zn_listen(S, "127.0.0.1", 0);
    assertok(retry != NULL);
    asserteq(zn_accept(retry, retry_onaccept, &c), ZN_OK);

    client = zn_connect(S, "127.0.0.1", zt_accept_port(retry), retry_onconnect,
                        &c);
    assertok(client != NULL);

    asserteq(zt_run(S), 0);
    asserteq(c.connected, 1);
    asserteq(c.accepted, 1);
    asserteq(S->waitings, 0);
    zn_delaccept(busy);
    zn_delaccept(retry);
    zt_close(S);
}

/* L2 regression (fixed): the reactive znB_closetcp() used to guess "is a
 * result queued?" from can_*, so closing a send parked by EAGAIN left its
 * retain unreleased; S->waitings never reached 0 and zn_run(ZN_RUN_LOOP)
 * never returned.  close now cancels exactly the parked directions.
 * Only the result-queue backends have can_write and the parked-send state,
 * hence the guard; the reactive backend is identified the portable way:
 * POSIX without ZN_USE_SELECT. */
#if !defined(ZN_USE_SELECT) && !defined(_WIN32)

#define BLOCKED_SEND_SIZE 65536
#define BLOCKED_MAX_CBS   200

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

/* del both ends; the loop can only finish once every retain of the deleted
 * client has been released by the backend */
static void blocked_teardown(blocked_ctx *c, zn_Tcp *tcp) {
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
    /* log before deleting: zn_run(LOOP) does not return today, so the log in
     * the test body below is never reached while the bug is present */
    test_log("  parked send: cbs=%d bytes=%u can_write=%d handler=%d\n",
             c->send_cbs, c->bytes, (int)tcp->can_write,
             tcp->send_handler != NULL);
    blocked_teardown(c, tcp);
}

static void blocked_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
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

static void blocked_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
    blocked_ctx *c = (blocked_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->accepted = 1;
    /* deliberately never read: the client's send must run into EAGAIN */
    c->server = tcp;
}

static void blocked_connect(void *ud, zn_Tcp *tcp, int err) {
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
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, blocked_accept, &c), ZN_OK);
    c.client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                          blocked_connect, &c);
    assertok(c.client != NULL);

    /* regressed by L2: del did not cancel the parked send, so its retain
     * never came back and the loop could not return */
    ret = zt_run(S);
    test_log(
            "  blocked send: ret=%d blocked=%d cbs=%d bytes=%u waitings=%u "
            "accepted=%d\n",
            ret, c.blocked, c.send_cbs, c.bytes, S->waitings, c.accepted);
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
 * (can_* == 1) has its result queued and that result owns the retain.  del
 * must cancel exactly the first kind: cancelling a completing one releases
 * the retain twice (the queued result releases again), and cancelling none
 * leaks it.  The state comes from a real loopback pair: the accepted side has
 * nothing to read (recv parks) and a one-byte send completes at once (result
 * queued), so no manual dispatch is needed. */
typedef struct parked_ctx {
    int send_cbs; /* the queued result must never reach the callback */
} parked_ctx;

static void parked_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    parked_ctx *c = (parked_ctx *)ud;
    (void)tcp, (void)err, (void)cnt;
    c->send_cbs++;
}

TEST(tcp_close_parked_and_queued) {
    zt_pair    p;
    parked_ctx c;
    zn_Tcp    *tcp;
    char       rbuf[8];

    memset(&c, 0, sizeof(c));
    zt_pair_open(&p);
    tcp = p.server;

    /* empty socket: the recv parks with no result; one byte: the send
     * completes at once and queues its result */
    asserteq(zn_recv(tcp, rbuf, sizeof(rbuf), cb_noop_recv, NULL), ZN_OK);
    asserteq(zn_send(tcp, "x", 1, parked_send, &c), ZN_OK);
    asserteq(p.S->waitings, 2);

    zn_deltcp(tcp);
    p.server = NULL;
    asserteq(p.S->waitings, 1); /* only the parked recv was cancelled */
    asserteq(tcp->recv_handler == NULL, 1);
    assertok(tcp->send_handler != NULL);

    /* drop the client too (no armed direction: reclaimed at once), so the ring
     * holds exactly the object under test */
    zn_deltcp(p.client);
    p.client = NULL;
    asserteq(p.S->waitings, 1);

    /* the queued result still owns its retain, so del cannot reclaim yet */
    assertok(!znL_empty(&p.S->active_tcps));

    /* draining it releases the last retain, suppresses the callback and
     * completes the delete's reclaim exactly once */
    asserteq(znR_process(p.S, INT_MAX), 0);
    asserteq(p.S->waitings, 0);
    asserteq(c.send_cbs, 0);
    /* the ring is back to just the head: znL_empty() compares next with h */
    assertok(znL_empty(&p.S->active_tcps));
    zt_pair_close(&p);
}

/* del with a send result already queued.  The parked recv is cancelled, the
 * queued result keeps its retain, so the slot stays on the ring until the
 * result is drained; only then is it reclaimed, exactly once.  A double
 * reclaim leaves the free list pointing at itself, so the next two factory
 * calls return one slot.  Built through the public API: a real loopback
 * connection whose recv parks (empty socket) and whose one-byte send
 * completes (result queued). */
typedef struct queued_del_ctx {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[8];
    int        connected;
    int        accepted;
} queued_del_ctx;

static void queued_del_connect(void *ud, zn_Tcp *tcp, int err) {
    queued_del_ctx *c = (queued_del_ctx *)ud;
    (void)tcp;
    asserteq(err, ZN_OK);
    c->connected = 1;
}

static void queued_del_accept(void *ud, zn_Accept *accept, int err,
                              zn_Tcp *tcp) {
    queued_del_ctx *c = (queued_del_ctx *)ud;
    (void)accept;
    asserteq(err, ZN_OK);
    assertok(tcp != NULL);
    c->server = tcp;
    c->accepted = 1;
}

static void queued_del_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud, (void)tcp, (void)err, (void)cnt;
}

static void queued_del_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    (void)ud, (void)tcp, (void)err, (void)cnt;
}

/* still on the active ring?  lets a bad free list fail before it is used */
static int queued_del_live(zn_State *S, zn_Tcp *tcp) {
    zn_Tcp *cur;
    for (cur = S->active_tcps.next; cur != (zn_Tcp *)(void *)&S->active_tcps;
         cur = cur->next)
        if (cur == tcp) return 1;
    return 0;
}

TEST(tcp_close_queued_result_then_del) {
    queued_del_ctx c;
    zn_Tcp        *a, *b;
    unsigned       port;

    memset(&c, 0, sizeof(c));
    c.S = zt_open();
    c.listener = zn_listen(c.S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    port = zt_accept_port(c.listener);
    assertok(port != 0);
    asserteq(zn_accept(c.listener, queued_del_accept, &c), ZN_OK);
    c.client = zn_connect(c.S, "127.0.0.1", port, queued_del_connect, &c);
    assertok(c.client != NULL);
    asserteq(zt_run(c.S), 0);
    asserteq(c.connected, 1);
    asserteq(c.accepted, 1);

    /* empty socket: recv parks; one byte: send completes and queues */
    asserteq(zn_recv(c.server, c.buf, sizeof(c.buf), queued_del_recv, &c),
             ZN_OK);
    asserteq(zn_send(c.server, "x", 1, queued_del_send, &c), ZN_OK);
    asserteq(c.S->waitings, 2);

    zn_deltcp(c.server); /* cancels exactly the parked recv */
    asserteq(c.S->waitings, 1);
    /* the queued send result still owns its retain: del cannot reclaim yet */
    assertok(queued_del_live(c.S, c.server));

    (void)zn_run(c.S, ZN_RUN_CHECK); /* drains the queued send result */
    asserteq(c.S->waitings, 0);
    /* the delete is the one and only reclaim; the completion does not free
     * the slot a second time */
    assertok(!queued_del_live(c.S, c.server));

    a = zn_connect(c.S, "127.0.0.1", port, queued_del_connect, &c);
    b = zn_connect(c.S, "127.0.0.1", port, queued_del_connect, &c);
    assertok(a != NULL);
    assertok(b != NULL);
    assertok(a != b); /* a double reclaim hands the same slot out twice */

    zn_deltcp(a);
    zn_deltcp(b);
    zn_deltcp(c.client);
    zn_delaccept(c.listener);
    zt_close(c.S);
}

/* The recv mirror: the queued result owns the retain, so del cancels nothing
 * and leaves the object on the ring; draining it suppresses the callback and
 * is the one place the slot goes back.  The completing recv is produced on a
 * real pair: arm the recv on an empty socket (it parks), let the peer write
 * one byte, then drive the backend retry by hand so the completion is queued
 * without also dispatching it. */
typedef struct recvq_ctx {
    int recv_cbs; /* the queued result must never reach the callback */
} recvq_ctx;

static void recvq_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    recvq_ctx *c = (recvq_ctx *)ud;
    (void)tcp, (void)err, (void)cnt;
    c->recv_cbs++;
}

/* a loopback write can reach the receiving socket asynchronously; wait on the
 * fd directly so the completion is not dispatched by a poll */
static int recvq_wait_readable(int fd, unsigned ms) {
    zn_Time end = zn_time() + ms;
    while (zn_time() < end) {
        fd_set         rfds;
        struct timeval tv;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0) return 1;
    }
    return 0;
}

TEST(tcp_recv_result_after_close) {
    zt_pair   p;
    recvq_ctx c;
    zn_Tcp   *tcp;
    char      buf[8];

    memset(&c, 0, sizeof(c));
    zt_pair_open(&p);
    tcp = p.server;

    asserteq(zn_recv(tcp, buf, sizeof(buf), recvq_recv, &c), ZN_OK);
    assertok(write(p.client->socket, "y", 1) == 1);
    assertok(recvq_wait_readable(tcp->socket, 2000));
    znB_retryrecv(tcp); /* the data is there: queue the completion */
    asserteq(p.S->waitings, 1);
    asserteq(tcp->can_read, 1);
    assertok(tcp->recv_handler != NULL);

    zn_deltcp(tcp);
    p.server = NULL;
    /* drop the client too (no armed direction: reclaimed at once), so the ring
     * holds exactly the object under test */
    zn_deltcp(p.client);
    p.client = NULL;
    asserteq(p.S->waitings, 1); /* a queued result is not cancelled */

    /* the queued result still owns its retain, so del cannot reclaim yet */
    assertok(!znL_empty(&p.S->active_tcps));

    asserteq(znR_process(p.S, INT_MAX), 0);
    asserteq(p.S->waitings, 0);
    asserteq(c.recv_cbs, 0);
    assertok(znL_empty(&p.S->active_tcps));
    zt_pair_close(&p);
}

/* Both directions parked: del cancels two retains and no completion is
 * outstanding, so the slot goes back immediately.  A missed release would
 * leave waitings non-zero; a spurious one would underflow it. */
TEST(tcp_close_both_parked) {
    zn_State   *S = zt_open();
    zn_SockAddr raddr;
    zn_Tcp     *tcp;
    char        buf[8];
    int         fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    assertok(znP_parseaddr(&raddr, "127.0.0.1", 1));
    tcp = znS_newtcpfd(S, fd, &raddr); /* adopts and owns the fd */
    assertok(tcp != NULL);

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
    zn_deltcp(tcp);
    asserteq(S->waitings, 0);
    assertok(znL_empty(&S->active_tcps));
    zt_close(S);
}

/* A send parked by EAGAIN must complete once the socket is writable again:
 * znB_retrysend() releases the parked retain, re-issues through zn_send()
 * and the completion is delivered by znR_dispatch().  White box: a socketpair
 * filled to EAGAIN makes the park deterministic, and znS_newtcpfd() is the
 * only way to hand the object its fd (it adopts and registers it, so a
 * failure would close it). */
TEST(tcp_retry_send_after_eagain) {
    zn_State   *S = zt_open();
    zn_SockAddr raddr;
    zn_Tcp     *tcp;
    static char big[262144];
    int         sv[2];

    assertok(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assertok(znP_parseaddr(&raddr, "127.0.0.1", 1));
    tcp = znS_newtcpfd(S, sv[0], &raddr);
    assertok(tcp != NULL);
    znP_setnonblock(sv[1]);

    /* fill the socket buffer so the very first zn_send() parks */
    memset(big, 'x', sizeof(big));
    while (write(sv[0], big, sizeof(big)) > 0);
    asserteq(zn_send(tcp, big, sizeof(big), cb_noop_send, NULL), ZN_OK);
    assertok(tcp->send_handler != NULL);
    asserteq(tcp->can_write, 0);
    asserteq(S->waitings, 1);

    /* the peer drains: this is the edge the parked send is waiting for */
    while (read(sv[1], big, sizeof(big)) > 0);
    znB_retrysend(tcp);
    assertok(tcp->can_write == 1 || tcp->send_handler == NULL);
    asserteq(znR_process(S, INT_MAX), 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

/* A terminal error in one direction suppresses a completion that is already
 * queued for the other one, even though the user did not delete the object:
 * the fd is gone, so the send result is only cleaned up (notes/lifecycle-v2.md
 * §3.3 rule 1).  Arming tries the syscall at once on the reactive backends, so
 * the order in the queue is deterministic: the recv EOF first, the one-byte
 * send second. */
typedef struct brokenq_ctx {
    int recv_cbs;
    int send_cbs;
} brokenq_ctx;

static void brokenq_recv(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    brokenq_ctx *c = (brokenq_ctx *)ud;
    (void)tcp;
    (void)cnt;
    c->recv_cbs++;
    asserteq(err, ZN_ECLOSED);
}

static void brokenq_send(void *ud, zn_Tcp *tcp, int err, unsigned cnt) {
    brokenq_ctx *c = (brokenq_ctx *)ud;
    (void)tcp;
    (void)err;
    (void)cnt;
    c->send_cbs++;
}

TEST(tcp_broken_suppresses_queued_send) {
    zt_pair     p;
    brokenq_ctx c;
    char        buf[8];

    memset(&c, 0, sizeof(c));
    zt_pair_open(&p);

    /* the peer stops writing but stays open: the server reads EOF, and its
     * own one-byte send still succeeds */
    assertok(shutdown(p.client->socket, SHUT_WR) == 0);
    assertok(recvq_wait_readable(p.server->socket, 2000));

    asserteq(zn_recv(p.server, buf, sizeof(buf), brokenq_recv, &c), ZN_OK);
    asserteq(zn_send(p.server, "x", 1, brokenq_send, &c), ZN_OK);
    asserteq(p.S->waitings, 2);
    assertok(znS_valid(p.server));
    asserteq(p.server->send_result.err, ZN_OK); /* a success, not an error */

    asserteq(znR_process(p.S, INT_MAX), 0);

    /* the error is delivered, the queued success is not */
    asserteq(c.recv_cbs, 1);
    asserteq(c.send_cbs, 0);
    asserteq(p.S->waitings, 0);
    assertok(!znS_valid(p.server)); /* BROKEN, but still ours */
    assertok(!znL_empty(&p.S->active_tcps));

    zn_deltcp(p.client);
    p.client = NULL;
    zn_deltcp(p.server);
    p.server = NULL;
    assertok(znL_empty(&p.S->active_tcps));
    zn_delaccept(p.listener);
    p.listener = NULL;
    zt_close(p.S);
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
    zn_Accept *accept = zn_listen(S, "127.0.0.1", 0);
    int        ret;

    assertok(accept != NULL);
    asserteq(zn_accept(accept, cb_noop_accept, NULL), ZN_OK);
    asserteq(S->waitings, 1);

    ret = zn_accept(accept, cb_noop_accept, NULL);
    test_log("  accept twice: second zn_accept -> %d (%s), waitings=%u\n", ret,
             zn_strerror(ret), S->waitings);
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

# include <fcntl.h>      /* fcntl(), O_NONBLOCK */
# include <netinet/in.h> /* struct sockaddr_in */
# include <arpa/inet.h>  /* htons(), htonl() */

typedef struct sp_ctx {
    int     ok;
    int     err;
    zn_Tcp *tcp;
} sp_ctx;

static void sp_on_accept(void *ud, zn_Accept *accept, int err, zn_Tcp *tcp) {
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
static void sp_on_accept_rearm(void *ud, zn_Accept *accept, int err,
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
    int                fd = socket(AF_INET, SOCK_STREAM, 0);
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
    int           fd = sp_connect(port);
    lg.l_onoff = 1;
    lg.l_linger = 0;
    assertok(setsockopt(fd, SOL_SOCKET, SO_LINGER, (const void *)&lg,
                        (socklen_t)sizeof(lg))
             == 0);
    close(fd); /* RST: the completed connection dies before it is accepted */
}

/* the reactive backends handed a blocking listen socket to accept(), so this
 * used to hang the loop; the guard turns that into a failure, not a stall */
TEST(tcp_listener_is_nonblocking) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_listen(S, "127.0.0.1", 0);

    assertok(accept != NULL);
    assertok((fcntl(accept->socket, F_GETFL) & O_NONBLOCK) != 0);
    zn_delaccept(accept);
    zt_close(S);
}

TEST(tcp_accept_spurious_ready) {
    zn_State  *S = zt_open();
    zn_Accept *accept = zn_listen(S, "127.0.0.1", 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        fd;

    memset(&c, 0, sizeof(c));
    assertok(accept != NULL);
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
    zn_Accept *accept = zn_listen(S, "127.0.0.1", 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        fd;

    memset(&c, 0, sizeof(c));
    assertok(accept != NULL);
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
    zn_Accept *accept = zn_listen(S, "127.0.0.1", 0);
    sp_ctx     c;
    zt_Loop    L;
    unsigned   port;
    int        before, fd, i;

    memset(&c, 0, sizeof(c));
    assertok(accept != NULL);
    port = zt_accept_port(accept);
    asserteq(zn_accept(accept, sp_on_accept_rearm, &c), ZN_OK);

    for (i = 0; i < 20; ++i) sp_connect_and_abort(port);

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
 * znB_onsend/znB_onrecv consumed the operation before the syscall, so a
 * readiness report with no room/data dropped the send/recv silently (no
 * callback, retain gone).  TCP directions do have a park state -- the
 * reactive backends use it for exactly this case (znB_retrysend -> znB_send
 * -> EAGAIN -> can_write = 0), so select parks the same way instead of
 * reporting an error that the other backends do not report. */

#if defined(ZN_USE_SELECT)

typedef struct b9_ctx {
    int ok;
    int err;
} b9_ctx;

static void b9_on_io(void *ud, zn_Tcp *tcp, int err, unsigned count) {
    b9_ctx *c = (b9_ctx *)ud;
    (void)tcp;
    (void)count;
    if (err == ZN_OK)
        c->ok++;
    else
        c->err++;
}

/* the object has to own a socketpair end: znS_newtcpfd() adopts the fd for
 * the backend and closes it unless it returns a live tcp */
static zn_Tcp *b9_socketpair(zn_State *S, int sv[2]) {
    zn_SockAddr raddr;
    assertok(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    znP_setnonblock(sv[1]);
    assertok(znP_parseaddr(&raddr, "127.0.0.1", 1));
    return znS_newtcpfd(S, sv[0], &raddr);
}

TEST(tcp_select_send_spurious_ready) {
    zn_State   *S = zt_open();
    zn_Tcp     *tcp;
    b9_ctx      c;
    static char big[262144];
    int         sv[2];

    memset(&c, 0, sizeof(c));
    tcp = b9_socketpair(S, sv);
    assertok(tcp != NULL);

    /* fill the kernel send buffer so the first send() has to return EAGAIN */
    memset(big, 'x', sizeof(big));
    while (write(sv[0], big, sizeof(big)) > 0);
    asserteq(zn_send(tcp, big, sizeof(big), b9_on_io, &c), ZN_OK);
    asserteq(S->waitings, 1);

    znB_onsend(tcp, 0); /* write-ready, but the send buffer is full */

    /* parked, not dropped and not failed: the operation is still the
     * caller's, the retain is still held, and no callback ran */
    asserteq(c.ok, 0);
    asserteq(c.err, 0);
    assertok(tcp->send_handler != NULL);
    asserteq(S->waitings, 1);

    /* drain the peer: the same readiness now completes the parked send */
    while (read(sv[1], big, sizeof(big)) > 0);
    znB_onsend(tcp, 0);
    asserteq(c.ok, 1);
    asserteq(c.err, 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

TEST(tcp_select_recv_spurious_ready) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp;
    b9_ctx    c;
    char      buf[16];
    int       sv[2];

    memset(&c, 0, sizeof(c));
    tcp = b9_socketpair(S, sv);
    assertok(tcp != NULL);

    asserteq(zn_recv(tcp, buf, (unsigned)sizeof(buf), b9_on_io, &c), ZN_OK);
    asserteq(S->waitings, 1);

    znB_onrecv(tcp, 0); /* read-ready, but nothing to read */

    asserteq(c.ok, 0);
    asserteq(c.err, 0);
    assertok(tcp->recv_handler != NULL);
    asserteq(S->waitings, 1);

    assertok(write(sv[1], "x", 1) == 1);
    znB_onrecv(tcp, 0); /* the next report completes the parked recv */
    asserteq(c.ok, 1);
    asserteq(c.err, 0);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp);
    close(sv[1]);
    zt_close(S);
}

#endif /* ZN_USE_SELECT */

#include "tcp_test.gen.inc"
