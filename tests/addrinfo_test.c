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
 * KNOWN FAILURE on the select backend (new bug, not in TODO.md): the select
 * znP_init() FD_SETs S->sockpairs[1] but never raises S->nfds, and POSIX
 * select() ignores every descriptor >= nfds.  A pure DNS test has no socket,
 * so nfds stays 0, fd 0 is the only descriptor watched, and the worker's
 * zn_post() wakeup is never seen -> these tests time out.  Fix: raise
 * S->nfds in znP_init() right after FD_SET (znet.h:2128).  The tests stay
 * strict because the desired behaviour is "the result is always delivered".
 * The suite is not in the justfile SUITES list yet; run it with
 *   just --justfile tests/justfile test addrinfo
 * and add it to SUITES (plus README) to cover it in `check`.
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

#include "addrinfo_test.gen.inc"
