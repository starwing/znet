/* state_test.c -- zn_State lifecycle, userdata, retain/release, zn_post and
 * close-from-callback semantics.
 *
 * Covers: newstate/engine/close, userdata, retain/release accounting,
 * zn_strerror (whole ZN_ERRORS table plus unknown codes), monotonic zn_time,
 * an invalid run mode, empty ZN_RUN_CHECK, post basics (deferred, FIFO,
 * NULL handler, from another thread, from a timer), close from a post / timer
 * / tcp callback, close with a pending accept or an armed timer, double close
 * from a callback, and "no new objects after close begins".
 *
 * Loop driving: zt_run() is used only when the callback that ends the test
 * also ends the state (a post/tcp close), or when a timer fires while the poll
 * is already blocked.  A close from a timer that is already due at the start of
 * zn_run() would leave the poll with an empty heap (TODO T3), so
 * close_in_timer_callback arms a timer that is still in the future.
 * zn_run(ONCE) on an idle state blocks as well; every ONCE below has a pending
 * post to wake it.
 *
 * The post tests keep one inert listening socket open (see state_anchor):
 * select's znP_poll() only watches descriptors <= S->nfds, and znP_init() does
 * not raise nfds to cover the post socketpair, so on select a post cannot wake
 * a state that has no socket at all.  That backend bug has its own regression
 * test, post_wakes_loop_without_sockets, at the end of the file; the post
 * semantics tests below are not weakened by the anchor, they just run on a
 * state that select is able to poll.
 *
 * The last three tests assert desired behaviour the library does not have yet;
 * each aborts the process it runs in, so they are last.
 */

#include "ztest.h"

#ifndef _WIN32
# include <pthread.h>
# include <sys/resource.h>
#endif

/* ---- shared recorders ---- */

#define MAXORD 32

static int       g_posts;
static void     *g_post_ud;
static zn_State *g_post_state;
static int       g_order[MAXORD];
static int       g_timer_fired;
static int       g_recv_calls;
static zn_State *g_close_state;

static void reset_recorders(void) {
    g_posts = 0;
    g_post_ud = NULL;
    g_post_state = NULL;
    g_timer_fired = 0;
    g_recv_calls = 0;
    g_close_state = NULL;
    memset(g_order, 0, sizeof(g_order));
}

/* A socket object is what raises S->nfds on the select backend; without one,
 * select() never looks at the post socketpair and a queued post cannot wake
 * zn_run() (see post_wakes_loop_without_sockets).  The listener is never armed
 * for accept, so it produces no callbacks and holds no retain; zn_close()
 * deletes it with every other object.  Port 0 keeps the test self-contained. */
static int state_anchor(zn_State *S) {
    zn_Accept *a = zn_newaccept(S, 0);
    if (a == NULL) return 0;
    return zn_listen(a, "127.0.0.1", 0) == ZN_OK;
}

/* ---- timer handler shared by several tests ----
 * Declared before the post handlers because cb_post_new_objects() also has to
 * pass a timer handler to zn_newtimer(). */

static zn_Time cb_timer_count(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    g_timer_fired++;
    return 0;
}

/* ---- post handlers ---- */

static void cb_post_record(void *ud, zn_State *S) {
    g_posts++;
    g_post_ud = ud;
    g_post_state = S;
}

static void cb_post_order(void *ud, zn_State *S) {
    (void)S;
    if (g_posts < MAXORD) g_order[g_posts] = (int)(size_t)ud;
    g_posts++;
}

static void cb_post_close(void *ud, zn_State *S) {
    (void)ud;
    g_posts++;
    zn_close(S); /* only marks CLOSING_IN_RUN; the poll finishes the close */
}

static void cb_post_close_twice(void *ud, zn_State *S) {
    (void)ud;
    g_posts++;
    zn_close(S);
    asserteq(S->status, ZN_STATUS_CLOSING_IN_RUN);
    zn_close(S); /* must see CLOSING_IN_RUN and return without doing anything */
}

static void cb_post_new_objects(void *ud, zn_State *S) {
    (void)ud;
    g_posts++;
    zn_close(S);
    assertok(zn_newtcp(S) == NULL);
    assertok(zn_newaccept(S, 0) == NULL);
    assertok(zn_newudp(S, "127.0.0.1", 0) == NULL);
    assertok(zn_newtimer(S, cb_timer_count, NULL) == NULL);
}

/* ---- timer handlers ---- */

static zn_Time cb_timer_post(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)timer;
    (void)elapsed;
    asserteq(zn_post((zn_State *)ud, cb_post_order, (void *)(size_t)7), ZN_OK);
    return 0;
}

static zn_Time cb_timer_close(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    zn_close(g_close_state);
    return 0;
}

/* ---- tcp loopback pair: close from the server's recv callback ---- */

static const char g_byte = 'x';
static char       g_srv_buf[8];

static void cb_send_done(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)count;
}

static void cb_connect_send(void *ud, zn_Tcp *tcp, unsigned err) {
    (void)ud;
    if (err != ZN_OK) return;
    asserteq(zn_send(tcp, &g_byte, 1, cb_send_done, NULL), ZN_OK);
}

static void cb_recv_close(void *ud, zn_Tcp *tcp, unsigned err, unsigned count) {
    (void)ud;
    (void)tcp;
    (void)err;
    (void)count;
    g_recv_calls++;
    zn_close(g_close_state);
}

static void cb_accept_recv(void *ud, zn_Accept *accept, unsigned err,
                           zn_Tcp *tcp) {
    (void)ud;
    (void)accept;
    if (err != ZN_OK || tcp == NULL) return;
    asserteq(zn_recv(tcp, g_srv_buf, (unsigned)sizeof(g_srv_buf),
                     cb_recv_close, NULL), ZN_OK);
}

static void cb_accept_noop(void *ud, zn_Accept *accept, unsigned err,
                           zn_Tcp *tcp) {
    (void)ud;
    (void)accept;
    (void)err;
    (void)tcp;
}

/* ---- helper thread ---- */

#ifndef _WIN32

typedef struct state_thread {
    zn_State *S;
    int       posts;
} state_thread;

void *state_thread_main(void *arg) {
    state_thread *t = (state_thread *)arg;
    int           i;
    for (i = 0; i < t->posts; ++i)
        (void)zn_post(t->S, cb_post_record, NULL);
    return NULL;
}

#endif /* !_WIN32 */

/* ---- state lifecycle ---- */

TEST(state_lifecycle) {
    zn_State   *S = zt_open();
    const char *engine = zn_engine();

    assertok(S != NULL);
    assertok(engine != NULL);
    assertok(engine[0] != '\0');
    test_log("  engine: %s\n", engine);
    zt_close(S);
}

TEST(state_userdata) {
    zn_State *S = zt_open();
    int       dummy = 7;

    assertok(zn_getuserdata(S) == NULL);
    zn_setuserdata(S, &dummy);
    assertok(zn_getuserdata(S) == &dummy);
    zn_setuserdata(S, NULL);
    assertok(zn_getuserdata(S) == NULL);
    zt_close(S);
}

TEST(state_retain_release) {
    zn_State *S = zt_open();

    asserteq(S->waitings, 0);
    asserteq(zn_retain(S), 1);
    asserteq(S->waitings, 1);
    asserteq(zn_retain(S), 2);
    asserteq(S->waitings, 2);
    asserteq(zn_release(S), 1);
    asserteq(S->waitings, 1);
    asserteq(zn_release(S), 0);
    asserteq(S->waitings, 0);
    zt_close(S);
}

TEST(state_strerror) {
    zn_State *S = zt_open();

    /* every code of the ZN_ERRORS table must map to its exact string */
#define X(name, str) assertstreq(zn_strerror(ZN_##name), str);
    ZN_ERRORS(X)
#undef X
    assertstreq(zn_strerror(ZN_ERROR_COUNT), "Unknown error");
    assertstreq(zn_strerror(-1), "Unknown error");
    assertstreq(zn_strerror(-ZN_ERROR), "Unknown error");
    zt_close(S);
}

TEST(state_time_monotonic) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t;
    zn_Time   t0, t1, before;

    reset_recorders();
    t0 = zn_time();
    t1 = zn_time();
    assertok(t1 >= t0);

    t = zn_newtimer(S, cb_timer_count, NULL);
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 25), ZN_OK);
    before = zn_time();
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_timer_fired == 1);
    zt_end(&L);
    assertok(zn_time() - before >= 10);

    zn_canceltimer(t);
    zn_deltimer(t);
    zt_close(S);
}

TEST(state_run_invalid_mode) {
    zn_State *S = zt_open();

    asserteq(zn_run(S, 12345), -ZN_ERROR);
    zt_close(S);
}

/* A fresh state has nothing to poll: ZN_RUN_CHECK must return 0 at once.
 * ZN_RUN_ONCE is not used here; on an idle state it blocks (TODO T3). */
TEST(state_run_check_empty) {
    zn_State *S = zt_open();

    asserteq(zn_run(S, ZN_RUN_CHECK), 0);
    asserteq(S->waitings, 0);
    zt_close(S);
}

/* ---- posts ---- */

TEST(post_basic) {
    zn_State *S = zt_open();
    int       ud = 0;

    assertok(state_anchor(S));
    reset_recorders();
    asserteq(zn_post(S, cb_post_record, &ud), ZN_OK);
    asserteq(g_posts, 0); /* deferred: zn_post never runs the handler itself */
    asserteq(zn_run(S, ZN_RUN_ONCE), 0);
    asserteq(g_posts, 1);
    assertok(g_post_ud == &ud);
    assertok(g_post_state == S);
    zt_close(S);
}

TEST(post_null_handler) {
    zn_State *S = zt_open();

    assertok(state_anchor(S));
    reset_recorders();
    asserteq(zn_post(S, NULL, NULL), ZN_OK);
    asserteq(zn_run(S, ZN_RUN_ONCE), 0); /* must not crash */
    asserteq(g_posts, 0);                /* and must not call anything */
    zt_close(S);
}

TEST(post_many) {
    zn_State *S = zt_open();
    int       i;

    assertok(state_anchor(S));
    reset_recorders();
    for (i = 0; i < 20; ++i)
        asserteq(zn_post(S, cb_post_order, (void *)(size_t)i), ZN_OK);
    asserteq(zn_run(S, ZN_RUN_ONCE), 0);
    asserteq(g_posts, 20);
    for (i = 0; i < 20; ++i) asserteq(g_order[i], i); /* post_queue is FIFO */
    zt_close(S);
}

#ifndef _WIN32

TEST(post_from_thread) {
    zn_State    *S = zt_open();
    zt_Loop      L;
    pthread_t    th;
    state_thread arg;

    assertok(state_anchor(S));
    reset_recorders();
    arg.S = S;
    arg.posts = 5;
    asserteq(pthread_create(&th, NULL, state_thread_main, &arg), 0);

    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_posts == 5);
    asserteq(pthread_join(th, NULL), 0);
    zt_end(&L);

    asserteq(g_posts, 5);
    zt_close(S);
}

TEST(post_from_thread_before_run) {
    zn_State    *S = zt_open();
    zt_Loop      L;
    pthread_t    th;
    state_thread arg;

    assertok(state_anchor(S));
    reset_recorders();
    arg.S = S;
    arg.posts = 5;
    asserteq(pthread_create(&th, NULL, state_thread_main, &arg), 0);
    asserteq(pthread_join(th, NULL), 0); /* all posts queued before any poll */

    asserteq(g_posts, 0);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_posts == 5);
    zt_end(&L);
    asserteq(g_posts, 5);
    zt_close(S);
}

#endif /* !_WIN32 */

/* A timer that is already due at the start of zn_run() posts from its handler;
 * the wake-up keeps the poll from sleeping on the now empty heap, so the post
 * runs before the run loop returns. */
TEST(post_in_timer_callback) {
    zn_State *S = zt_open();
    zn_Timer *t;

    assertok(state_anchor(S));
    reset_recorders();
    t = zn_newtimer(S, cb_timer_post, S);
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 0), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_posts, 1);
    asserteq(g_order[0], 7);
    zt_close(S);
}

/* ---- close from callbacks ---- */

TEST(close_in_post_callback) {
    zn_State *S = zt_open();

    assertok(state_anchor(S));
    reset_recorders();
    asserteq(zn_post(S, cb_post_close, NULL), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_posts, 1);
    /* zn_close() ran inside the callback and freed S: never touch it again. */
}

TEST(close_in_timer_callback) {
    zn_State *S = zt_open();
    zn_Timer *t;

    reset_recorders();
    g_close_state = S;
    t = zn_newtimer(S, cb_timer_close, NULL);
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 20), ZN_OK);
    /* still in the future: the poll returns on its timeout and the handler
     * runs after it, so the deferred close completes in this same znS_poll */
    asserteq(zt_run(S), 0);
    /* zn_close() freed S inside the loop: never touch it again. */
}

TEST(close_in_tcp_callback) {
    zn_State  *S = zt_open();
    zn_Accept *a;
    zn_Tcp    *client;
    unsigned   port;

    reset_recorders();
    g_close_state = S;

    a = zn_newaccept(S, 0);
    assertok(a != NULL);
    asserteq(zn_listen(a, "127.0.0.1", 0), ZN_OK);
    port = zt_accept_port(a);
    assertok(port != 0);
    asserteq(zn_accept(a, cb_accept_recv, NULL), ZN_OK);

    client = zn_newtcp(S);
    assertok(client != NULL);
    asserteq(zn_connect(client, "127.0.0.1", port, 0, cb_connect_send, NULL),
             ZN_OK);

    asserteq(zt_run(S), 0);
    asserteq(g_recv_calls, 1);
    /* the server recv handler called zn_close(): S is gone, do not touch it. */
}

TEST(close_with_pending_accept) {
    zn_State  *S = zt_open();
    zn_Accept *a = zn_newaccept(S, 0);

    assertok(a != NULL);
    asserteq(zn_listen(a, "127.0.0.1", 0), ZN_OK);
    asserteq(zn_accept(a, cb_accept_noop, NULL), ZN_OK);
    assertok(S->waitings > 0); /* the pending accept holds a retain */
    zn_close(S); /* cancels the accept and frees S; no loop run afterwards */
}

TEST(close_with_armed_timer) {
    zn_State *S = zt_open();
    zn_Timer *t = zn_newtimer(S, cb_timer_count, NULL);

    assertok(t != NULL);
    asserteq(zn_starttimer(t, 1000), ZN_OK);
    zn_close(S); /* znT_cleartimers() drops the heap; nothing may fire */
}

TEST(close_twice_from_callback) {
    zn_State *S = zt_open();

    assertok(state_anchor(S));
    reset_recorders();
    asserteq(zn_post(S, cb_post_close_twice, NULL), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_posts, 1);
}

/* ---- desired behaviour the library does not implement yet ---- */

/* select: znP_poll() watches fds 0..S->nfds, but znP_init() (znet.h:2116)
 * never raises S->nfds to cover the post socketpair (S->sockpairs[1]); only
 * znP_connect/znP_listen/znP_recvfrom do.  On a state with no socket object
 * select() therefore never reports the post pipe and zn_post() cannot wake the
 * loop: zn_run() sleeps until some other timer/poll timeout (ZN_FOREVER - now,
 * ~49 days, TODO T3) and the queued post never runs.
 * Desired behaviour: zn_post() wakes zn_run() on its own.
 * TODO (new bug, no TODO.md ID): in znP_init() raise nfds to sockpairs[1]
 * (e.g. `if (S->nfds < S->sockpairs[1]) S->nfds = S->sockpairs[1];`).
 * Kept last: it fails (times out) instead of passing. */
TEST(post_wakes_loop_without_sockets) {
    zn_State *S = zt_open();
    zt_Loop   L;

    reset_recorders();
    asserteq(zn_post(S, cb_post_record, NULL), ZN_OK);
    /* zt_begin arms a 20 ms tick, so each poll is bounded and a backend that
     * never dispatches the post fails after the 1000 ms deadline instead of
     * sleeping in select() for weeks. */
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_posts == 1);
    zt_end(&L);
    asserteq(g_posts, 1);
    zt_close(S);
}

/* AGENTS.md invariant 7: once zn_close() has started, every zn_new* must
 * return NULL.  zn_newtcp/newaccept/newudp do that through ZN_GETOBJECT, but
 * zn_newtimer() -> znT_newtimer() (znet.h:417) has no status check and still
 * hands out a timer.  Desired behaviour: zn_newtimer() returns NULL when
 * S->status > ZN_STATUS_READY, exactly like ZN_GETOBJECT.
 * TODO (new bug, no TODO.md ID): add the S->status guard to zn_newtimer().
 * Kept last: it aborts the process. */
TEST(objects_after_close_begins) {
    zn_State *S = zt_open();

    assertok(state_anchor(S));
    reset_recorders();
    asserteq(zn_post(S, cb_post_new_objects, NULL), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_posts, 1);
}

/* B1: epoll's znP_signal() (znet.h:2407) reverses the eventfd_write() result,
 * so a successfully queued post reports ZN_ERROR.  Desired behaviour:
 * zn_post() returns ZN_OK and the handler runs.  Kept last: it aborts.
 *
 * `#ifdef zn_use_backend_epoll` cannot be used: znet.h #undef's that macro in
 * its implementation pass (znet.h:2402), so it is never visible here.  Use the
 * backend selection condition instead (see tests/README.md): Linux + epoll.
 * The justfile hands __linux__ to gen_entries.lua, so the runner table and the
 * compiler agree: compiled only on the Linux native (epoll) build. */
#if defined(__linux__) && !defined(ZN_USE_SELECT)

TEST(post_returns_ok_epoll) {
    zn_State *S = zt_open();
    zt_Loop   L;

    reset_recorders();
    asserteq(zn_post(S, cb_post_record, NULL), ZN_OK);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_posts == 1);
    zt_end(&L);
    assertok(g_post_state == S);
    zt_close(S);
}

#endif /* __linux__ && !ZN_USE_SELECT */

/* zn_initialize/zn_deinitialize are no-ops on POSIX but real on Windows;
 * exercise both so the entry points stay covered in every configuration. */
TEST(state_initialize_deinitialize) {
    zn_State *S;

    zn_deinitialize();
    zn_initialize();
    S = zt_open();
    zt_close(S);
    zn_deinitialize();
    zn_initialize();
}

/* zn_newstate() must free the partially built state when the backend cannot
 * open its wakeup socketpair.  RLIMIT_NOFILE=0 makes socketpair()/eventfd()
 * fail deterministically; the limit is process-local and restored before any
 * assertion can abort. */
#ifndef _WIN32
TEST(state_newstate_init_failure) {
    struct rlimit old, lim;
    zn_State     *S;

    if (getrlimit(RLIMIT_NOFILE, &old) != 0)
        test_skip("getrlimit(RLIMIT_NOFILE) unavailable");
    lim = old;
    lim.rlim_cur = 0;
    if (setrlimit(RLIMIT_NOFILE, &lim) != 0)
        test_skip("cannot lower RLIMIT_NOFILE");
    S = zn_newstate();
    if (setrlimit(RLIMIT_NOFILE, &old) != 0)
        zt_fail("cannot restore RLIMIT_NOFILE");
    assertok(S == NULL);
}
#endif

#include "state_test.gen.inc"
