/* lifecycle_test.c -- the object contract in notes/lifecycle-v2.md.
 *
 * Covers §10 of the contract: factory parameter/state errors and
 * zn_lasterror(), a factory failure that does not leak, del while operations
 * are in flight, del inside the completion callbacks, the CONNECTING guard,
 * the terminal-error state, state close with pending work, and
 * zn_del*(NULL).  The queued-result variants live next to the reactive
 * white-box cases in tcp_test.c.
 *
 * Everything here is written against a real loopback pair: a listener, a
 * client factory call and the accepted tcp.  Objects come from the factories
 * only, never from a hand-built slot.
 */

#include "ztest.h"

/* ---- a loopback pair ---- */

typedef struct lt_Pair {
    zn_State  *S;
    zn_Accept *listener;
    zn_Tcp    *client;
    zn_Tcp    *server;
    char       buf[32];
    int        connected;
    int        accepted;
    int        recv_count;
    int        send_count;
    int        recv_err;
    int        accept_err;
} lt_Pair;

static void lt_on_connect(void *ud, zn_Tcp *tcp, int err) {
    lt_Pair *p = (lt_Pair *)ud;
    (void)tcp;
    if (err == ZN_OK) p->connected = 1;
}

static void lt_on_accept(void *ud, zn_Accept *a, int err, zn_Tcp *tcp) {
    lt_Pair *p = (lt_Pair *)ud;
    (void)a;
    p->accept_err = err;
    if (err != ZN_OK || tcp == NULL) return;
    p->server = tcp;
    p->accepted = 1;
}

static void lt_on_recv(void *ud, zn_Tcp *tcp, int err, unsigned count) {
    lt_Pair *p = (lt_Pair *)ud;
    (void)tcp;
    (void)count;
    p->recv_err = err;
    p->recv_count++;
}

static void lt_on_send(void *ud, zn_Tcp *tcp, int err, unsigned count) {
    lt_Pair *p = (lt_Pair *)ud;
    (void)tcp;
    (void)err;
    (void)count;
    p->send_count++;
}

static void lt_on_recvfrom(void *ud, zn_Udp *udp, int err, unsigned count,
                           const char *addr, unsigned port) {
    lt_Pair *p = (lt_Pair *)ud;
    (void)udp;
    (void)err;
    (void)count;
    (void)addr;
    (void)port;
    p->recv_count++;
}

static void lt_open(lt_Pair *p) {
    memset(p, 0, sizeof(*p));
    p->S = zt_open();
    p->listener = zn_listen(p->S, "127.0.0.1", 0);
    if (p->listener == NULL) zt_fail("zn_listen() failed");
    asserteq(zn_accept(p->listener, lt_on_accept, p), ZN_OK);
    p->client = zn_connect(p->S, "127.0.0.1", zt_accept_port(p->listener),
                           lt_on_connect, p);
    if (p->client == NULL) zt_fail("zn_connect() failed");
}

/* pump until the client is connected and the server accepted it */
static void lt_connect(lt_Pair *p) {
    zt_Loop L;
    zt_begin(&L, p->S, 2000);
    ZT_WAIT(L, p->connected == 1 && p->accepted == 1);
    zt_end(&L);
}

/* pump for a while: enough for a round of completions, but without assuming
 * the loop has anything left to do afterwards */
static void lt_pump(zn_State *S, unsigned ms) {
    zt_Loop L;
    zt_begin(&L, S, ms);
    zt_wait(&L, ms);
    zt_end(&L);
}

static void lt_finish(lt_Pair *p) {
    zn_deltcp(p->server);
    zn_deltcp(p->client);
    zn_delaccept(p->listener);
    zt_close(p->S);
}

/* ---- factory parameters and last_error ---- */

TEST(life_factory_params) {
    zn_State  *S = zt_open();
    zn_Accept *a;
    zn_Udp    *u;
    zn_Tcp    *t;
    lt_Pair    p;

    memset(&p, 0, sizeof(p));

    /* no state, no object, and nothing to record the reason in */
    assertok(zn_listen(NULL, "127.0.0.1", 0) == NULL);
    assertok(zn_connect(NULL, "127.0.0.1", 1, lt_on_connect, &p) == NULL);
    assertok(zn_bind(NULL, "127.0.0.1", 0) == NULL);
    asserteq(zn_lasterror(NULL), ZN_EPARAM);

    /* a fresh state has no failure to report */
    asserteq(zn_lasterror(S), ZN_OK);

    /* a NULL or unparsable address, or a port that cannot be one */
    assertok(zn_listen(S, NULL, 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_listen(S, "not-an-ip", 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_listen(S, "127.0.0.1", 70000u) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);

    assertok(zn_bind(S, NULL, 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_bind(S, "not-an-ip", 0) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);

    assertok(zn_connect(S, NULL, 1, lt_on_connect, &p) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    assertok(zn_connect(S, "not-an-ip", 1, lt_on_connect, &p) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);
    /* a tcp nobody can wait on is a parameter error too */
    assertok(zn_connect(S, "127.0.0.1", 1, NULL, NULL) == NULL);
    asserteq(zn_lasterror(S), ZN_EPARAM);

    /* success clears it again */
    a = zn_listen(S, "127.0.0.1", 0);
    assertok(a != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    u = zn_bind(S, "127.0.0.1", 0);
    assertok(u != NULL);
    asserteq(zn_lasterror(S), ZN_OK);
    t = zn_connect(S, "127.0.0.1", zt_accept_port(a), lt_on_connect, &p);
    assertok(t != NULL);
    asserteq(zn_lasterror(S), ZN_OK);

    zn_deltcp(t);
    zn_deludp(u);
    zn_delaccept(a);
    zt_close(S);
}

/* ---- a failed factory does not leak ---- */

TEST(life_factory_no_leak) {
    zn_State  *S = zt_open();
    zn_Accept *a;
    zn_Accept *b;
    unsigned   port;
    int        i;

    /* parameter failures never reach a slot */
    for (i = 0; i < 4; ++i) {
        assertok(zn_listen(S, "not-an-ip", 0) == NULL);
        assertok(znL_empty(&S->active_accepts));
    }

    /* a failure after the slot was taken (EBIND) returns it to the pool: the
     * ring holds exactly the one listener that did succeed */
    a = zn_listen(S, "127.0.0.1", 0);
    assertok(a != NULL);
    port = zt_accept_port(a);
    assertok(port != 0);
    for (i = 0; i < 4; ++i) {
        assertok(zn_listen(S, "127.0.0.1", port) == NULL);
        assertok(!znL_empty(&S->active_accepts));
        assertok(S->active_accepts.next == S->active_accepts.prev);
    }
    asserteq(zn_lasterror(S), ZN_EBIND);
    zn_delaccept(a);
    assertok(znL_empty(&S->active_accepts));

    /* and the pool still hands out two distinct slots */
    a = zn_listen(S, "127.0.0.1", 0);
    b = zn_listen(S, "127.0.0.1", 0);
    assertok(a != NULL && b != NULL && a != b);
    zn_delaccept(a);
    zn_delaccept(b);
    assertok(znL_empty(&S->active_accepts));
    zt_close(S);
}

/* ---- the same port twice ---- */

TEST(life_factory_conflict) {
    zn_State  *S = zt_open();
    zn_Accept *a = zn_listen(S, "127.0.0.1", 0);
    zn_Accept *b;
    zn_Udp    *u1;
    unsigned   port;

    assertok(a != NULL);
    port = zt_accept_port(a);
    assertok(port != 0);

    /* a taken port is EBIND, and the state is still usable afterwards */
    assertok(zn_listen(S, "127.0.0.1", port) == NULL);
    asserteq(zn_lasterror(S), ZN_EBIND);
    b = zn_listen(S, "127.0.0.1", 0);
    assertok(b != NULL);
    asserteq(zn_lasterror(S), ZN_OK);

    /* the same for udp */
    u1 = zn_bind(S, "127.0.0.1", 0);
    assertok(u1 != NULL);
    assertok(zn_bind(S, "127.0.0.1", zt_udp_port(u1)) == NULL);
    asserteq(zn_lasterror(S), ZN_EBIND);
    zn_deludp(u1);
    assertok(znL_empty(&S->active_udps)); /* the failed bind left no slot */
    zn_delaccept(b);
    zn_delaccept(a);
    zt_close(S);
}

/* ---- a factory refuses to create objects while the state closes ---- */

typedef struct lt_Close {
    int refused_listen;
    int refused_connect;
    int refused_bind;
    int code;
} lt_Close;

/* a post keeps the deferred close deterministic on every backend: it is
 * already pending when the loop starts, so the factories are called while the
 * state is CLOSING_IN_RUN. */
static void lt_post_close_state(void *ud, zn_State *S) {
    lt_Close *c = (lt_Close *)ud;
    zn_close(S); /* from a callback: the real close is deferred to the loop */
    c->refused_listen = zn_listen(S, "127.0.0.1", 0) == NULL;
    c->refused_connect = zn_connect(S, "127.0.0.1", 1, lt_on_connect, NULL)
                      == NULL;
    c->refused_bind = zn_bind(S, "127.0.0.1", 0) == NULL;
    c->code = zn_lasterror(S);
}

TEST(life_factory_refuses_closing_state) {
    lt_Close  c;
    zn_State *S = zt_open();

    memset(&c, 0, sizeof(c));
    asserteq(zn_post(S, lt_post_close_state, &c), ZN_OK);

    asserteq(zt_run(S), 0); /* the loop performs the close and returns */

    asserteq(c.refused_listen, 1);
    asserteq(c.refused_connect, 1);
    asserteq(c.refused_bind, 1);
    asserteq(c.code, ZN_ESTATE);
}

/* ---- del with a connect in flight ---- */

static void lt_cb_count_connect(void *ud, zn_Tcp *tcp, int err) {
    int *count = (int *)ud;
    (void)tcp;
    (void)err;
    (*count)++;
}

TEST(life_del_connecting) {
    zn_State  *S = zt_open();
    zn_Accept *listener = zn_listen(S, "127.0.0.1", 0);
    zn_Tcp    *tcp;
    zn_Tcp    *a;
    zn_Tcp    *b;
    int        calls = 0;

    assertok(listener != NULL);
    tcp = zn_connect(S, "127.0.0.1", zt_accept_port(listener),
                     lt_cb_count_connect, &calls);
    assertok(tcp != NULL);
    asserteq(S->waitings, 1); /* the armed connect owns one retain */

    /* give up ownership while the connect is still in flight: the retain
     * comes back, the callback never runs, the slot is reclaimed once */
    zn_deltcp(tcp);
    lt_pump(S, 60); /* let an owed completion arrive, if there is one */
    asserteq(S->waitings, 0);
    asserteq(calls, 0);
    assertok(znL_empty(&S->active_tcps));

    /* the pool is healthy: two factories hand out different slots */
    a = zn_connect(S, "127.0.0.1", 1, lt_cb_count_connect, &calls);
    b = zn_connect(S, "127.0.0.1", 1, lt_cb_count_connect, &calls);
    assertok(a != NULL && b != NULL && a != b);
    zn_deltcp(a);
    zn_deltcp(b);
    zn_delaccept(listener);
    zt_close(S);
}

/* del while both directions are armed: no completion is delivered and the
 * object is reclaimed exactly once */
TEST(life_del_with_send_and_recv) {
    lt_Pair p;

    lt_open(&p);
    lt_connect(&p);
    asserteq(zn_send(p.server, "x", 1, lt_on_send, &p), ZN_OK);
    asserteq(zn_recv(p.server, p.buf, sizeof(p.buf), lt_on_recv, &p), ZN_OK);
    asserteq(p.S->waitings, 2);

    zn_deltcp(p.server);
    p.server = NULL;
    lt_pump(p.S, 60);
    asserteq(p.S->waitings, 0);
    asserteq(p.send_count, 0); /* suppressed: ownership was gone */
    asserteq(p.recv_count, 0);

    zn_deltcp(p.client);
    p.client = NULL;
    assertok(znL_empty(&p.S->active_tcps));
    zn_delaccept(p.listener);
    zt_close(p.S);
}

/* ---- del inside the completion callbacks ---- */

static void lt_cb_connect_del(void *ud, zn_Tcp *tcp, int err) {
    int *calls = (int *)ud;
    (void)err;
    (*calls)++;
    zn_deltcp(tcp); /* the callback owns the object it was handed */
}

TEST(life_del_in_connect_cb) {
    zn_State  *S = zt_open();
    zn_Accept *listener = zn_listen(S, "127.0.0.1", 0);
    int        calls = 0;

    assertok(listener != NULL);
    assertok(zn_connect(S, "127.0.0.1", zt_accept_port(listener),
                        lt_cb_connect_del, &calls)
             != NULL);
    lt_pump(S, 60);
    asserteq(calls, 1);
    assertok(znL_empty(&S->active_tcps));
    zn_delaccept(listener);
    zt_close(S);
}

typedef struct lt_AcceptDel {
    zn_Accept *listener;
    int        accepted;
} lt_AcceptDel;

static void lt_cb_accept_del(void *ud, zn_Accept *a, int err, zn_Tcp *tcp) {
    lt_AcceptDel *c = (lt_AcceptDel *)ud;
    if (err != ZN_OK) return;
    c->accepted++;
    zn_deltcp(tcp);  /* the accepted tcp belongs to this callback now */
    zn_delaccept(a); /* ... and so does the listener it was armed on */
    c->listener = NULL;
}

TEST(life_del_in_accept_cb) {
    lt_AcceptDel c;
    zn_State    *S = zt_open();
    zn_Tcp      *client;
    int          calls = 0;

    memset(&c, 0, sizeof(c));
    c.listener = zn_listen(S, "127.0.0.1", 0);
    assertok(c.listener != NULL);
    asserteq(zn_accept(c.listener, lt_cb_accept_del, &c), ZN_OK);
    client = zn_connect(S, "127.0.0.1", zt_accept_port(c.listener),
                        lt_cb_count_connect, &calls);
    assertok(client != NULL);

    lt_pump(S, 60);
    asserteq(c.accepted, 1);
    asserteq(calls, 1);
    assertok(c.listener == NULL);
    assertok(znL_empty(&S->active_accepts));
    /* the client is still ours: nothing wrong happened to it */
    zn_deltcp(client);
    zt_close(S);
}

/* ---- CONNECTING is not CONNECTED ---- */

TEST(life_connecting_guards) {
    lt_Pair  p;
    zt_Loop  L;
    unsigned waitings;

    lt_open(&p);
    /* both directions refuse while the connect is in flight, and the refusal
     * neither consumes the object nor takes a retain */
    waitings = p.S->waitings;
    asserteq(zn_send(p.client, "x", 1, lt_on_send, &p), ZN_ESTATE);
    asserteq(zn_recv(p.client, p.buf, sizeof(p.buf), lt_on_recv, &p),
             ZN_ESTATE);
    asserteq(p.S->waitings, waitings);

    zt_begin(&L, p.S, 2000);
    ZT_WAIT(L, p.connected == 1 && p.accepted == 1);
    /* connected now: the same call is accepted */
    asserteq(zn_send(p.client, "x", 1, lt_on_send, &p), ZN_OK);
    zt_end(&L);
    lt_finish(&p);
}

/* ---- a terminal error closes the fd and leaves the object to del ---- */

TEST(life_terminal_error) {
    lt_Pair     p;
    zt_Loop     L;
    zn_PeerInfo info;

    lt_open(&p);
    lt_connect(&p);

    /* the server watches for the peer going away */
    asserteq(zn_recv(p.server, p.buf, sizeof(p.buf), lt_on_recv, &p), ZN_OK);
    zn_deltcp(p.client);
    p.client = NULL;

    zt_begin(&L, p.S, 2000);
    ZT_WAIT(L, p.recv_count == 1);
    asserteq(p.recv_err, ZN_ECLOSED);

    /* broken: fd closed, both directions refuse, the peer info survives */
    assertok(!znS_valid(p.server));
    asserteq(zn_send(p.server, "x", 1, lt_on_send, &p), ZN_ESTATE);
    asserteq(zn_recv(p.server, p.buf, sizeof(p.buf), lt_on_recv, &p),
             ZN_ESTATE);
    zn_getpeerinfo(p.server, &info);
    assertok(info.addr[0] != 0);
    assertok(info.port != 0);

    zn_deltcp(p.server);
    p.server = NULL;
    assertok(znL_empty(&p.S->active_tcps));
    zn_delaccept(p.listener);
    p.listener = NULL;
    zt_end(&L);
    zt_close(p.S);
}

/* ---- zn_del*(NULL) is a no-op ---- */

TEST(life_del_null) {
    zn_deltcp(NULL);
    zn_delaccept(NULL);
    zn_deludp(NULL);
}

/* ---- state close with one armed operation of every kind ---- */

TEST(life_close_with_pending) {
    lt_Pair p;
    zn_Udp *udp;
    zn_Tcp *connecting;
    int     recv_before;
    int     send_before;
    char    buf[8];

    lt_open(&p);
    lt_connect(&p);

    asserteq(zn_recv(p.server, p.buf, sizeof(p.buf), lt_on_recv, &p), ZN_OK);
    asserteq(zn_send(p.server, "x", 1, lt_on_send, &p), ZN_OK);
    asserteq(zn_accept(p.listener, lt_on_accept, &p), ZN_OK);
    udp = zn_bind(p.S, "127.0.0.1", 0);
    assertok(udp != NULL);
    asserteq(zn_recvfrom(udp, buf, sizeof(buf), lt_on_recvfrom, &p), ZN_OK);
    connecting = zn_connect(p.S, "127.0.0.1", 1, lt_cb_count_connect, NULL);
    assertok(connecting != NULL);

    recv_before = p.recv_count;
    send_before = p.send_count;
    assertok(p.S->waitings >= 5);

    /* the close cancels all of it: no callback runs, every retain comes back
     * (ASan/LSan watch the rest) */
    zn_close(p.S);
    asserteq(p.recv_count, recv_before);
    asserteq(p.send_count, send_before);
}

/* ---- del a CONNECTING tcp and then close the state ---- */

TEST(life_del_connecting_then_close) {
    zn_State  *S = zt_open();
    zn_Accept *listener = zn_listen(S, "127.0.0.1", 0);
    zn_Tcp    *tcp;
    int        calls = 0;

    assertok(listener != NULL);
    tcp = zn_connect(S, "127.0.0.1", zt_accept_port(listener),
                     lt_cb_count_connect, &calls);
    assertok(tcp != NULL);
    zn_deltcp(tcp);
    zn_delaccept(listener);
    zn_close(S); /* the aborted connect must be drained, not waited for */
    asserteq(calls, 0);
}

#include "lifecycle_test.gen.inc"
