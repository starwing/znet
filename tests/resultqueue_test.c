/* resultqueue_test.c -- white-box tests for the reactive result queue.
 *
 * Only the epoll/kqueue backends use znR_*; select and IOCP deliver their
 * completions directly.  A result is a zn_Result embedded in a zn_Tcp
 * (send_result / recv_result); zn_onresult() runs the user handler and
 * releases the retain taken by zn_send()/zn_recv().  Objects may be deleted
 * from those handlers, so a delete is deferred while a result is queued.
 *
 * The interesting property is the batching contract (notes/backends.md:49-52):
 * one znR_process() iteration services a *snapshot* of the queue, and the
 * ZN_MAX_RESULT_LOOPS (= 100) cap counts iterations, not results.  A queue
 * that is filled once is therefore drained in one iteration; only a cascade
 * where servicing one result queues the next one hits the cap.  When it does,
 * znR_process() returns non-zero and znP_poll() must return 1 instead of
 * blocking in epoll_wait/kevent -- that is the fix from commit 4380d8b
 * (znet.h:2561, znet.h:2762).
 *
 * znet.h #undefs its internal zn_use_* backend selectors at the end of the
 * section that consumes them (znet.h:1138, :2143, :2402, :2597), so after
 * #include "ztest.h" they are gone on every backend: a test file cannot test
 * `zn_use_resultqueue` directly.  ZN_USE_SELECT is the only backend switch
 * that reaches both misc/gen_entries.lua (the -D mirror) and the compiler, so
 * it is what keys this suite; on select the generator emits no tests at all.
 * For an IOCP build the generator must also be given -D_WIN32 (the current
 * justfile only drives POSIX), because Windows has no result queue either.
 *
 * The dummy sockets below are real fds so the ZN_ERROR path in zn_onresult()
 * has something to close.  The ZN_OK path never touches the socket and never
 * reads the send buffer, so a shared static buffer is enough.
 */

#include "ztest.h"

#if !defined(ZN_USE_SELECT) && !defined(_WIN32)

#define RQ_BATCH 120 /* ZN_MAX_RESULT_LOOPS + 20 */

static int      g_calls;
static unsigned g_last_err;
static unsigned g_last_count;
static zn_Tcp  *g_last_tcp;
static char     g_payload[4] = { 'a', 'b', 'c', 0 };

/* cascade state for rq_poll_returns_one_when_backlog */
static zn_State *g_chain_S;
static zn_Tcp   *g_chain[RQ_BATCH];
static int       g_chain_n;
static int       g_chain_i;

static void rq_reset(void) {
    g_calls = 0;
    g_last_err = 0xFFFFFFFFu;
    g_last_count = 0xFFFFFFFFu;
    g_last_tcp = NULL;
}

static void rq_on_send(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    (void)ud;
    g_calls++;
    g_last_tcp = tcp;
    g_last_err = err;
    g_last_count = count;
}

/* one tcp owning a real but never used socket, with a send operation ready to
 * complete through the result queue */
static zn_Tcp *rq_new(zn_State *S, unsigned len) {
    zn_Tcp *tcp = zn_newtcp(S);
    int     fd;
    if (tcp == NULL) return NULL;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        zn_deltcp(tcp);
        return NULL;
    }
    tcp->socket = fd;
    tcp->send_handler = rq_on_send;
    tcp->send_buffer.buf = g_payload;
    tcp->send_buffer.len = len;
    return tcp;
}

/* queue one completion the way the backend does; the retain is the operation
 * that zn_onresult() releases */
static void rq_queue(zn_State *S, zn_Tcp *tcp, int err) {
    zn_retain(S);
    znR_add(S, err, &tcp->send_result);
}

static int rq_count_queued(zn_State *S) {
    zn_Result *r;
    int        n = 0;
    for (r = S->result_queue.first; r != NULL; r = r->next) n++;
    return n;
}

/* queue the next result from inside a handler: one result per iteration */
static void rq_on_send_chain(void *ud, zn_Tcp *tcp, unsigned err,
                             unsigned count) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)count;
    g_calls++;
    if (g_chain_i + 1 < g_chain_n) {
        ++g_chain_i;
        zn_retain(g_chain_S);
        znR_add(g_chain_S, ZN_OK, &g_chain[g_chain_i]->send_result);
    }
}

/* ---- basics ---- */

TEST(rq_empty) {
    zn_State *S = zt_open();
    assertok(znQ_empty(&S->result_queue));
    asserteq(znR_process(S, 0), 0);
    asserteq(znR_process(S, 1), 0);
    asserteq(S->waitings, 0);
    zt_close(S);
}

TEST(rq_one) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp;

    rq_reset();
    tcp = rq_new(S, 3);
    assertok(tcp != NULL);
    rq_queue(S, tcp, ZN_OK);
    asserteq(S->waitings, 1);

    asserteq(znR_process(S, 0), 0);
    asserteq(g_calls, 1);
    asserteq(g_last_tcp, tcp);
    asserteq(g_last_err, ZN_OK);
    asserteq(g_last_count, 3); /* OK: the handler sees the queued length */
    asserteq(S->waitings, 0);
    assertok(znQ_empty(&S->result_queue));

    zn_deltcp(tcp);
    zt_close(S);
}

/* 120 results queued up front.  One znR_process() iteration detaches the whole
 * queue snapshot, so they are all serviced in one batch and the loop cap is
 * not reached.  What must hold whatever the split is: every result serviced
 * exactly once, has_more == "queue still non-empty", and one outstanding
 * retain per still-queued result.  The second call always ends with the queue
 * empty.  The >ZN_MAX_RESULT_LOOPS cascade is in
 * rq_poll_returns_one_when_backlog.
 *
 * Observed on HEAD (kqueue, and the same code path on epoll): g_calls == 120,
 * queued == 0, r == 0 -- the whole snapshot goes in one iteration. */
TEST(rq_batch_limit) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp[RQ_BATCH];
    int       i, r, queued;

    rq_reset();
    for (i = 0; i < RQ_BATCH; ++i) {
        tcp[i] = rq_new(S, 2);
        if (tcp[i] == NULL) {
            test_skip("socket() failed after %d dummy sockets", i);
            zt_close(S); /* zn_close drains the queued results */
            return;
        }
        rq_queue(S, tcp[i], ZN_OK);
    }
    asserteq(S->waitings, RQ_BATCH);

    r = znR_process(S, 0);
    queued = rq_count_queued(S);
    asserteq(g_calls + queued, RQ_BATCH);
    asserteq(r, queued != 0);
    asserteq(S->waitings, (unsigned)queued);

    asserteq(znR_process(S, 0), 0);
    asserteq(g_calls, RQ_BATCH);
    asserteq(rq_count_queued(S), 0);
    asserteq(S->waitings, 0);

    for (i = 0; i < RQ_BATCH; ++i) zn_deltcp(tcp[i]);
    zt_close(S);
}

TEST(rq_process_all) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp[RQ_BATCH];
    int       i;

    rq_reset();
    for (i = 0; i < RQ_BATCH; ++i) {
        tcp[i] = rq_new(S, 2);
        if (tcp[i] == NULL) {
            test_skip("socket() failed after %d dummy sockets", i);
            zt_close(S);
            return;
        }
        rq_queue(S, tcp[i], ZN_OK);
    }
    asserteq(S->waitings, RQ_BATCH);

    asserteq(znR_process(S, 1), 0); /* all=1 ignores the loop cap */
    asserteq(g_calls, RQ_BATCH);
    asserteq(rq_count_queued(S), 0);
    asserteq(S->waitings, 0);

    for (i = 0; i < RQ_BATCH; ++i) zn_deltcp(tcp[i]);
    zt_close(S);
}

/* ---- error / close paths ---- */

TEST(rq_err_result) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp;

    rq_reset();
    tcp = rq_new(S, 3);
    assertok(tcp != NULL);
    rq_queue(S, tcp, ZN_ERROR);

    asserteq(znR_process(S, 0), 0);
    asserteq(g_calls, 1);
    asserteq(g_last_err, ZN_ERROR);
    asserteq(g_last_count, 0);
    /* zn_onresult() closes the tcp before reporting the error */
    asserteq(tcp->socket, ZN_INVALID_SOCKET);
    asserteq(S->waitings, 0);

    zn_deltcp(tcp); /* already closed: just returns the object to the pool */
    zt_close(S);
}

/* zn_close() must drain the result queue (znP_close calls znR_process(S, 1)
 * and asserts the queue is empty) without running a user handler for a socket
 * it just closed.  Do not touch S afterwards: it is freed. */
TEST(rq_close_with_queued) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp;

    rq_reset();
    tcp = rq_new(S, 3);
    assertok(tcp != NULL);
    rq_queue(S, tcp, ZN_OK);

    zn_close(S);
    asserteq(g_calls, 0);
}

TEST(rq_close_with_many_queued) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp[RQ_BATCH];
    int       i;

    rq_reset();
    for (i = 0; i < RQ_BATCH; ++i) {
        tcp[i] = rq_new(S, 2);
        if (tcp[i] == NULL) {
            test_skip("socket() failed after %d dummy sockets", i);
            zn_close(S);
            return;
        }
        rq_queue(S, tcp[i], ZN_OK);
    }
    asserteq(S->waitings, RQ_BATCH);

    zn_close(S); /* drains all 120, none of them calls back */
    asserteq(g_calls, 0);
}

/* ---- the commit-4380d8b regression ---- */

/* A genuine >ZN_MAX_RESULT_LOOPS cascade: each serviced result queues the next
 * one, so every znR_process() iteration drains exactly one result.  After 100
 * iterations one result (the next link) is still queued; znP_poll() must
 * report "still work to do" without entering epoll_wait/kevent (commit
 * 4380d8b, znet.h:2561, :2762).
 *
 * Note the shape: 120 results queued *at once* are drained by a single
 * iteration (see rq_batch_limit), so the cap can only be reached by a
 * cascade -- and then the backlog is one pending link, not 20 independently
 * queued results.  "100 serviced, 20 still queued" is unreachable under the
 * batch contract; the invariant is "cap iterations serviced, queue
 * non-empty". */
TEST(rq_poll_returns_one_when_backlog) {
    zn_State *S = zt_open();
    zn_Tcp   *tcp[RQ_BATCH];
    int       i, queued;

    rq_reset();
    g_chain_S = S;
    g_chain_n = RQ_BATCH;
    g_chain_i = 0;
    for (i = 0; i < RQ_BATCH; ++i) {
        tcp[i] = rq_new(S, 1);
        if (tcp[i] == NULL) {
            test_skip("socket() failed after %d dummy sockets", i);
            zt_close(S);
            return;
        }
        tcp[i]->send_handler = rq_on_send_chain;
        g_chain[i] = tcp[i];
    }
    rq_queue(S, tcp[0], ZN_OK);
    asserteq(S->waitings, 1);

    asserteq(znP_poll(S, 0), 1);
    asserteq(g_calls, ZN_MAX_RESULT_LOOPS);
    queued = rq_count_queued(S);
    asserteq(queued, 1); /* the cascade's next link */
    asserteq(S->waitings, (unsigned)queued);

    /* the loop would call znP_poll again; drain explicitly before closing */
    asserteq(znR_process(S, 1), 0);
    asserteq(g_calls, RQ_BATCH);
    asserteq(rq_count_queued(S), 0);
    asserteq(S->waitings, 0);

    for (i = 0; i < RQ_BATCH; ++i) zn_deltcp(tcp[i]);
    zt_close(S);
}

#endif /* !ZN_USE_SELECT && !_WIN32 */

#include "resultqueue_test.gen.inc"
