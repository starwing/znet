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
tests/check.bat             MSVC entry: build+run every suite (IOCP + select)
tests/run.bat               MSVC entry: build+run one suite / one test
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
* `zt_run(S)` on an *idle* state blocks: with an empty timer heap the poll
  timeout is `ZN_FOREVER`, so nothing wakes the loop. `ZT_WAIT`/pump avoids it.

Rules for robust tests:

* No fixed ports. Listen/bind with port `0`, then read the bound port with
  `zt_accept_port(a)` / `zt_udp_port(u)` / `zt_tcp_port(tcp)`.
* Some readiness reports cannot be produced by the kernel on demand
  (`EAGAIN` on a listener, an error event on a listener). `zt_dispatch_accept()`
  / `zt_dispatch_udp()` call the backend entry point directly (`error_event` 0 =
  readable, 1 = error/except) so those branches stay testable; they are POSIX
  only. Wrap the call in `zt_guard_start(ms)` when the old behaviour blocked, so
  a regression is a failure instead of a stalled suite.
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

znet.h defines its internal `ZN_USE_*` selectors once in the implementation
preamble and leaves them defined, so they are visible in a test file. The
suites still key off the platform switches that misc/gen_entries.lua can also
mirror with `-D`:

| want | guard |
| --- | --- |
| POSIX | `#if !defined(_WIN32)` |
| select backend | `#if defined(ZN_USE_SELECT)` |
| reactive backend (kqueue/epoll) | `#if !defined(ZN_USE_SELECT) && !defined(_WIN32)` |
| kqueue backend | `#if defined(ZN_HAVE_KQUEUE) && !defined(ZN_USE_SELECT)` |
| epoll backend | `#if defined(__linux__) && !defined(ZN_USE_SELECT)` |

`ZN_HAVE_KQUEUE` and `ZN_HAVE_INFO` are defined by znet.h on the matching
platforms and are safe to test after the include. The justfile passes the
platform ones to the generator (`GEN_DEFS`); keep that list in sync with this
table.

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
just test-lua              # build znet.so + run tests/test.lua
just cov                   # lcov report for znet.h
just uncovered             # uncovered lines grouped by function (last cov)
just clean-cov             # coverage data only
```

On Windows the same suites build and run through the batch entry points, with
or without just.  A plain ssh or cmd session is enough: `misc\msvc-env.bat`
locates Visual Studio with vswhere and loads its environment when `cl.exe` is
not on PATH (set `ZNET_VCVARS` to an explicit `vcvars64.bat` if that fails);
`lua` must be on PATH.

```bat
tests\check.bat              # native (IOCP only), cl /nologo /W4 /WX
tests\check.bat native       # one backend
tests\run.bat tcp            # one suite; all tests of it in one process
tests\run.bat tcp tcp_conn   # only matching tests
tests\run.bat tcp -select    # select backend
```

Both scripts derive the suite list from `tests\*_test.c`, and both run one
process per test, like the POSIX runner: an MSVC `abort()` exits with
`0xC0000409` (negative), which `if errorlevel 1` would miss, so failures are
detected by comparing the exit code as a string.  `check.bat` also requires
the harness's ` OK`/` SKIP` line in the captured output: a test that kills
its own thread exits 0 without running to the end (see `notes/changes.md` §1.14).  It compiles
`zn_task.h` too, because no suite includes that header and its Win32 branch
would otherwise never reach the compiler.  The IOCP suite passes on Windows
(VS2022 x64, 2026-10-28); `check.bat select` exits 2 because `znet.h` always
uses IOCP on `_WIN32`.

`just uncovered` (also spelled `just test uncovered`) reads the tracefile left
by the last `just cov` and lists every line with a zero hit count under the
function that owns it, together with that function's call count.  The function
boundaries come from scanning the source, not from lcov's FN ranges: `znet.h`
is compiled into both backends, so its merged FN ranges span unrelated code.

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
3. whether it matches a known TODO ID (L1, L2, L3, L5, B2, T1, T3, …) or
   is new;
4. the smallest deterministic reproduction.

The owner decides the fix; the test is the regression proof.
