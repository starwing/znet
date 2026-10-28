/* addrinfo_test.c -- zn_getaddrinfo: async DNS on a private worker thread.
 *
 * The result never arrives through a socket; the worker posts it with
 * zn_post(), so the loop has to be pumped until the handler runs.  The
 * hidden 20 ms tick of zt_begin/ZT_WAIT bounds every poll and turns a
 * worker that never posts into a normal test failure.
 *
 * zn_closeaddrinfo(S) must run before zn_close(S): the request keeps a
 * pointer to the state and a late zn_post() would touch freed memory
 * (TODO L11).  Waiting for the handler first also guarantees the worker has
 * already moved past the request, so nothing is still in flight.
 *
 * Only deterministic cases live here: a name that always exists
 * ("localhost"), a numeric literal, and a name in the reserved .invalid
 * TLD that every resolver rejects.  No fixed ports and no sleeps.
 *
 * The worker is process-global, so the last two tests pin its lifetime:
 * zn_closeaddrinfo(S) only drops that state's requests (the worker stays up
 * for the next state), while zn_closeaddrinfo(NULL) stops the worker and
 * must return to the caller -- the Win32 version used to call the CRT's
 * _endthreadex(), which terminates the calling thread, not the worker.
 *
 * zt_open() returns a state with the select backend's post socketpair
 * already in readfds and S->nfds raised past it, so a pure DNS test (no
 * sockets) still sees the worker's zn_post() wakeup.
 */

/* ztest.h first: it defines ZN_IMPLEMENTATION for znet.h.  zn_addrinfo.h
 * guards its implementation with its own zn_implemented macro (not znet.h's
 * znet_implemented), so it is still emitted after znet.h. */
#include "ztest.h"
#include "zn_addrinfo.h"

#define MAXINFO 16

static int         ai_done;
static unsigned    ai_err;
static unsigned    ai_count;
static zn_PeerInfo ai_infos[MAXINFO];

static void ai_reset(void) {
    ai_done = 0;
    ai_err = 0xFFFFFFFFu;
    ai_count = 0;
    memset(ai_infos, 0, sizeof(ai_infos));
}

/* the handler may see more than MAXINFO entries; keep the first MAXINFO */
static void ai_handler(void *ud, unsigned err, unsigned count,
                       zn_PeerInfo *infos) {
    unsigned i;
    (void)ud;
    ai_err = err;
    ai_count = count;
    if (infos != NULL) {
        for (i = 0; i < count && i < MAXINFO; ++i) ai_infos[i] = infos[i];
    }
    ai_done = 1;
}

/* start a lookup and pump the loop until the handler has run */
static void ai_resolve(zn_State *S, const char *node, int flags, unsigned ms) {
    zt_Loop L;

    ai_reset();
    asserteq(zn_getaddrinfo(S, node, "80", flags, ai_handler, NULL), ZN_OK);
    zt_begin(&L, S, ms);
    ZT_WAIT(L, ai_done);
    zt_end(&L);
}

/* ---- a real name ---- */

TEST(addrinfo_localhost) {
    zn_State *S = zt_open();
    unsigned i;

    ai_resolve(S, "localhost", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    assertok(ai_count >= 1);
    for (i = 0; i < ai_count && i < MAXINFO; ++i) {
        assertok(ai_infos[i].addr[0] != 0);
        assertok(ai_infos[i].port == 80);
    }

    zn_closeaddrinfo(S);
    zt_close(S);
}

/* ---- a numeric literal needs no resolver ---- */

TEST(addrinfo_literal_ipv4) {
    zn_State *S = zt_open();

    ai_resolve(S, "127.0.0.1", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    assertok(ai_count >= 1);
    assertstreq(ai_infos[0].addr, "127.0.0.1");
    assertok(ai_infos[0].port == 80);

    zn_closeaddrinfo(S);
    zt_close(S);
}

/* ---- failure is reported, not silently dropped ---- */

TEST(addrinfo_invalid_node) {
    zn_State *S = zt_open();

    /* .invalid is reserved (RFC 2606); every resolver rejects it */
    ai_resolve(S, "this.host.does.not.exist.invalid", ZN_TCP | ZN_IPV4, 2000);
    assertok(ai_err != 0);
    asserteq(ai_count, 0);
    assertok(zn_aierror(ai_err) != NULL);

    zn_closeaddrinfo(S);
    zt_close(S);
}

/* ---- two lookups on one state, in order ---- */

TEST(addrinfo_two_lookups) {
    zn_State *S = zt_open();

    ai_resolve(S, "127.0.0.1", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    assertok(ai_count >= 1);
    assertstreq(ai_infos[0].addr, "127.0.0.1");

    ai_resolve(S, "::1", ZN_TCP | ZN_IPV6, 1000);
    asserteq(ai_err, 0);
    assertok(ai_count >= 1);
    assertstreq(ai_infos[0].addr, "::1");

    zn_closeaddrinfo(S);
    zt_close(S);
}

/* ---- closing one state keeps the shared worker for the next one ---- */

TEST(addrinfo_close_state_keeps_worker) {
    zn_State *S = zt_open();

    ai_resolve(S, "127.0.0.1", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    zn_closeaddrinfo(S); /* S != NULL: only this state's requests */
    asserteq(znA_initalize, 1);
    zt_close(S);

    S = zt_open();
    ai_resolve(S, "localhost", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    assertok(ai_count >= 1);
    zn_closeaddrinfo(S);
    zt_close(S);
}

/* ---- zn_closeaddrinfo(NULL) stops the worker, not the caller ---- */

TEST(addrinfo_stop_all) {
    zn_State *S = zt_open();

    ai_resolve(S, "127.0.0.1", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    /* the watchdog turns "the worker never exits" into a failure, not a hang */
    zt_guard_start(ZT_TIMEOUT_MS);
    zn_closeaddrinfo(NULL);
    zt_guard_stop();
    asserteq(znA_initalize, 0);

    /* still on the caller thread: a fresh lookup restarts the worker */
    ai_resolve(S, "127.0.0.1", ZN_TCP | ZN_IPV4, 1000);
    asserteq(ai_err, 0);
    asserteq(znA_initalize, 1);

    zn_closeaddrinfo(S);
    zt_close(S);
}

#include "addrinfo_test.gen.inc"
