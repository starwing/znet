# znet test suite — authoring guide

The suite is deliberately close to the one in `~/Work/Code/piecetab/tests`:
plain C, no framework, one binary per topic, `TEST(name)` functions collected
by a Lua-generated `.gen.inc`.

## Layout

```
justfile                    repo-root entry point; imports tests/ and bench/
                            as modules (`just tests check`, `just bench echo 5`)
misc/gen_entries.lua        scans TEST(name), emits entries table + main()
tests/justfile              build/run/coverage recipes for this directory
tests/tests.h               runner + assert macros (C89)
tests/ztest.h               znet-specific helpers; includes znet.h with
                            ZN_IMPLEMENTATION so unit tests see internals
tests/<topic>_test.c        the suites
tests/<topic>_test.gen.inc  generated, gitignored, never edited
tests/test.lua              Lua binding smoke test (needs Lua >= 5.3)
tests/COVERAGE.md           current line-coverage report and uncovered-line analysis
tests/build/                binaries, Lua module and coverage output (gitignored)
bench/                      benchmarks (own justfile); not part of the suite
```

A suite file looks like:

```c
#include "ztest.h"

TEST(timer_order) {
    zn_State *S = zt_open();
    ...
    zt_close(S);
}

#include "timer_test.gen.inc"
```

The `.gen.inc` include must be the last line. `TEST(name)` must start at
column 0 (the scanner matches `^TEST(`).

## Driving the event loop

* `zt_run(S)` — `zn_run(S, ZN_RUN_LOOP)`, guarded by a watchdog that fails the
  test after `ZT_TIMEOUT_MS` (5 s). Use it when the test's callbacks delete
  every object, so the loop has nothing left to poll. This is the preferred
  shape: it also proves the run loop terminates.
* `zt_begin(&L, S, ms)` / `ZT_WAIT(L, cond)` / `zt_wait(&L, ms)` /
  `zt_end(&L)` — pump `ZN_RUN_ONCE` in a loop until a condition holds. A
  hidden 20 ms tick timer keeps every poll bounded; a deadline turns a stuck
  condition into a normal test failure. Use this for timers and for anything
  where the loop must keep running after the condition is met.
* Never mix the two in one phase: the tick timer keeps `ZN_RUN_LOOP` alive.
* `zt_run(S)` on an *idle* state blocks: an empty timer heap gives
  `ZN_FOREVER - now` as the poll timeout (TODO T3). `ZT_WAIT`/pump avoids it.

Rules for robust tests:

* No fixed ports. Listen/bind with port `0`, then read the bound port with
  `zt_accept_port(a)` / `zt_udp_port(u)` / `zt_tcp_port(tcp)`.
* No `sleep()` when the loop can wait for a callback. `zt_wait(&L, ms)` is the
  escape hatch for "nothing else should fire".
* Clean up: every `zn_new*` needs a `zn_del*`; close listeners before `zt_run`
  expects to return. `zt_close(S)` at the end is the safety net.
* Assert the desired behaviour, not the current buggy behaviour. If a test
  fails because of a library bug, keep it, put it at the END of the file (an
  abort kills the rest of that process) and add a comment with the TODO ID.
* Assertions abort with `file:line`; the runner reports the test name.
* `test_skip("reason")` for tests that need a facility the host lacks (e.g.
  IPv6 loopback). The run prints SKIP instead of OK; use it sparingly and
  never to hide a bug.

## Conditional compilation

`misc/gen_entries.lua` must agree with the C preprocessor about which tests
exist, otherwise a test is either missing from the runner or compiled but
unused (-Werror). It supports `#if/#ifdef/#ifndef/#elif/#else/#endif` with
`defined(X)`, `!`, `&&`, `||`, parentheses, integer literals and identifiers,
and it is given the same `-D` flags as the compiler.

znet.h `#undef`s its internal `zn_use_*` macros at the end of each
implementation section, so **they are not visible in a test file**. Use the
macros that survive instead:

| want | guard |
| --- | --- |
| POSIX | `#if !defined(_WIN32)` |
| select backend | `#if defined(ZN_USE_SELECT)` |
| reactive backend (kqueue/epoll) | `#if !defined(ZN_USE_SELECT) && !defined(_WIN32)` |
| kqueue backend | `#if defined(zn_have_kqueue) && !defined(ZN_USE_SELECT)` |
| epoll backend | `#if defined(__linux__) && !defined(ZN_USE_SELECT)` |
| 64-bit timer | `#if defined(ZN_USE_64BIT_TIMER)` |

`zn_have_kqueue` and `zn_have_info` are defined by znet.h on the matching
platforms and are not undefined, so they are safe to test after the include.
The justfile passes the platform ones to the generator (`GEN_DEFS`); keep
that list in sync with this table.

## Style

C89: declarations at block start, `/* */` comments, no `//`, no mixed
declarations, no VLAs, no designated initialisers. 4 spaces, K&R braces.
`-Wall -Wextra -Werror -pedantic` must stay clean.

Two deliberate deviations from strict C89, both matching the piecetab suite:

* `tests.h`/`ztest.h` use variadic macros for `test_log`, `test_skip` and
  `zt_fail`. They are a C99 feature; `check-c89`/`check-cpp03` pass
  `-Wno-variadic-macros` for that reason. Everything else in a suite must be
  C89.
* The "function signature on one line, <80 columns" rule in AGENTS.md applies
  to library files. Test callbacks must match the public typedefs
  (`zn_RecvFromHandler` alone is 119 columns), so a callback definition may
  continue on a second aligned line; that is not a rule violation.

## Commands

From the repo root (the module spelling `just tests <recipe>` is equivalent):

```sh
just test tcp              # build+run tcp_test (native)
just test tcp tcp_conn     # only matching tests
just test-select tcp       # select backend
just check                 # everything, native+select
just check-64bit-timer     # timer suite, 64-bit zn_Time
just test-lua              # build znet.so + run tests/test.lua
just cov                   # lcov report for znet.h
just clean-cov             # coverage data only
```

`just test-lua` is outside `check`: it links the optional Lua binding against
the host Lua (>= 5.3, headers found via pkg-config or `$LUA_INC`) and runs a
self-contained loopback echo test, so it needs no network.

The current line-coverage numbers and the per-line analysis of what cannot be
covered (IOCP-only code, fault-injection paths, dead defensive branches) are in
[`COVERAGE.md`](COVERAGE.md).

`just test` runs the binary in one process (fast, stops at the first failure).
`just check` runs one process per test, so one failing test does not hide the
others. Use `test <suite> <prefix>` while developing.

Without a root justfile (e.g. inside the Linux VM) the same recipes are
reachable directly: `just --justfile tests/justfile check`. Coverage is
collected from `tests/build/cov-native` and `cov-select`; the `.gcda` files are
compiler-version specific, so `cov` wipes those directories first (macOS and
the Linux VM share this workspace).

## Reporting library bugs

Do not touch `znet.h` (or any other library/header/framework file). If a test
exposes a library bug, leave the failing test in place and report:

1. test name and exact assertion output;
2. the code path involved (`file:line` in `znet.h`);
3. whether it matches a known TODO ID (L1, L2, L3, L5, B2, B3, T1, T3, …) or
   is new;
4. the smallest deterministic reproduction.

The owner decides the fix; the test is the regression proof.
