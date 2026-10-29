/* timer-heap microbenchmark: arm / cancel / fire / clock, no sockets.
 *
 * usage: bench_timer [n]   (no argument runs 1000, 10000, 100000)
 *
 * `fire` drives the internal znT_update directly, so the pop-min path is
 * measured without a poll loop.  `clock` is the control line: zn_starttimer
 * calls zn_time once per arm, and without that line its cost would be hidden
 * inside the arm numbers.
 */

#define ZN_IMPLEMENTATION
#include "../znet.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BENCH_SEED        12345u /* fixed so permutations compare across runs */
#define BENCH_TARGET_MS   50.0
#define BENCH_MAX_BATCHES 262144u
#define BENCH_ZERO_BATCHES 4096u /* coarse clock: stop instead of spinning */

typedef enum BenchOp {
    BENCH_ARM,
    BENCH_CANCEL,
    BENCH_FIRE,
    BENCH_CLOCK
} BenchOp;

static zn_Time bench_ontimer(void *ud, zn_Timer *timer, zn_Time delayed) {
    (void)ud;
    (void)timer;
    (void)delayed;
    return 0;
}

static const char *bench_name(BenchOp op) {
    switch (op) {
    case BENCH_ARM: return "arm";
    case BENCH_CANCEL: return "cancel";
    case BENCH_FIRE: return "fire";
    case BENCH_CLOCK: return "clock";
    }
    return "?";
}

static unsigned bench_rand(unsigned *state) {
    *state = (*state * 1103515245u + 12345u) & 0x7fffffffu;
    return *state;
}

static void bench_order(unsigned *order, unsigned n) {
    unsigned i, j, tmp, state = BENCH_SEED;
    for (i = 0; i < n; ++i) order[i] = i;
    for (i = n; i > 1; --i) {
        j = bench_rand(&state) % i;
        tmp = order[i - 1];
        order[i - 1] = order[j];
        order[j] = tmp;
    }
}

static int bench_arm_all(zn_Timer **t, unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i)
        if (zn_starttimer(t[i], 3600000u + i) != ZN_OK) return 0;
    return 1;
}

static void bench_cancel_all(zn_Timer **t, unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i) zn_canceltimer(t[i]);
}

static void bench_delete_all(zn_Timer **t, unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i) zn_deltimer(t[i]);
}

static zn_Time bench_maxemit(zn_Timer **t, unsigned n) {
    zn_Time  max = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        if (t[i]->emit > max) max = t[i]->emit;
    return max;
}

/* run op until the accumulated measured time reaches BENCH_TARGET_MS;
 * ns/op = ticks * 1e9 / (CLOCKS_PER_SEC * n * batches), as in the plan.
 * returns -1.0 if arming for a cancel/fire batch failed. */
static double bench_measure(zn_State *S, BenchOp op, zn_Timer **t, unsigned n,
                            unsigned *order, unsigned *batches_out) {
    clock_t  t0, t1;
    double   ticks = 0.0, target, ns;
    unsigned batches = 0, i;
    zn_Time  maxemit = 0;

    target = (double)CLOCKS_PER_SEC * BENCH_TARGET_MS / 1000.0;
    for (;;) {
        if (op == BENCH_CANCEL || op == BENCH_FIRE) {
            if (!bench_arm_all(t, n)) return *batches_out = batches, -1.0;
        }
        if (op == BENCH_FIRE) maxemit = bench_maxemit(t, n);
        t0 = clock();
        switch (op) {
        case BENCH_ARM:
            for (i = 0; i < n; ++i) (void)zn_starttimer(t[i], 3600000u + i);
            break;
        case BENCH_CANCEL:
            for (i = 0; i < n; ++i) zn_canceltimer(t[order[i]]);
            break;
        case BENCH_FIRE: znT_update(&S->ts, maxemit + 1); break;
        case BENCH_CLOCK:
            for (i = 0; i < n; ++i) (void)zn_time();
            break;
        }
        t1 = clock();
        if (t1 > t0) ticks += (double)(t1 - t0);
        ++batches;
        if (op == BENCH_ARM) bench_cancel_all(t, n); /* setup, untimed */
        if (ticks >= target) break;
        if (ticks == 0.0 && batches >= BENCH_ZERO_BATCHES) break;
        if (batches >= BENCH_MAX_BATCHES) break;
    }
    ns = ticks * 1e9 / ((double)CLOCKS_PER_SEC * (double)n * (double)batches);
    *batches_out = batches;
    return ns;
}

static int bench_run_size(zn_State *S, unsigned n) {
    zn_Timer **t;
    unsigned  *order;
    unsigned   i, created, batches;
    double     ns;
    int        oi;
    BenchOp    op;
    int        ok = 1;

    t = (zn_Timer **)malloc((size_t)n * sizeof(zn_Timer *));
    order = (unsigned *)malloc((size_t)n * sizeof(unsigned));
    if (t == NULL || order == NULL) {
        free(t);
        free(order);
        return 0;
    }
    created = 0;
    for (i = 0; i < n && ok; ++i) {
        t[i] = zn_newtimer(S, bench_ontimer, NULL);
        if (t[i] == NULL)
            ok = 0;
        else
            ++created;
    }
    if (ok) {
        bench_order(order, n);
        /* grow the heap up front: arm measures inserts, not realloc */
        if (!bench_arm_all(t, n))
            ok = 0;
        else
            bench_cancel_all(t, n);
    }
    for (oi = (int)BENCH_ARM; ok && oi <= (int)BENCH_CLOCK; ++oi) {
        op = (BenchOp)oi;
        ns = bench_measure(S, op, t, n, order, &batches);
        if (ns < 0.0)
            ok = 0;
        else
            printf("%-7s n=%7u %10.2f ns/op  batches=%u\n", bench_name(op), n,
                   ns, batches);
    }
    bench_delete_all(t, created);
    free(t);
    free(order);
    return ok;
}

int main(int argc, char **argv) {
    zn_State *S;
    unsigned  n = 0;
    int       ok;

    if (argc > 1) {
        long v = atol(argv[1]);
        if (v <= 0 || v > 10000000L) {
            fprintf(stderr, "usage: bench_timer [n]  (1 <= n <= 10000000)\n");
            return 2;
        }
        n = (unsigned)v;
    }
    zn_initialize();
    S = zn_newstate();
    if (S == NULL) {
        zn_deinitialize();
        return 1;
    }
    if (n != 0)
        ok = bench_run_size(S, n);
    else
        ok = bench_run_size(S, 1000u) && bench_run_size(S, 10000u)
          && bench_run_size(S, 100000u);
    zn_close(S);
    zn_deinitialize();
    return ok ? 0 : 1;
}
