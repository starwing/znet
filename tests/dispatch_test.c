/* dispatch_test.c -- deleting/closing objects from dispatch callbacks.
 *
 * znet promises that callbacks are deferred and never reentrant, so a handler
 * may delete any object it likes, including the one whose callback is running
 * and another one that still has a pending operation.  A delete is postponed
 * while an operation is in flight (ZN_GETOBJECT/ZN_PUTOBJECT contract,
 * AGENTS.md invariants 3-5), which is what keeps the dispatch paths in
 * znB_dispatch() safe.
 *
 * Everything here is plain loopback TCP on port 0 (no fixed ports, no sleeps):
 * one listener, one or two clients, and a handler that closes/deletes.
 *
 * The last test is select-only and is the regression test for L1 (see
 * notes/changes.md 1.1): a callback closes another fd that is still queued in
 * the same select() batch, so znB_dispatch() later finds S->infos[fd] == NULL.
 * Before the fix that was a NULL dereference.  It stays last because a crash
 * would kill the rest of the process when the whole suite runs in one binary.
 */

#include "ztest.h"

#if !defined(_WIN32)
# include <poll.h>
#endif

typedef void dt_RecvCb(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

typedef struct dt_Ctx {
    zn_State  *S;
    zn_Accept *a;
    zn_Tcp    *c1;     /* first client */
    zn_Tcp    *c2;     /* second client, tests that need two */
    zn_Tcp    *srv[2]; /* accepted connections, in accept order */
    char       recv_buf[2][64];
    char       send_buf[2][1];
    unsigned   port;
    int        max_accept; /* 1 or 2: how many zn_accept() calls to arm */
    int        autosend;   /* clients send one byte when connect completes */
    int        naccepted;
    int        nconn;
    int        nrecv;
    int        nsent;
    int        ndeleted;
    int        accept_error;
    int        connect_error;
    dt_RecvCb *recv_cb; /* armed on accept; NULL: the test arms recv */
} dt_Ctx;

/* ---- callbacks ---- */

static void dt_on_accept(void *ud, zn_Accept *a, unsigned err, zn_Tcp *tcp) {
    dt_Ctx *c = (dt_Ctx *)ud;
    if (err != ZN_OK) {
        c->accept_error = (int)err;
        return;
    }
    if (c->naccepted >= 2) { /* not reached: arming is limited by max_accept */
        zn_deltcp(tcp);
        return;
    }
    c->srv[c->naccepted] = tcp;
    c->naccepted++;
    if (c->recv_cb != NULL) {
        int idx = c->naccepted - 1;
        asserteq(zn_recv(tcp, c->recv_buf[idx],
                         (unsigned)sizeof(c->recv_buf[idx]), c->recv_cb, c),
                 ZN_OK);
    }
    if (c->naccepted < c->max_accept)
        asserteq(zn_accept(a, dt_on_accept, c), ZN_OK);
}

static void dt_on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count);

static void dt_on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    dt_Ctx *c = (dt_Ctx *)ud;
    if (err != ZN_OK) {
        c->connect_error = (int)err;
        return;
    }
    c->nconn++;
    if (c->autosend)
        asserteq(zn_send(tcp, c->send_buf[0], 1, dt_on_send, c), ZN_OK);
}

static void dt_on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    dt_Ctx *c = (dt_Ctx *)ud;
    (void)tcp;
    (void)err;
    (void)count;
    c->nsent++;
}

/* delete the *other* accepted connection; it still has a pending recv, so the
 * delete is deferred until its own completion runs */
static void dt_on_recv_close_other(void *ud, zn_Tcp *tcp, unsigned err,
                                   unsigned count) {
    dt_Ctx *c = (dt_Ctx *)ud;
    zn_Tcp *other;
    (void)err;
    (void)count;
    c->nrecv++;
    other = (tcp == c->srv[0]) ? c->srv[1] : c->srv[0];
    if (other == NULL) return;
    if (other == c->srv[0])
        c->srv[0] = NULL;
    else
        c->srv[1] = NULL;
    c->ndeleted++;
    zn_closetcp(other);
    zn_deltcp(other);
}

/* delete the tcp whose callback is running */
static void dt_on_recv_delete_self(void *ud, zn_Tcp *tcp, unsigned err,
                                   unsigned count) {
    dt_Ctx *c = (dt_Ctx *)ud;
    (void)err;
    (void)count;
    c->nrecv++;
    c->ndeleted++;
    zn_closetcp(tcp);
    zn_deltcp(tcp);
}

/* close the whole state from a callback: zn_close() only flags the request
 * while zn_run() is on the stack; the loop does the real close afterwards */
static void dt_on_recv_close_state(void *ud, zn_Tcp *tcp, unsigned err,
                                   unsigned count) {
    dt_Ctx *c = (dt_Ctx *)ud;
    (void)tcp;
    (void)err;
    (void)count;
    c->nrecv++;
    zn_close(c->S);
}

/* ---- setup helpers ---- */

static void dt_start(dt_Ctx *c, int max_accept) {
    memset(c, 0, sizeof(*c));
    c->S = zt_open();
    c->max_accept = max_accept;
    c->a = zn_newaccept(c->S);
    assertok(c->a != NULL);
    asserteq(zn_listen(c->a, "127.0.0.1", 0), ZN_OK);
    c->port = zt_accept_port(c->a);
    assertok(c->port != 0);
    asserteq(zn_accept(c->a, dt_on_accept, c), ZN_OK);
}

static void dt_newclient(dt_Ctx *c, zn_Tcp **out) {
    *out = zn_newtcp(c->S);
    assertok(*out != NULL);
    asserteq(zn_connect(*out, "127.0.0.1", c->port, dt_on_connect, c), ZN_OK);
}

/* ---- portable tests ---- */

/* The first accepted connection's recv callback deletes the second accepted
 * connection, which still has a pending recv armed.  The delete must be
 * deferred (the object owns a queued zn_Result), the deferred completion must
 * run without calling the user handler for a closed socket, and every retain
 * must come back so the state is idle again.
 *
 * Only one client writes, so exactly one server fd is ready: the second one is
 * not in the current poll batch, which keeps this test portable.  The forced
 * same-batch variant is select-only, see the last test. */
TEST(dispatch_delete_other_in_callback) {
    dt_Ctx  c;
    zt_Loop L;

    dt_start(&c, 2);
    dt_newclient(&c, &c.c1);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.naccepted == 1 && c.nconn == 1);

    dt_newclient(&c, &c.c2);
    ZT_WAIT(L, c.naccepted == 2 && c.nconn == 2);

    /* both accepted sockets now have a pending recv */
    asserteq(zn_recv(c.srv[0], c.recv_buf[0], (unsigned)sizeof(c.recv_buf[0]),
                     dt_on_recv_close_other, &c),
             ZN_OK);
    asserteq(zn_recv(c.srv[1], c.recv_buf[1], (unsigned)sizeof(c.recv_buf[1]),
                     dt_on_recv_close_other, &c),
             ZN_OK);
    /* only the first accepted connection's peer sends */
    asserteq(zn_send(c.c1, c.send_buf[0], 1, dt_on_send, &c), ZN_OK);

    ZT_WAIT(L, c.nrecv == 1 && c.ndeleted == 1 && c.nsent == 1);
    zt_wait(&L, 60); /* let the deferred close result run */

    asserteq(c.accept_error, 0);
    asserteq(c.connect_error, 0);
    /* the first accepted connection fired, the second one is gone */
    assertok(c.srv[0] != NULL);
    assertok(c.srv[1] == NULL);
    /* the deferred delete released the pending recv's retain */
    asserteq(c.S->waitings, 0);
#if !defined(ZN_USE_SELECT) && !defined(_WIN32)
    /* only the reactive backends defer through a result queue */
    assertok(znQ_empty(&c.S->result_queue));
#endif

    zn_deltcp(c.srv[0]);
    zn_deltcp(c.c1);
    zn_deltcp(c.c2);
    zn_delaccept(c.a);
    zt_end(&L);
    zt_close(c.S);
}

/* A recv callback closes the state.  zn_run(LOOP) must notice, perform the
 * real close and return 0; nothing may touch S afterwards. */
TEST(dispatch_close_state_in_callback) {
    dt_Ctx c;

    dt_start(&c, 1);
    c.recv_cb = dt_on_recv_close_state;
    c.autosend = 1;
    dt_newclient(&c, &c.c1);

    asserteq(zt_run(c.S), 0); /* zn_close() freed S inside the loop */
    asserteq(c.nrecv, 1);
    asserteq(c.accept_error, 0);
    asserteq(c.connect_error, 0);
}

/* A recv callback deletes the tcp it is running for.  The run loop must still
 * terminate: that object was the last pending operation. */
TEST(dispatch_delete_self_in_callback) {
    dt_Ctx c;

    dt_start(&c, 1);
    c.recv_cb = dt_on_recv_delete_self;
    c.autosend = 1;
    dt_newclient(&c, &c.c1);

    asserteq(zt_run(c.S), 0);
    asserteq(c.nrecv, 1);
    asserteq(c.ndeleted, 1);
    asserteq(c.accept_error, 0);
    asserteq(c.connect_error, 0);

    /* the client and the listener are still alive after the loop */
    zn_deltcp(c.c1);
    zn_delaccept(c.a);
    zt_close(c.S);
}

/* ---- select only: same-batch fd deletion (L1 regression) ---- */

#if defined(ZN_USE_SELECT)

/* L1: a callback that closes another fd which is ready in the *same* select
 * batch must not be dispatched a second time.  znF_unregister() now clears the
 * fd from the batch (reg and result), and znB_dispatch() still skips a NULL
 * S->infos[fd] as a guard.  The deleted connection must not deliver a second
 * callback (nrecv == 1).
 *
 * Both server fds are made ready before the pump, and the batch is walked in
 * ascending fd order, so the callback for the lower (first accepted) fd always
 * runs first and always deletes the higher one that is still queued.
 */
TEST(dispatch_select_close_other_fd) {
    dt_Ctx  c;
    zt_Loop L;

    dt_start(&c, 2);
    dt_newclient(&c, &c.c1);
    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.naccepted == 1 && c.nconn == 1);
    dt_newclient(&c, &c.c2);
    ZT_WAIT(L, c.naccepted == 2 && c.nconn == 2);

    asserteq(zn_recv(c.srv[0], c.recv_buf[0], (unsigned)sizeof(c.recv_buf[0]),
                     dt_on_recv_close_other, &c),
             ZN_OK);
    asserteq(zn_recv(c.srv[1], c.recv_buf[1], (unsigned)sizeof(c.recv_buf[1]),
                     dt_on_recv_close_other, &c),
             ZN_OK);

    test_log(
            "dispatch_select_close_other_fd: both accepted sockets have a "
            "pending recv; writing one byte to each client socket so both "
            "server fds are ready in the same select batch.\n");
    test_log(
            "L1 regression: the first callback closes the other tcp; the "
            "second event must not be dispatched (its batch bits are gone).\n");
    asserteq((int)send(c.c1->socket, "x", 1, 0), 1);
    asserteq((int)send(c.c2->socket, "x", 1, 0), 1);
    usleep(50000);

    /* Wait (without consuming anything) until BOTH server fds are readable.
     * Loopback delivery is not synchronous with send(): the second byte can
     * become visible to the receiver a few milliseconds later, and if it is
     * missing from the select() batch the second event silently skips the
     * regression.  poll() cannot be satisfied by stale readiness: the bytes
     * stay queued until select dispatches the recv. */
    {
        struct pollfd pfd[2];
        zn_Time       deadline = zn_time() + 2000;
        int           pr;
        for (;;) {
            pfd[0].fd = c.srv[0]->socket;
            pfd[0].events = POLLIN;
            pfd[0].revents = 0;
            pfd[1].fd = c.srv[1]->socket;
            pfd[1].events = POLLIN;
            pfd[1].revents = 0;
            pr = poll(pfd, 2, 20);
            if (pr == 2) break;
            if (zn_time() > deadline)
                zt_fail("server fds never became readable together");
        }
    }

    /* the lower ready fd's callback deletes the other one */
    ZT_WAIT(L, c.nrecv >= 1);
    zt_wait(&L, 60);

    /* desired behaviour once L1 is fixed */
    asserteq(c.nrecv, 1);
    asserteq(c.ndeleted, 1);

    zn_deltcp(c.srv[0]);
    zn_deltcp(c.srv[1]);
    zn_deltcp(c.c1);
    zn_deltcp(c.c2);
    zn_delaccept(c.a);
    zt_end(&L);
    zt_close(c.S);
}

/* disarming a direction drops it from the current batch snapshot: a callback
 * that re-arms the same fd must not be re-entered from the old bits */
static void dt_on_accept_nop(void *ud, zn_Accept *a, unsigned err, zn_Tcp *t) {
    (void)ud;
    (void)a;
    (void)err;
    (void)t;
}

TEST(dispatch_select_disarm_drops_snapshot) {
    zn_State  *S = zt_open();
    zn_Accept *a = zn_newaccept(S);
    int        fd;
    assertok(a != NULL);
    asserteq(zn_listen(a, "127.0.0.1", 0), ZN_OK);
    fd = a->socket;
    asserteq(zn_accept(a, dt_on_accept_nop, NULL), ZN_OK);
    FD_SET(fd, &S->result[ZN_READ]);
    FD_SET(fd, &S->result[ZN_EXCEPT]);
    znB_disarmaccept(a);
    assertok(!FD_ISSET(fd, &S->result[ZN_READ]));
    assertok(!FD_ISSET(fd, &S->result[ZN_EXCEPT]));
    zn_delaccept(a);
    zt_close(S);
}

#endif /* ZN_USE_SELECT */

#if !defined(_WIN32)

/* ---- same-batch invalidation (L8 regression) ----
 *
 * A poll batch is a snapshot of readiness.  A connect callback may delete
 * another object whose connect completion is later in that same batch; the
 * mempool then returns the same slot to zn_newtcp().  Without invalidation the
 * stale batch entry finds the new object and reports ZN_OK for a connect that
 * has not completed (it is aimed at a closed port and must fail).
 *
 * Both client completions are queued before the first poll, and c1 is queued
 * before c2, so the batch order is deterministic without a sleep: readiness is
 * observed with poll() on the raw client fds, which does not consume the
 * backend's event.
 *
 * The second case closes and reconnects the same object instead of deleting
 * it; the stale entry then names the same zn_SocketInfo pointer. */

static unsigned dt_l8_deadport(void) {
    struct sockaddr_in sa;
    socklen_t          len = (socklen_t)sizeof(sa);
    unsigned           port;
    int                fd = socket(AF_INET, SOCK_STREAM, 0);
    assertok(fd >= 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001u);
    sa.sin_port = 0;
    asserteq(bind(fd, (struct sockaddr *)&sa, sizeof(sa)), 0);
    asserteq(getsockname(fd, (struct sockaddr *)&sa, &len), 0);
    port = (unsigned)ntohs(sa.sin_port);
    close(fd);
    return port;
}

static void dt_l8_wait_write(zn_Tcp *tcp) {
    struct pollfd p;
    p.fd = tcp->socket;
    p.events = POLLOUT;
    p.revents = 0;
    if (poll(&p, 1, 1000) != 1) zt_fail("client fd never became writable");
}

typedef struct dt_L8 {
    zn_State  *S;
    zn_Accept *a;
    zn_Tcp    *c1;
    zn_Tcp    *c2;
    zn_Tcp    *n;
    unsigned   port;
    unsigned   dead;
    int        mode; /* 0: delete + reuse, 1: close + reconnect */
    int        c1_calls;
    int        c2_calls;
    int        new_calls;
    int        new_err;
    int        accepted;
    int        done;
} dt_L8;

static void dt_l8_on_new(void *ud, zn_Tcp *tcp, unsigned err) {
    dt_L8 *c = (dt_L8 *)ud;
    (void)tcp;
    c->new_calls++;
    c->new_err = (int)err;
    c->done = 1;
}

static void dt_l8_on_accept(void *ud, zn_Accept *a, unsigned e, zn_Tcp *t) {
    dt_L8 *c = (dt_L8 *)ud;
    if (e != ZN_OK) return;
    c->accepted++;
    zn_deltcp(t);
    asserteq(zn_accept(a, dt_l8_on_accept, c), ZN_OK);
}

static void dt_l8_on_connect(void *ud, zn_Tcp *tcp, unsigned err) {
    dt_L8 *c = (dt_L8 *)ud;
    if (tcp != c->c1) {
        c->c2_calls++;
        return;
    }
    c->c1_calls++;
    assertok(err == ZN_OK);
    if (c->mode == 0) {
        zn_deltcp(c->c2);
        c->n = zn_newtcp(c->S);
        assertok(c->n != NULL);
        /* the pool must hand back the victim's slot for the stale entry to
         * matter; otherwise this test would pass without the fix */
        assertok(c->n == c->c2);
    } else {
        asserteq(zn_closetcp(c->c2), ZN_OK);
        c->n = c->c2;
    }
    asserteq(zn_connect(c->n, "127.0.0.1", c->dead, dt_l8_on_new, c), ZN_OK);
}

static void dt_l8_case(int mode) {
    dt_L8   c;
    zt_Loop L;

    memset(&c, 0, sizeof(c));
    c.mode = mode;
    c.S = zt_open();
    c.a = zn_newaccept(c.S);
    assertok(c.a != NULL);
    asserteq(zn_listen(c.a, "127.0.0.1", 0), ZN_OK);
    c.port = zt_accept_port(c.a);
    assertok(c.port != 0);
    c.dead = dt_l8_deadport();
    asserteq(zn_accept(c.a, dt_l8_on_accept, &c), ZN_OK);

    c.c1 = zn_newtcp(c.S);
    c.c2 = zn_newtcp(c.S);
    assertok(c.c1 != NULL && c.c2 != NULL);

    /* queue c1's completion first, then c2's: both are pending when the
     * test's first poll runs, and the batch order follows that order */
    asserteq(zn_connect(c.c1, "127.0.0.1", c.port, dt_l8_on_connect, &c),
             ZN_OK);
    dt_l8_wait_write(c.c1);
    asserteq(zn_connect(c.c2, "127.0.0.1", c.port, dt_l8_on_connect, &c),
             ZN_OK);
    dt_l8_wait_write(c.c2);

    zt_begin(&L, c.S, 2000);
    ZT_WAIT(L, c.done);
    zt_end(&L);

    /* c1 ran first and consumed c2; c2's stale event must not run */
    asserteq(c.c1_calls, 1);
    asserteq(c.c2_calls, 0);
    asserteq(c.new_calls, 1);
    asserteq(c.new_err, ZN_ERROR); /* the dead port must not report ZN_OK */
    assertok(c.accepted >= 1);

    if (c.n != NULL) zn_deltcp(c.n);
    zn_deltcp(c.c1);
    zn_delaccept(c.a);
    asserteq(c.S->waitings, 0);
    zt_close(c.S);
}

TEST(dispatch_l8_delete_and_reuse) { dt_l8_case(0); }
TEST(dispatch_l8_close_and_reconnect) { dt_l8_case(1); }

#endif /* !_WIN32 */

#include "dispatch_test.gen.inc"
