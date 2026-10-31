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
#   just check-c89         strict C89 compile check
#   just test tcp          one suite on the native backend
#   just test tcp tcp_conn only tests whose name starts with tcp_conn
#   just cov               lcov report for znet.h
#   just clean-cov         remove coverage data only
#   just test-lua          build the Lua binding + run tests/test.lua
#   just smoke             build + run example.c
#   just bench             build the benchmark programs
#   just bench echo 5      echo benchmark, 5 seconds
#   just clean             remove every build artifact

set windows-shell := ["cmd.exe", "/C"]

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
[unix]
test suite="tcp" *args="":
    #!/usr/bin/env bash
    set -euo pipefail
    if [ '{{suite}}' = uncovered ]; then
        exec just --justfile tests/justfile uncovered
    fi
    exec just --justfile tests/justfile test '{{suite}}' {{args}}

# one suite, select backend
[unix]
test-select suite="tcp" *args="":
    @just --justfile tests/justfile test-select {{suite}} {{args}}

# Lua binding smoke test (needs Lua >= 5.3 and its headers)
[unix]
test-lua: (tests::test-lua)

# lcov coverage report for znet.h (native + select, merged)
[unix]
cov: (tests::cov)

# lines of znet.h that never executed
[unix]
cov-lines: (tests::cov-lines)

# branches of znet.h never taken
[unix]
cov-unbranched: (tests::cov-unbranched)

# uncovered lines from the last `just cov`, grouped by function
[unix]
uncovered: (tests::uncovered)

# ---------------------------------------------------------------------------
# smoke program

# build + run example.c
[unix]
smoke: (smoke-run "native" "")
[unix]
smoke-select: (smoke-run "select" "-DZN_USE_SELECT")

[unix]
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
[unix]
clean: (tests::clean) (bench::clean)
    rm -rf /tmp/znet-smoke-native /tmp/znet-smoke-select

# remove coverage data only (keeps the compiled test binaries)
[unix]
clean-cov: (tests::clean-cov)

# ---------------------------------------------------------------------------
# windows / MSVC
#
# `just check`, `just test` and `just bench` are wired to the batch scripts in
# tests\ and bench\; these recipes cover the rest.  misc\msvc-env.bat loads the
# MSVC environment when cl.exe is not on PATH, so this also works from ssh.
# MSVC has no -pedantic equivalent, so the C89/C++03 targets stay POSIX-only.

[windows]
test suite="tcp" *args="":
    @call "{{source_directory()}}\tests\run.bat" {{suite}} {{args}}

[windows]
test-select suite="tcp" *args="":
    @call "{{source_directory()}}\tests\run.bat" {{suite}} -select {{args}}

[windows]
smoke:
    @call "{{source_directory()}}\misc\msvc-env.bat" && cl /nologo /W4 /WX /I. example.c
    @example.exe

[windows]
smoke-select:
    @call "{{source_directory()}}\misc\msvc-env.bat" && cl /nologo /W4 /WX /DZN_USE_SELECT /I. example.c
    @example.exe

[windows]
clean: (tests::clean) (bench::clean)
    @if exist "{{source_directory()}}\example.exe" del /q "{{source_directory()}}\example.exe"
    @if exist "{{source_directory()}}\example.obj" del /q "{{source_directory()}}\example.obj"
