/* taskpool_test.c -- zn_task.h: a pool starts, runs tasks, pauses, and shuts
 * down.
 *
 * zn_task.h carries its own implementation and had no runtime coverage at all
 * (check-c89 and tests\check.bat only compile it), so this is a smoke test,
 * not a harness: one worker, a handful of tasks, one pause round trip.  Tasks
 * must return on their own -- zn_deltaskpool joins the workers and has no way
 * to cancel one (TODO.md U11).
 *
 * The worker writes the counter it was handed and the test thread polls it.
 * There is no portable wait for "a worker ran something", so the poll has a
 * deadline: a lost run must fail instead of hanging.
 *
 * Pause is the interesting part: a resumed pool has to wake a parked worker,
 * otherwise the tasks queued while paused sit there until the next addtask.
 */

#include "ztest.h"
#include "zn_task.h"

typedef struct tp_ctx {
    volatile int runs;
} tp_ctx;

static void tp_count(void *ud, zn_TaskPool *ws) {
    tp_ctx *c = (tp_ctx *)ud;
    (void)ws;
    ++c->runs;
}

static int tp_wait(tp_ctx *c, int want) {
    zn_Time end = zn_time() + 5000;
    while (c->runs != want && zn_time() < end) {
    }
    return c->runs == want;
}

TEST(taskpool_runs_a_task) {
    tp_ctx       c;
    zn_TaskPool *ws;

    c.runs = 0;
    ws = zn_newtaskpool(1);
    assertok(ws != NULL);
    asserteq(zn_taskcount(ws), 0);
    asserteq(zn_addtask(ws, tp_count, &c), 1);
    assertok(tp_wait(&c, 1));
    zn_deltaskpool(ws);
}

TEST(taskpool_pause_defers_tasks) {
    tp_ctx       c;
    zn_TaskPool *ws;
    zn_Time      end;
    int          i;

    c.runs = 0;
    ws = zn_newtaskpool(1);
    assertok(ws != NULL);
    zn_pausetasks(ws, 1);
    for (i = 0; i < 4; ++i) {
        asserteq(zn_addtask(ws, tp_count, &c), 1);
    }
    asserteq(c.runs, 0); /* a paused pool must not run anything */
    /* park the worker first: resuming has to wake it, not just flip status */
    end = zn_time() + 50;
    while (zn_time() < end) {
    }
    zn_pausetasks(ws, 0);
    assertok(tp_wait(&c, 4));
    zn_deltaskpool(ws);
}

#include "taskpool_test.gen.inc"
