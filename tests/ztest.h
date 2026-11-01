/* tests/ztest.h -- znet test helpers (C89)
 *
 * Every test file starts with:
 *
 *   #include "ztest.h"
 *
 * which pulls in znet.h (with ZN_IMPLEMENTATION, so the internal static
 * helpers are visible to unit tests) and tests.h.
 *
 * Two ways to drive the event loop:
 *
 *   zt_run(S)            run to completion; the loop must terminate because
 *                        the test's callbacks delete every object.  A
 *                        watchdog aborts the test after ZT_TIMEOUT_MS so a
 *                        library hang is a failure instead of a stall.
 *
 *   zt_Loop L;
 *   zt_begin(&L, S, 2000);
 *   ZT_WAIT(L, done);    pump the loop until `done`; a hidden 20 ms tick
 *   zt_end(&L);          timer keeps each poll bounded so `done` is polled
 *                        even when the state has no other timers.
 *
 * Do not mix them in one phase: the hidden tick timer keeps zn_run(LOOP)
 * alive forever.
 *
 * Helpers have external linkage on purpose: each test binary is a single
 * translation unit, and static helpers would trigger -Wunused-function for
 * every helper a given suite does not use.
 */

#ifndef ZTEST_H
#define ZTEST_H

#define ZN_IMPLEMENTATION
#include "znet.h"

#include "tests.h"

#include <errno.h>
#include <string.h>

#if defined(_WIN32)
# include <windows.h>
#else
# include <signal.h>
# include <unistd.h>
#endif

#ifndef ZT_TIMEOUT_MS
#define ZT_TIMEOUT_MS 5000
#endif

/* ---- failure helper ---- */

#define zt_fail(...)                                  \
    do {                                              \
        test_log("FAIL %s:%d: ", __FILE__, __LINE__); \
        test_log(__VA_ARGS__);                        \
        test_log("\n");                               \
        abort();                                      \
    } while (0)

/* ---- watchdog ---------------------------------------------------------
 * zt_run() must not rely on the library for its timeout: the whole point of
 * several regressions is that the loop never returns.  A watchdog outside
 * the event loop turns that into a test failure. */

#if defined(_WIN32)

HANDLE zt_guard_event = NULL;
HANDLE zt_guard_thread = NULL;

DWORD WINAPI zt_guard_main(LPVOID param) {
    DWORD ms = (DWORD)(size_t)param;
    if (WaitForSingleObject(zt_guard_event, ms) == WAIT_TIMEOUT) {
        static const char
                msg[] = "FAIL: test timeout (event loop did not return)\n";
        fwrite(msg, 1, sizeof(msg) - 1, stderr);
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 97);
    }
    return 0;
}

void zt_guard_start(unsigned ms) {
    zt_guard_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    zt_guard_thread = CreateThread(NULL, 0, zt_guard_main, (LPVOID)(size_t)ms,
                                   0, NULL);
    if (zt_guard_event == NULL || zt_guard_thread == NULL)
        zt_fail("cannot start watchdog");
}

void zt_guard_stop(void) {
    if (zt_guard_thread) {
        SetEvent(zt_guard_event);
        WaitForSingleObject(zt_guard_thread, 1000);
        CloseHandle(zt_guard_thread);
        zt_guard_thread = NULL;
    }
    if (zt_guard_event) {
        CloseHandle(zt_guard_event);
        zt_guard_event = NULL;
    }
}

#else /* POSIX */

int zt_guard_installed = 0;

void zt_guard_alarm(int sig) {
    static const char
            msg[] = "FAIL: test timeout (event loop did not return)\n";
    (void)sig;
    (void)write(2, msg, sizeof(msg) - 1);
    _exit(97);
}

void zt_guard_start(unsigned ms) {
    if (!zt_guard_installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = zt_guard_alarm;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, NULL);
        zt_guard_installed = 1;
    }
    alarm((ms + 999) / 1000);
}

void zt_guard_stop(void) { alarm(0); }

#endif /* _WIN32 */

/* ---- state ---- */

zn_State *zt_open(void) {
    zn_State *S;
    S = zn_newstate();
    if (S == NULL) zt_fail("zn_newstate() returned NULL");
    return S;
}

void zt_close(zn_State *S) {
    if (S != NULL) zn_close(S);
}

/* ---- loop driving ---- */

/* run the loop until it has nothing left to do; the test is responsible for
 * deleting its objects from callbacks (that is the normal contract). */
int zt_run(zn_State *S) {
    int ret;
    zt_guard_start(ZT_TIMEOUT_MS);
    ret = zn_run(S, ZN_RUN_LOOP);
    zt_guard_stop();
    return ret;
}

typedef struct zt_Loop {
    zn_State *S;
    zn_Timer *tick;
    zn_Time   deadline;
} zt_Loop;

zn_Time zt_tick_cb(void *ud, zn_Timer *timer, zn_Time elapsed) {
    (void)ud;
    (void)timer;
    (void)elapsed;
    return 20;
}

void zt_begin(zt_Loop *L, zn_State *S, unsigned ms) {
    L->S = S;
    L->deadline = zn_time() + ms;
    L->tick = zn_newtimer(S, zt_tick_cb, NULL);
    if (L->tick == NULL) zt_fail("cannot create tick timer");
    asserteq(zn_starttimer(L->tick, 20), ZN_OK);
}

/* one bounded poll cycle; returns 0 once the deadline has passed */
int zt_pump(zt_Loop *L) {
    if (zn_time() > L->deadline) return 0;
    (void)zn_run(L->S, ZN_RUN_ONCE);
    return 1;
}

void zt_end(zt_Loop *L) {
    if (L->tick != NULL) {
        zn_canceltimer(L->tick);
        zn_deltimer(L->tick);
        L->tick = NULL;
    }
}

/* pump for `ms` milliseconds: lets a test assert that nothing else fires */
void zt_wait(zt_Loop *L, unsigned ms) {
    zn_Time end = zn_time() + ms;
    while (zn_time() < end) (void)zn_run(L->S, ZN_RUN_ONCE);
}

#define ZT_WAIT(L, cond)                                                   \
    do {                                                                   \
        while (!(cond)) {                                                  \
            if (!zt_pump(&(L))) {                                          \
                test_log("FAIL %s:%d: timeout waiting for %s\n", __FILE__, \
                         __LINE__, #cond);                                 \
                abort();                                                   \
            }                                                              \
        }                                                                  \
    } while (0)

/* ---- socket inspection (white box) ----
 * The public API never exposes the local port, and tests must not use fixed
 * ports (parallel runs would collide).  Listening on port 0 and reading the
 * bound port back through getsockname() keeps every test self-contained. */

#if defined(_WIN32)
typedef SOCKET zt_sock_t;
typedef int    zt_socklen_t;
#else
typedef int       zt_sock_t;
typedef socklen_t zt_socklen_t;
#endif

unsigned zt_sockport(zt_sock_t fd) {
    zn_SockAddr  sa;
    zt_socklen_t len = (zt_socklen_t)sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    if (getsockname(fd, &sa.addr, &len) != 0) return 0;
    return znP_isv6(&sa) ? (unsigned)ntohs(sa.ipv6.sin6_port)
                         : (unsigned)ntohs(sa.ipv4.sin_port);
}

void zt_sockaddr(zt_sock_t fd, char *out, size_t cap) {
    zn_SockAddr  sa;
    zt_socklen_t len = (zt_socklen_t)sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    out[0] = 0;
    if (getsockname(fd, &sa.addr, &len) != 0) return;
    if (znP_isv6(&sa))
        (void)znP_ntop(AF_INET6, &sa.ipv6.sin6_addr, out, (zt_socklen_t)cap);
    else
        (void)znP_ntop(AF_INET, &sa.ipv4.sin_addr, out, (zt_socklen_t)cap);
}

unsigned zt_tcp_port(zn_Tcp *tcp) { return zt_sockport(tcp->socket); }
unsigned zt_udp_port(zn_Udp *udp) { return zt_sockport(udp->socket); }
unsigned zt_accept_port(zn_Accept *a) { return zt_sockport(a->socket); }

/* shrink the kernel send buffer so a few sends hit EAGAIN and the reactive
 * backends leave a send pending (regression L2). */
void zt_tcp_set_sndbuf(zn_Tcp *tcp, int bytes) {
    (void)setsockopt(tcp->socket, SOL_SOCKET, SO_SNDBUF, (const char *)&bytes,
                     (zt_socklen_t)sizeof(bytes));
}

int zt_peer_is(const zn_PeerInfo *info, const char *addr, unsigned port) {
    return strcmp(info->addr, addr) == 0 && info->port == port;
}

/* ---- manual dispatch (white box) ----
 * The kernel will not report "ready" for a listener or a UDP socket that has
 * nothing to accept, so a spurious readiness report can only be injected by
 * calling the backend entry point.  The reactive entry points queue the
 * completion, so the helper also pumps the result queue; select reports
 * inline and takes ZN_ERROR/ZN_OK the same way.  Driving backend internals from
 * tests is tracked for removal in TODO.md Q2. */

#if !defined(_WIN32)

void zt_dispatch_accept(zn_Accept *accept, int error_event) {
    znB_onaccept(accept, error_event ? ZN_ERROR : ZN_OK);
    /* the reactive backends only queue the completion; deliver it now */
#if !defined(ZN_USE_SELECT)
    znR_process(accept->S, ZN_MAX_RESULT_LOOPS);
#endif
}

void zt_dispatch_udp(zn_Udp *udp, int error_event) {
    znB_onrecvfrom(udp, error_event ? ZN_ERROR : ZN_OK);
#if !defined(ZN_USE_SELECT)
    znR_process(udp->S, ZN_MAX_RESULT_LOOPS);
#endif
}

/* the injectable connect completion exists only where it is queued: select
 * reports inline and no test can hand it an unconnected object */
#if !defined(ZN_USE_SELECT)

void zt_dispatch_connect(zn_Tcp *tcp, int error_event) {
    znB_onconnect(tcp, error_event ? ZN_ERROR : ZN_OK);
}

#endif /* !ZN_USE_SELECT */

#endif /* !_WIN32 */

#endif /* ZTEST_H */
