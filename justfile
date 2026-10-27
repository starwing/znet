# znet — root justfile.
#
# Directories that own recipes have their own justfile and are imported as
# modules, so both spellings work:
#
#   just check            ==  just tests check
#   just bench            ==  just bench build      (module default)
#
#   just check             whole suite: native backend + select
#   just check-select      select backend only
#   just check-c89         strict C89 compile check (TODO C1/C4 still red)
#   just test tcp          one suite on the native backend
#   just test tcp tcp_conn only tests whose name starts with tcp_conn
#   just cov               lcov report for znet.h
#   just clean-cov         remove coverage data only
#   just test-lua          build the Lua binding + run tests/test.lua
#   just smoke             build + run example.c
#   just bench             build the benchmark programs
#   just bench echo 5      echo benchmark, 5 seconds
#   just clean             remove every build artifact

mod tests
mod bench

CC := env("CC", "cc")
CFLAGS := "-std=c99 -D_XOPEN_SOURCE=600 -O2 -g -pedantic -Wall -Wextra -Wno-unused-parameter"

# list the recipes
default:
    @just --list

# ---------------------------------------------------------------------------
# checks (implemented in tests/justfile)

# whole suite on the native backend and on select
check: (tests::check)

# native backend only
check-native: (tests::check-native)

# select backend only
check-select: (tests::check-select)

# alias of check (ASan/UBSan is the default)
check-asan: (tests::check-asan)

# strict C89 compile check (no run): suites + example.c + zn_task.h
check-c89: (tests::check-c89)

# strict C++03 compile check (no run)
check-cpp03: (tests::check-cpp03)

# Windows/MSVC only; fails clearly elsewhere
check-msvc: (tests::check-msvc)

# (`just test uncovered` is the coverage report, not a suite)
# one suite, native backend: just test tcp [name-prefix]
test suite="tcp" *args="":
    #!/usr/bin/env bash
    set -euo pipefail
    if [ '{{suite}}' = uncovered ]; then
        exec just --justfile tests/justfile uncovered
    fi
    exec just --justfile tests/justfile test '{{suite}}' {{args}}

# one suite, select backend
test-select suite="tcp" *args="":
    @just --justfile tests/justfile test-select {{suite}} {{args}}

# Lua binding smoke test (needs Lua >= 5.3 and its headers)
test-lua: (tests::test-lua)

# lcov coverage report for znet.h (native + select, merged)
cov: (tests::cov)

# lines of znet.h that never executed
cov-lines: (tests::cov-lines)

# branches of znet.h never taken
cov-unbranched: (tests::cov-unbranched)

# uncovered lines from the last `just cov`, grouped by function
uncovered: (tests::uncovered)

# ---------------------------------------------------------------------------
# smoke program

# build + run example.c
smoke: (smoke-run "native" "")
smoke-select: (smoke-run "select" "-DZN_USE_SELECT")

[private]
smoke-run tag flags:
    #!/usr/bin/env bash
    set -euo pipefail
    out="/tmp/znet-smoke-{{tag}}"
    mkdir -p "$out"
    {{CC}} {{CFLAGS}} {{flags}} -o "$out/example" example.c -pthread
    "$out/example"

# ---------------------------------------------------------------------------
# cleanup

# remove every build artifact (tests, benchmarks, smoke binaries)
clean: (tests::clean) (bench::clean)
    rm -rf /tmp/znet-smoke-native /tmp/znet-smoke-select

# remove coverage data only (keeps the compiled test binaries)
clean-cov: (tests::clean-cov)
