/* timer_test.c -- timer heap semantics.
 *
 * Covers: ordering, 0 delay, same deadline, re-arm from the handler, delete
 * from the handler, cancel root/middle/last, cancel twice, cancel unarmed,
 * restart armed/after fire, nexttime/timeout accounting (T3/T4), the epoll
 * integer timeout clamp (B6), and the zn_Time width (T1/T2 truncation
 * regressions, skipped on 32-bit builds).
 *
 * Timer callbacks do not retain the state, so the tests drive the loop with
 * zt_begin/ZT_WAIT: the hidden 20 ms tick keeps every poll bounded.  zn_run()
 * blocks in poll when nothing is pending, so only timer_loop_exits_after_fire
 * uses zt_run() and it arms a far deadline first.
 */

#include "ztest.h"

#include <limits.h>

/* ---- recorder ---- */

#define MAXEV 64

static int     g_fired;
static int     g_order[MAXEV];
static zn_Time g_elapsed[MAXEV];

static void reset_events(void) {
    g_fired = 0;
    memset(g_order, 0, sizeof(g_order));
    memset(g_elapsed, 0, sizeof(g_elapsed));
}

static zn_Time cb_record(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)timer;
    if (g_fired < MAXEV) {
        g_order[g_fired] = (int)(size_t)ud;
        g_elapsed[g_fired] = elapsed;
    }
    g_fired++;
    return 0;
}

/* ---- ordering ---- */

TEST(timer_order) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *a, *b, *c;

    reset_events();
    a = zn_newtimer(S, cb_record, (void *)(size_t)1);
    b = zn_newtimer(S, cb_record, (void *)(size_t)2);
    c = zn_newtimer(S, cb_record, (void *)(size_t)3);
    assertok(a && b && c);
    asserteq(zn_starttimer(a, 60), ZN_OK);
    asserteq(zn_starttimer(b, 20), ZN_OK);
    asserteq(zn_starttimer(c, 40), ZN_OK);

    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 3);
    zt_end(&L);

    asserteq(g_order[0], 2);
    asserteq(g_order[1], 3);
    asserteq(g_order[2], 1);
    assertok(g_elapsed[0] >= 5);
    assertok(g_elapsed[2] <= 2000);
    zt_close(S);
}

TEST(timer_zero_delay) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_record, (void *)(size_t)7);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 0), ZN_OK);
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_fired == 1);
    zt_end(&L);
    asserteq(g_order[0], 7);
    assertok(g_elapsed[0] < 1000);
    zt_close(S);
}

/* Timers with the same deadline may fire in any order (T7); only the groups are
 * ordered.  Four timers at 30 ms and one at 60 ms: the whole 30 ms group must
 * come before the 60 ms one, and the 30 ms group must contain exactly 0..3. */
TEST(timer_fire_same_deadline) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t[5];
    int       i, seen[4];

    reset_events();
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 5; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        asserteq(zn_starttimer(t[i], i < 4 ? 30 : 60), ZN_OK);
    }
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 5);
    zt_end(&L);
    for (i = 0; i < 4; ++i) seen[g_order[i]] = 1;
    for (i = 0; i < 4; ++i) asserteq(seen[i], 1);
    /* the 60 ms group is strictly after the 30 ms one */
    asserteq(g_order[4], 4);
    zt_close(S);
}

/* ---- re-arm / delete from the handler ---- */

#define REARM_TIMES 3

static zn_Time cb_rearm(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    g_fired++;
    return g_fired < REARM_TIMES ? 20 : 0;
}

TEST(timer_rearm_in_handler) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_rearm, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 10), ZN_OK);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == REARM_TIMES);
    zt_wait(&L, 60);
    zt_end(&L);
    asserteq(g_fired, REARM_TIMES); /* handler returned 0 the last time */
    assertok(t->index == ZN_TIMER_NOINDEX);
    zt_close(S);
}

static zn_Time cb_del_self(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)elapsed;
    g_fired++;
    zn_deltimer(timer);
    return 0;
}

TEST(timer_del_in_handler) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_del_self, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 10), ZN_OK);
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_fired == 1);
    zt_wait(&L, 80); /* nothing else must fire, and no crash */
    zt_end(&L);
    asserteq(g_fired, 1);
    asserteq(S->ts.used, 0);
    zt_close(S);
}

/* ---- cancel variants ---- */

TEST(timer_cancel_root) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *a = zn_newtimer(S, cb_record, (void *)(size_t)1);
    zn_Timer *b = zn_newtimer(S, cb_record, (void *)(size_t)2);
    zn_Timer *c = zn_newtimer(S, cb_record, (void *)(size_t)3);

    reset_events();
    assertok(a && b && c);
    asserteq(zn_starttimer(a, 80), ZN_OK);
    asserteq(zn_starttimer(b, 30), ZN_OK);
    asserteq(zn_starttimer(c, 60), ZN_OK);
    zn_canceltimer(b); /* cancel the heap root */
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 2);
    zt_wait(&L, 80);
    zt_end(&L);
    asserteq(g_fired, 2);
    asserteq(g_order[0], 3);
    asserteq(g_order[1], 1);
    zt_close(S);
}

TEST(timer_cancel_mid) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *a = zn_newtimer(S, cb_record, (void *)(size_t)1);
    zn_Timer *b = zn_newtimer(S, cb_record, (void *)(size_t)2);
    zn_Timer *c = zn_newtimer(S, cb_record, (void *)(size_t)3);

    reset_events();
    assertok(a && b && c);
    asserteq(zn_starttimer(a, 80), ZN_OK);
    asserteq(zn_starttimer(b, 30), ZN_OK);
    asserteq(zn_starttimer(c, 120), ZN_OK);
    zn_canceltimer(a); /* cancel the middle node */
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 2);
    zt_wait(&L, 80);
    zt_end(&L);
    asserteq(g_fired, 2);
    asserteq(g_order[0], 2);
    asserteq(g_order[1], 3);
    zt_close(S);
}

/* Cancel a node whose replacement (the last heap slot) is smaller than the
 * removed node's parent: the moved timer has to sift *up* before the usual
 * sift-down.  A real arm/cancel sequence rarely produces that shape, so the
 * heap is built by hand from six timers. */
TEST(timer_cancel_sift_up) {
    zn_State *S = zt_open();
    zn_Timer *t[6];
    zn_Time   emit[6];
    int       i;

    emit[0] = 1;
    emit[1] = 10;
    emit[2] = 2;
    emit[3] = 12;
    emit[4] = 11;
    emit[5] = 5;
    assertok(znT_resize(&S->ts, 6));
    for (i = 0; i < 6; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        t[i]->start = 0;
        t[i]->emit = emit[i];
        t[i]->index = (unsigned)i;
        S->ts.keys[i] = emit[i], S->ts.nodes[i] = t[i];
    }
    S->ts.used = 6;
    S->ts.nexttime = emit[0];
    assertok(S->ts.size >= 6);

    /* removing slot 4 (emit 11) moves the last timer (emit 5) into slot 1,
     * which is above the old parent emit 10, so it must sift up */
    zn_canceltimer(t[4]);
    asserteq(S->ts.used, 5);
    asserteq(t[4]->index, ZN_TIMER_NOINDEX);
    asserteq(t[5]->index, 1);
    asserteq(S->ts.nodes[1], t[5]);
    asserteq(S->ts.keys[1], emit[5]);
    asserteq(S->ts.nodes[3], t[3]);
    asserteq(S->ts.nodes[4], t[1]);
    asserteq(S->ts.nexttime, emit[0]);
    zt_close(S);
}

TEST(timer_cancel_last) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *a = zn_newtimer(S, cb_record, (void *)(size_t)1);
    zn_Timer *b = zn_newtimer(S, cb_record, (void *)(size_t)2);
    zn_Timer *c = zn_newtimer(S, cb_record, (void *)(size_t)3);

    reset_events();
    assertok(a && b && c);
    asserteq(zn_starttimer(a, 80), ZN_OK);
    asserteq(zn_starttimer(b, 30), ZN_OK);
    asserteq(zn_starttimer(c, 120), ZN_OK);
    zn_canceltimer(c); /* cancel the last heap slot */
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 2);
    zt_wait(&L, 80);
    zt_end(&L);
    asserteq(g_fired, 2);
    asserteq(g_order[0], 2);
    asserteq(g_order[1], 1);
    zt_close(S);
}

TEST(timer_cancel_twice) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *a = zn_newtimer(S, cb_record, (void *)(size_t)1);
    zn_Timer *b = zn_newtimer(S, cb_record, (void *)(size_t)2);

    reset_events();
    assertok(a && b);
    asserteq(zn_starttimer(a, 80), ZN_OK);
    asserteq(zn_starttimer(b, 30), ZN_OK);
    zn_canceltimer(b);
    zn_canceltimer(b); /* second cancel must be a no-op */
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 1);
    zt_wait(&L, 80);
    zt_end(&L);
    asserteq(g_fired, 1);
    asserteq(g_order[0], 1);
    zt_close(S);
}

TEST(timer_cancel_unarmed) {
    zn_State *S = zt_open();
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);

    assertok(t != NULL);
    zn_canceltimer(t); /* never started */
    asserteq(t->index, ZN_TIMER_NOINDEX);
    asserteq(S->ts.used, 0);
    zn_deltimer(t);
    zt_close(S);
}

/* ---- restart ---- */

TEST(timer_restart_armed) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 10), ZN_OK);
    asserteq(zn_starttimer(t, 100), ZN_OK); /* re-arm before it fires */
    asserteq(S->ts.used, 1);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 1);
    zt_wait(&L, 120);
    zt_end(&L);
    asserteq(g_fired, 1);
    assertok(g_elapsed[0] >= 50); /* the 10 ms deadline was replaced */
    zt_close(S);
}

TEST(timer_restart_after_fire) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 10), ZN_OK);
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 1);
    asserteq(t->index, ZN_TIMER_NOINDEX);
    asserteq(zn_starttimer(t, 20), ZN_OK);
    ZT_WAIT(L, g_fired == 2);
    zt_end(&L);
    asserteq(g_fired, 2);
    zt_close(S);
}

TEST(timer_deltimer_armed) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 20), ZN_OK);
    zn_deltimer(t); /* cancels then returns the object to the pool */
    asserteq(S->ts.used, 0);
    zt_begin(&L, S, 1000);
    zt_wait(&L, 80);
    zt_end(&L);
    asserteq(g_fired, 0);
    zt_close(S);
}

/* ---- nexttime / timeout accounting ---- */

TEST(timer_nexttime_and_timeout) {
    zn_State *S = zt_open();

    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);

    /* empty heap: gettimeout must report ZN_FOREVER, so every backend waits
     * without a timeout instead of blocking for ~49.7 days (TODO T3). */
    asserteq(znT_gettimeout(&S->ts, 0), ZN_FOREVER);
    asserteq(znT_gettimeout(&S->ts, 12345), ZN_FOREVER);

    {
        zn_Timer *t = zn_newtimer(S, cb_record, NULL);
        assertok(t != NULL);
        asserteq(zn_starttimer(t, 100), ZN_OK);
        asserteq(S->ts.used, 1);
        asserteq(t->index, 0);
        asserteq(S->ts.nexttime, t->emit);
        asserteq(znT_gettimeout(&S->ts, t->emit - 10), 10);
        asserteq(znT_gettimeout(&S->ts, t->emit + 1), 0);
        zn_canceltimer(t);
        asserteq(S->ts.used, 0);
        zn_deltimer(t);
    }
    zt_close(S);
}

/* The poll wait is sized in int/DWORD, so znS_poll clamps it; the internal
 * gettimeout keeps returning the exact delta (see timer_64bit_delay). */
TEST(timer_timeout_clamp) {
    asserteq(znS_clampms(0), 0);
    asserteq(znS_clampms(25), 25);
    asserteq(znS_clampms((zn_Time)INT_MAX), INT_MAX);
    asserteq(znS_clampms((zn_Time)INT_MAX + 1), INT_MAX);
    asserteq(znS_clampms(ZN_FOREVER - 1), INT_MAX);
    asserteq(znS_clampms(ZN_FOREVER), ZN_FOREVER);
}

/* Cancelling the root must move nexttime to the new root: a stale nexttime
 * wakes the loop on every poll until the cancelled deadline (TODO T4). */
TEST(timer_cancel_updates_nexttime) {
    zn_State *S = zt_open();
    zn_Timer *a = zn_newtimer(S, cb_record, NULL);
    zn_Timer *b = zn_newtimer(S, cb_record, NULL);

    assertok(a != NULL && b != NULL);
    asserteq(zn_starttimer(a, 40), ZN_OK);
    asserteq(zn_starttimer(b, 80), ZN_OK);
    asserteq(S->ts.nexttime, a->emit);

    zn_canceltimer(a);
    asserteq(S->ts.used, 1);
    asserteq(S->ts.nexttime, b->emit);
    asserteq(znT_gettimeout(&S->ts, b->emit - 5), 5);

    zn_canceltimer(b);
    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);

    zn_deltimer(a);
    zn_deltimer(b);
    zt_close(S);
}

/* epoll_wait takes an int; a plain cast turned a timeout above INT_MAX into a
 * negative number, i.e. "wait forever" (TODO B6). */
#if defined(__linux__) && !defined(ZN_USE_SELECT)
TEST(epoll_timeout_clamp) {
    asserteq(znB_timeoutms(ZN_FOREVER), -1);
    asserteq(znB_timeoutms(0), 0);
    asserteq(znB_timeoutms(25), 25);
    asserteq(znB_timeoutms((zn_Time)INT_MAX), INT_MAX);
    asserteq(znB_timeoutms((zn_Time)INT_MAX + 1), INT_MAX);
    asserteq(znB_timeoutms(ZN_FOREVER - 1), INT_MAX);
}
#endif /* __linux__ && !ZN_USE_SELECT */

/* A fired one-shot timer must leave the loop with nothing to do; this is the
 * path that makes zn_run(LOOP) return normally. */
TEST(timer_loop_exits_after_fire) {
    zn_State *S = zt_open();
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 500), ZN_OK);
    asserteq(zt_run(S), 0);
    asserteq(g_fired, 1);
    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);
    zt_close(S);
}

/* ---- stress ---- */

TEST(timer_many) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t[MAXEV];
    int       i, seen[MAXEV];

    reset_events();
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < MAXEV; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        asserteq(zn_starttimer(t[i], (zn_Time)((i % 16) * 10 + 1)), ZN_OK);
    }
    zt_begin(&L, S, 3000);
    ZT_WAIT(L, g_fired == MAXEV);
    zt_end(&L);
    for (i = 0; i < MAXEV; ++i) seen[g_order[i]] = 1;
    for (i = 0; i < MAXEV; ++i) asserteq(seen[i], 1);
    asserteq(S->ts.used, 0);
    zt_close(S);
}

/* ---- handler return value width ---- */

static zn_Time cb_big_return(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    g_fired++;
    return g_fired == 1 ? (zn_Time)0x7FFFFFF0u : 0;
}

TEST(timer_handler_returns_int_max) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_big_return, NULL);

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 0), ZN_OK);
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_fired == 1);
    /* the positive return value re-arms the timer */
    assertok(t->index != ZN_TIMER_NOINDEX);
    zn_canceltimer(t);
    zn_deltimer(t);
    zt_end(&L);
    zt_close(S);
}

/* T2: a delay wider than 32 bits must be stored exactly.  Skipped where
 * zn_Time is still 32-bit (the pre-existing wrap remains there). */
TEST(timer_64bit_delay) {
    zn_State *S;
    zn_Timer *t;
    zn_Time   big = (zn_Time)0xFFFFFFFFu + 6;

    if (sizeof(zn_Time) <= sizeof(unsigned)) {
        test_skip("zn_Time is 32-bit");
        return;
    }
    S = zt_open();
    t = zn_newtimer(S, cb_record, NULL);
    assertok(t != NULL);
    asserteq(zn_starttimer(t, big), ZN_OK);
    asserteq(t->emit, t->start + big);
    asserteq(S->ts.nexttime, t->emit);
    assertok(znT_gettimeout(&S->ts, t->start) > (zn_Time)0xFFFFFFFFu / 2);
    zn_canceltimer(t);
    zn_deltimer(t);
    zt_close(S);
}

/* T1: znT_update stores the handler return in an int, truncating a
 * 64-bit re-arm delay.  0x80000005 becomes negative, so the timer is silently
 * not re-armed.  The test does not wait for the re-armed deadline; it checks
 * the timer is scheduled far in the future and then cancels it. */
static zn_Time cb_big_rearm(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    g_fired++;
    return g_fired == 1 ? (zn_Time)0x80000000u + 5 : 0;
}

TEST(timer_64bit_handler_return) {
    zn_State *S;
    zt_Loop   L;
    zn_Timer *t;

    if (sizeof(zn_Time) <= sizeof(unsigned)) {
        test_skip("zn_Time is 32-bit");
        return;
    }
    S = zt_open();
    t = zn_newtimer(S, cb_big_rearm, NULL);
    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 0), ZN_OK);
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_fired == 1);
    assertok(t->index != ZN_TIMER_NOINDEX);
    assertok(t->emit > zn_time() + (zn_Time)1000000000u);
    zn_canceltimer(t);
    zn_deltimer(t);
    zt_end(&L);
    zt_close(S);
}

/* More than ZN_MIN_TIMERHEAP (512) armed timers forces znT_resize() to
 * double the array, covering the growth path. */
TEST(timer_resize_heap) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t[600];
    int       i;

    reset_events();
    for (i = 0; i < 600; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        asserteq(zn_starttimer(t[i], (zn_Time)(5 + i % 8)), ZN_OK);
    }
    assertok(S->ts.size >= 600);
    zt_begin(&L, S, 3000);
    ZT_WAIT(L, g_fired == 600);
    zt_end(&L);
    asserteq(S->ts.used, 0);
    zt_close(S);
}

/* ---- starttimer value domain (T6) ---- */

static zn_Time cb_sentinel(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    g_fired++;
    return ZN_FOREVER;
}

TEST(timer_start_param) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);
    zn_Time   emit;

    reset_events();
    assertok(t != NULL);
    asserteq(zn_starttimer(t, ZN_FOREVER), ZN_EPARAM);
    asserteq(t->index, ZN_TIMER_NOINDEX);
    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);

    /* a rejected re-arm must leave the armed timer alone */
    asserteq(zn_starttimer(t, 100), ZN_OK);
    emit = t->emit;
    asserteq(zn_starttimer(t, ZN_FOREVER), ZN_EPARAM);
    asserteq(S->ts.used, 1);
    asserteq(t->index, 0);
    asserteq(t->emit, emit);
    asserteq(S->ts.nexttime, emit);

    /* zero is still a valid delay: fire on the next update */
    asserteq(zn_starttimer(t, 0), ZN_OK);
    asserteq(t->emit, t->start);
    zn_canceltimer(t);
    zn_deltimer(t);

    /* the handler returning the sentinel must neither re-arm nor crash */
    t = zn_newtimer(S, cb_sentinel, NULL);
    assertok(t != NULL);
    asserteq(zn_starttimer(t, 0), ZN_OK);
    zt_begin(&L, S, 1000);
    ZT_WAIT(L, g_fired == 1);
    zt_end(&L);
    asserteq(t->index, ZN_TIMER_NOINDEX);
    asserteq(S->ts.used, 0);
    zn_deltimer(t);
    zt_close(S);
}

/* ---- randomized model check (heap invariant + firing order) ---- */

/* Same fan-out as the implementation.  C5 changes this one line to
 * ZN_TIMER_ARITY; the rest of the property test is arity-agnostic. */
#define PROP_ARITY  2
#define PROP_TIMERS 256
#define PROP_OPS    2000

static zn_Timer *g_prop[PROP_TIMERS];
static zn_Time   g_prop_emit[PROP_TIMERS];
static int       g_prop_armed[PROP_TIMERS];
static int       g_prop_fired[PROP_TIMERS];
static zn_Time   g_prop_fired_emit[PROP_TIMERS];
static int       g_prop_nfired;
static unsigned  g_prop_seed = 12345u;

static unsigned prop_rand(void) {
    g_prop_seed = g_prop_seed * 1103515245u + 12345u;
    return (g_prop_seed >> 8) & 0xFFFFFFu;
}

static zn_Time prop_min(void) {
    zn_Time min = ZN_FOREVER;
    int     i;
    for (i = 0; i < PROP_TIMERS; ++i)
        if (g_prop_armed[i] && g_prop_emit[i] < min) min = g_prop_emit[i];
    return min;
}

static zn_Time cb_prop(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)elapsed;
    if (g_prop_nfired < PROP_TIMERS) {
        g_prop_fired[g_prop_nfired] = (int)(size_t)ud;
        g_prop_fired_emit[g_prop_nfired] = timer->emit;
    }
    g_prop_nfired++;
    return 0;
}

/* the model and the heap must agree on size, nexttime, every index and the
 * parent/child order; a broken sift shows up here even when the test never
 * observes a wrong firing order */
static void prop_check(zn_State *S) {
    zn_Time  min = prop_min();
    unsigned used = 0;
    int      i;

    for (i = 0; i < PROP_TIMERS; ++i) used += (unsigned)g_prop_armed[i];
    asserteq(S->ts.used, used);
    asserteq(S->ts.nexttime, min);
    for (i = 0; i < PROP_TIMERS; ++i) {
        if (!g_prop_armed[i]) {
            asserteq(g_prop[i]->index, ZN_TIMER_NOINDEX);
            continue;
        }
        assertok(g_prop[i]->index < S->ts.used);
        assertok(S->ts.nodes[g_prop[i]->index] == g_prop[i]);
        asserteq(g_prop[i]->emit, g_prop_emit[i]);
    }
    for (i = 0; i < (int)S->ts.used; ++i) {
        unsigned p;
        asserteq(S->ts.nodes[i]->index, (unsigned)i);
        asserteq(S->ts.keys[i], S->ts.nodes[i]->emit);
        if (i == 0) continue;
        p = (unsigned)((i - 1) / PROP_ARITY);
        assertok(S->ts.keys[p] <= S->ts.keys[i]);
    }
}

TEST(timer_heap_property) {
    zn_State *S = zt_open();
    int       i, op;

    g_prop_seed = 12345u;
    memset(g_prop_armed, 0, sizeof(g_prop_armed));
    for (i = 0; i < PROP_TIMERS; ++i) {
        g_prop[i] = zn_newtimer(S, cb_prop, (void *)(size_t)i);
        assertok(g_prop[i] != NULL);
        assertok(zn_starttimer(g_prop[i], (zn_Time)(prop_rand() % 5000))
                 == ZN_OK);
        g_prop_armed[i] = 1;
        g_prop_emit[i] = g_prop[i]->emit;
    }
    prop_check(S);

    for (op = 0; op < PROP_OPS; ++op) {
        unsigned r = prop_rand() % 4;
        int      id = (int)(prop_rand() % PROP_TIMERS);

        if (r < 2) { /* arm or re-arm */
            assertok(zn_starttimer(g_prop[id], (zn_Time)(prop_rand() % 5000))
                     == ZN_OK);
            g_prop_armed[id] = 1;
            g_prop_emit[id] = g_prop[id]->emit;
        } else if (r == 2) { /* cancel, armed or not */
            zn_canceltimer(g_prop[id]);
            g_prop_armed[id] = 0;
        } else if (prop_min() != ZN_FOREVER) { /* fire up to a synthetic now */
            zn_Time target = prop_min() + (zn_Time)(prop_rand() % 800);
            int     expect = 0, j;

            for (j = 0; j < PROP_TIMERS; ++j)
                if (g_prop_armed[j] && g_prop_emit[j] <= target) ++expect;
            g_prop_nfired = 0;
            znT_update(&S->ts, target);
            asserteq(g_prop_nfired, expect);
            for (j = 0; j < g_prop_nfired; ++j) {
                int fired = g_prop_fired[j];
                assertok(fired >= 0 && fired < PROP_TIMERS);
                assertok(g_prop_armed[fired]);
                assertok(g_prop_emit[fired] <= target);
                assertok(g_prop_fired_emit[j] == g_prop_emit[fired]);
                g_prop_armed[fired] = 0;
                if (j > 0)
                    assertok(g_prop_fired_emit[j - 1] <= g_prop_fired_emit[j]);
            }
        }
        prop_check(S);
    }

    for (i = 0; i < PROP_TIMERS; ++i) {
        zn_canceltimer(g_prop[i]);
        zn_deltimer(g_prop[i]);
    }
    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);
    zt_close(S);
}

#include "timer_test.gen.inc"
