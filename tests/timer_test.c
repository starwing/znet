/* timer_test.c -- timer heap semantics.
 *
 * Covers: ordering, 0 delay, same deadline, re-arm from the handler, delete
 * from the handler, cancel root/middle/last, cancel twice, cancel unarmed,
 * restart armed/after fire, nexttime/timeout accounting, and the 64-bit timer
 * option (zn_Time truncation regressions T1/T2).
 *
 * Timer callbacks do not retain the state, so the tests drive the loop with
 * zt_begin/ZT_WAIT: the hidden 20 ms tick keeps every poll bounded.  Using
 * zn_run(LOOP) with a timer that is already due would block instead of
 * returning (empty heap timeout, TODO T3), so only timer_loop_exits_after_fire
 * uses zt_run() and it arms a far deadline first.
 */

#include "ztest.h"

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

TEST(timer_fire_same_deadline) {
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t[4];
    int       i, seen[4];

    reset_events();
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 4; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        asserteq(zn_starttimer(t[i], 30), ZN_OK);
    }
    zt_begin(&L, S, 2000);
    ZT_WAIT(L, g_fired == 4);
    zt_end(&L);
    for (i = 0; i < 4; ++i) seen[g_order[i]] = 1;
    for (i = 0; i < 4; ++i) asserteq(seen[i], 1);
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

    emit[0] = 1;  emit[1] = 10; emit[2] = 2;
    emit[3] = 12; emit[4] = 11; emit[5] = 5;
    assertok(znT_resizeheap(&S->ts, 6));
    for (i = 0; i < 6; ++i) {
        t[i] = zn_newtimer(S, cb_record, (void *)(size_t)i);
        assertok(t[i] != NULL);
        t[i]->start = 0;
        t[i]->emit = emit[i];
        t[i]->index = (unsigned)i;
        S->ts.heap[i] = t[i];
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
    asserteq(S->ts.heap[1], t[5]);
    asserteq(S->ts.heap[3], t[3]);
    asserteq(S->ts.heap[4], t[1]);
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
    zn_Time   empty_timeout;

    asserteq(S->ts.used, 0);
    asserteq(S->ts.nexttime, ZN_FOREVER);

    /* empty heap: znT_gettimeout returns ZN_FOREVER - now, not ZN_FOREVER
     * (TODO T3).  Assert the useful property: it is effectively forever. */
    empty_timeout = znT_gettimeout(&S->ts, 0);
    assertok(empty_timeout > 1000000000u);

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

#ifdef ZN_USE_64BIT_TIMER

/* T2: a delay wider than 32 bits must be stored exactly. */
TEST(timer_64bit_delay) {
    zn_State *S = zt_open();
    zn_Timer *t = zn_newtimer(S, cb_record, NULL);
    zn_Time   big = (zn_Time)0xFFFFFFFFu + 6;

    assertok(t != NULL);
    asserteq(zn_starttimer(t, big), ZN_OK);
    asserteq(t->emit, t->start + big);
    asserteq(S->ts.nexttime, t->emit);
    assertok(znT_gettimeout(&S->ts, t->start) > (zn_Time)0xFFFFFFFFu / 2);
    zn_canceltimer(t);
    zn_deltimer(t);
    zt_close(S);
}

/* T1: znT_updatetimers stores the handler return in an int, truncating a
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
    zn_State *S = zt_open();
    zt_Loop   L;
    zn_Timer *t = zn_newtimer(S, cb_big_rearm, NULL);

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

#endif /* ZN_USE_64BIT_TIMER */

/* More than ZN_MAX_TIMERHEAP (512) armed timers forces znT_resizeheap() to
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

#include "timer_test.gen.inc"
