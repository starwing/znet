# znet benchmarks

Every program here is built by `just bench` into `bench/build/`; the server+client
pairs can be run with `just bench <name> [seconds]` from the repo root.

```sh
just bench                 # build everything (module default)
just bench echo 5          # 5 s of ping-pong echo
just bench flood 5         # 5 s of flood (one direction saturates)
just bench echo-buffer 5   # 5 s of echo through zn_buffer.h framing
just bench list            # what was built + manual usage
just bench clean
ZNET_BENCH_PORT=19000 just bench echo 3
```

The run recipes start the server, wait 0.4 s, start the client, sleep N seconds
and then `kill -9` both: the SIGINT handler in these programs calls `zn_close()`
from a signal handler, which is not async-signal-safe.  All programs set stdout
line-buffered, so the per-second counters survive the kill.

## Programs

| program | what it does |
| --- | --- |
| `bench_echo.c` | one binary, `bench_echo server\|client[N] [ip [port]]`: 1 KiB frames ping-pong; prints recv/send counters every second |
| `bench_flood.c` | same CLI; the server pushes continuously, the client floods back |
| `bench_echo_buffer.c` | same CLI; same echo but through `zn_buffer.h` send/receive buffering and 2-byte length framing |
| `bench_server.c` / `bench_client.c` | split pair: `bench_server [port]` + `bench_client [ip [port]]`, uses `zn_bufferpool.h` |
| `echo_server.c` | minimal echo server, manual runs |

`bench_echo.lua` is the original Lua driver; it is not wired into the justfile.

## Framing contract (why the length prefix is not added by hand)

`zn_buffer.h`'s header handler must return the **whole frame** (prefix + payload),
and the packet handler receives the frame **including its prefix**.  The old
bench added/removed the prefix itself and stalled after the first frame once
that contract changed.

## Status

All six programs are ported to the current API (the `packet` argument of
`zn_connect`/`zn_newaccept` is `0`) and compile clean under
`-std=c99 -pedantic -Wall -Wextra` (and `-std=c89 -pedantic-errors`).

The `.bat` wrappers that used to live in `tests/` are gone; the recipes above
replace them.
