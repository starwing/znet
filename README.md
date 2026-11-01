<p align="center">
  <img src="misc/znet-logo.svg" width="112" height="112" alt="znet logo">
</p>

# znet

znet is a single-header C networking library built around an event loop. The
same API runs on IOCP (Windows), epoll (Linux), kqueue (BSD and macOS) and
select (fallback). It is a C rewrite of [zsummerX][zsummerx]; the only
dependencies are libc, the socket API and the platform's threads.

The core handles TCP, UDP, timers and cross-thread hand-off without hiding any
buffer from the caller. Optional headers add buffering, a thread pool and
asynchronous DNS. You allocate the memory, tell znet where it is, and get a
callback when the operation finishes.

**English** | [中文](README.zh.md)

<div align="center">

[![Build](https://github.com/starwing/znet/actions/workflows/ci.yml/badge.svg)](https://github.com/starwing/znet/actions/workflows/ci.yml) [![Coverage Status](https://coveralls.io/repos/github/starwing/znet/badge.svg?branch=master)](https://coveralls.io/github/starwing/znet?branch=master)

</div>

## Highlights

- **One header, one implementation unit.** Define `ZN_IMPLEMENTATION` in
  exactly one file and include `znet.h` there; every other file just includes
  it. Nothing to link except `-pthread` on POSIX (`ws2_32` on Windows, linked
  automatically by MSVC).
- **Four backends, one behavior.** IOCP on Windows, edge-triggered epoll on
  Linux, kqueue on BSD/macOS, select as the portable fallback. On POSIX,
  `ZN_USE_SELECT` forces select.
- **No hidden buffers.** `zn_send` and `zn_recv` borrow the caller's buffer and
  never copy it. The buffer must stay valid and untouched until the matching
  callback. `zn_buffer.h` adds optional buffering on top, but the core stays
  zero-copy.
- **One operation per direction.** A second send or receive while one is in
  flight returns `ZN_EBUSY`. Queueing, if you want it, is explicit.
- **Callbacks never run inside the call that armed them.** I/O callbacks are
  delivered from `zn_run()`, so a handler can be freed as soon as its operation
  completes.
- **Register once.** Sockets are registered with epoll/kqueue when they are
  created. Arming an operation costs at most one syscall; there is no
  per-operation re-registration.
- **Clean C.** Compiles as C89 and as C++03 without warnings, with no `long
  long`, VLAs or designated initializers. `zn_Time` is a `size_t` holding
  milliseconds.
- **Explicit ownership.** Factories create objects, `zn_del*` destroys them
  exactly once, and the pointer is dead afterwards. `zn_del*(NULL)` is a no-op.

## Performance

Measured on an Apple M4 Mac mini (macOS 27.2, Apple clang 21, `-O2`), loopback,
1 KiB frames, 4–5 s runs. The benchmark labels MiB as MB.

| Workload | Throughput |
| --- | --- |
| echo, 1 connection | ~47 MiB/s |
| echo, 4 connections | ~120 MiB/s |
| echo, 8 connections | ~155 MiB/s |
| flood, 1 connection, one direction | ~400 MiB/s |
| echo through `zn_buffer.h` framing | ~47 MiB/s |
| timer heap at 100k entries: arm / cancel / fire | 9.8 / 18.0 / 25.6 ns per op |

A single-connection echo is an RTT benchmark, not a throughput ceiling: 47 MiB/s
at 1 KiB is about 48k round trips per second, or 21 µs per round trip. Almost
all of that is event-loop wakeup latency, not library work. Adding connections
overlaps the round trips: 4 reach 120 MiB/s, 8 reach about 155 MiB/s, while the
syscall count per message stays almost flat. Flood removes the round trip and reaches about 400 MiB/s in
one direction.

The library's own cost is small. A 1 KiB message costs about four syscalls per
side (one send, one or two receives, one poll wait) and roughly 0.1 µs of
library code. The timer heap is a binary min-heap with an index per timer, so
cancel is O(log n) rather than a scan. Timer numbers move by 10–15% between
runs.

Reproduce with:

```sh
just bench              # build bench/build/*
just bench echo 5
just bench flood 5
just bench timer        # arm/cancel/fire/clock for 1k, 10k, 100k
```

## Quick start

Copy `znet.h` into your project, then define the implementation in one
translation unit:

```c
#define ZN_IMPLEMENTATION
#include "znet.h"
```

Every other file includes the header without the define. On Windows call
`zn_initialize()` before creating a state; on POSIX it is a no-op, but pairing
it with `zn_deinitialize()` keeps the code portable.

A complete echo server:

```c
#define ZN_IMPLEMENTATION
#include "znet.h"
#include <stdlib.h>

typedef struct Conn {
    char buf[1024];
} Conn;

static void on_recv(void *ud, zn_Tcp *tcp, int err, unsigned n);

static void on_sent(void *ud, zn_Tcp *tcp, int err, unsigned n) {
    Conn *c = (Conn *)ud;
    (void)n;                     /* a 1 KiB frame fits the socket buffer */
    if (err != ZN_OK) {          /* the library already closed the socket */
        zn_deltcp(tcp);
        free(c);
        return;
    }
    zn_recv(tcp, c->buf, sizeof c->buf, on_recv, c);
}

static void on_recv(void *ud, zn_Tcp *tcp, int err, unsigned n) {
    Conn *c = (Conn *)ud;
    if (err != ZN_OK || zn_send(tcp, c->buf, n, on_sent, c) != ZN_OK) {
        zn_deltcp(tcp);
        free(c);
    }
}

static void on_accept(void *ud, zn_Accept *a, int err, zn_Tcp *tcp) {
    Conn *c;
    if (err != ZN_OK) return;     /* still open, no longer armed */
    c = (Conn *)malloc(sizeof *c);
    if (c == NULL || zn_recv(tcp, c->buf, sizeof c->buf, on_recv, c) != ZN_OK) {
        free(c);
        zn_deltcp(tcp);
    }
    zn_accept(a, on_accept, ud);  /* one accept per call */
}

int main(void) {
    zn_State *S;
    zn_Accept *a;
    zn_initialize();
    S = zn_newstate();
    a = S ? zn_listen(S, "127.0.0.1", 12345) : NULL;
    if (a == NULL || zn_accept(a, on_accept, NULL) != ZN_OK) {
        if (S != NULL) zn_close(S);
        zn_deinitialize();
        return 1;
    }
    zn_run(S, ZN_RUN_LOOP);
    zn_close(S);
    zn_deinitialize();
    return 0;
}
```

Build and try it:

```sh
cc -std=c99 -O2 demo.c -pthread -o demo
./demo
printf 'hello\n' | nc 127.0.0.1 12345
```

[`examples/example.c`](examples/example.c) is the full C demo: two TCP clients, a UDP
server and client, a timer, and the error paths in between. Run it with `just smoke`.
[`examples/example.cpp`](examples/example.cpp) is the same demo against the C++ wrapper,
written with C++11 lambdas (`just example-cpp`);
[`examples/example.lua`](examples/example.lua) is the Lua version (`just example-lua`).
`just examples` runs all three.

## C API

All declarations are in `znet.h`; `S` is a `zn_State *`. I/O operations return
`ZN_OK` or a `ZN_*` code, and their callbacks receive the same codes. Address
arguments are numeric IPv4/IPv6 text, not hostnames -- use `zn_addrinfo.h` for
name resolution.

### State and loop

| Function | Usage |
| --- | --- |
| `void zn_initialize(void)` | Initialize the socket layer (Winsock on Windows; no-op elsewhere). Call once before any state. |
| `void zn_deinitialize(void)` | Release it after all states are closed. |
| `const char *zn_strerror(int err)` | Text for a `ZN_*` code. |
| `const char *zn_engine(void)` | Active backend: `"IOCP"`, `"epoll"`, `"kqueue"` or `"select"`. |
| `zn_State *zn_newstate(void)` | Create the event loop and its object pools. `NULL` on out-of-memory or a backend failure. |
| `void zn_close(zn_State *S)` | Delete every live object and free `S`. It may be called from a callback; teardown then completes after the current poll. The pointer is dead afterwards. |
| `int zn_lasterror(zn_State *S)` | Why the last `zn_listen`, `zn_connect` or `zn_bind` returned `NULL`. Overwritten by the next such call, so read it immediately. |
| `void *zn_getuserdata(zn_State *S)` | Read the caller pointer stored on the state. |
| `void zn_setuserdata(zn_State *S, void *ud)` | Store it. Each state has one slot. |
| `unsigned zn_retain(zn_State *S)` | Increment the keep-alive count. `ZN_RUN_LOOP` stays in the loop while it is non-zero. |
| `unsigned zn_release(zn_State *S)` | Decrement it. Every retain must be paired with one release. |
| `int zn_run(zn_State *S, int mode)` | `ZN_RUN_ONCE` waits for one poll; `ZN_RUN_CHECK` polls without blocking; `ZN_RUN_LOOP` polls until there is no work, timer or retain left. Returns 0 when idle. |
| `int zn_post(zn_State *S, zn_PostHandler *h, void *ud)` | Queue `h(ud, S)` for the loop thread. Safe from another thread while the loop runs; do not race it with `zn_close`. Returns `ZN_ERROR` if close has started or the wakeup failed. |
| `zn_Time zn_time(void)` | Monotonic milliseconds from an unspecified origin. On 32-bit builds it wraps after 49.7 days. |

### Timers

| Function | Usage |
| --- | --- |
| `zn_Timer *zn_newtimer(S, h, ud)` | Allocate a timer; `NULL` if the state is closing or out of memory. `h` has type `zn_Time h(void *ud, zn_Timer *t, zn_Time elapsed)`. |
| `void zn_deltimer(zn_Timer *t)` | Cancel and free the timer. |
| `int zn_starttimer(zn_Timer *t, zn_Time delayms)` | Arm or re-arm. A delay of 0 fires on the next poll; `ZN_FOREVER` or larger returns `ZN_EPARAM`; an allocation failure returns `ZN_ERROR`. |
| `void zn_canceltimer(zn_Timer *t)` | Disarm but keep the object, so it can be armed again. |

The handler's return value is the next delay in milliseconds: return 0 to stop,
and the timer object stays allocated until `zn_deltimer`. `elapsed` is the time
since the timer was armed. Timers with the same deadline fire in an unspecified
order.

### TCP acceptor

| Function | Usage |
| --- | --- |
| `zn_Accept *zn_listen(S, const char *addr, unsigned port)` | Create a listening socket. `addr` must be a numeric address. On failure returns `NULL` and sets `zn_lasterror`. |
| `int zn_accept(zn_Accept *a, zn_AcceptHandler *h, void *ud)` | Arm one accept. `h(ud, a, err, tcp)` runs once; call `zn_accept` again for the next connection. Returns `ZN_EBUSY` if one is already armed. |
| `void zn_delaccept(zn_Accept *a)` | Close the listener and give up the object. Use it exactly once. |

If the accept callback reports an error, `tcp` is `NULL` and the listener is
still open but no longer armed. Re-arm it with a backoff, or delete it. The
accepted `tcp` belongs to the callback; delete it when you are done.

### TCP socket

| Function | Usage |
| --- | --- |
| `zn_Tcp *zn_connect(S, addr, port, h, ud)` | Start a non-blocking connect. The object is usable when `h(ud, tcp, err)` reports `ZN_OK`; on error the object is broken and must be deleted. |
| `void zn_deltcp(zn_Tcp *tcp)` | Close the socket and release the object. Exactly once per object; the pointer is dead after the call. |
| `void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)` | Copy the peer's numeric address and host-order port. Still valid after a terminal error. |
| `int zn_send(zn_Tcp *t, buf, len, h, ud)` | Ask to send `len` bytes from `buf`. `h(ud, t, err, count)` reports bytes actually sent; a short send is possible, so send the rest yourself. |
| `int zn_recv(zn_Tcp *t, buf, len, h, ud)` | Receive into `buf`. `h(ud, t, err, count)` reports the bytes received. |

`zn_send` and `zn_recv` return `ZN_ESTATE` while the connection is still being
established, and `ZN_EBUSY` if that direction already has an operation in
flight. A terminal error in a callback (`ZN_ECLOSED`, `ZN_EHANGUP`, ...) means
the socket is already closed; the only remaining call is `zn_deltcp`.

### UDP socket

| Function | Usage |
| --- | --- |
| `zn_Udp *zn_bind(S, addr, port)` | Create and bind a datagram socket. Port 0 asks the OS for an ephemeral port. |
| `void zn_deludp(zn_Udp *udp)` | Close and release the socket, exactly once. |
| `int zn_connectudp(zn_Udp *udp, addr, port)` | Set or change the default peer. Synchronous, no callback, may be called again. |
| `int zn_sendto(zn_Udp *udp, buf, len, addr, port)` | Send one datagram. With `addr == NULL` and `port == 0` it goes to the connected peer; otherwise it is the unconnected form. The socket is not connected by this call. |
| `int zn_recvfrom(zn_Udp *udp, buf, len, h, ud)` | Arm one receive. `h(ud, udp, err, count, addr, port)` reports the sender and byte count; re-arm for the next datagram. |

`zn_sendto` is synchronous: it returns after the datagram is handed to the OS
and has no callback. After `zn_connectudp`, use the `NULL`/0 form only; passing
an address to a connected socket is not supported. Datagrams larger than
`ZN_MAX_UDPSEND` (1200 bytes by default, below the IPv6 minimum MTU) are
rejected unless the macro is raised before including the header.

### Lifetime rules

Four rules hold everywhere:

1. **The buffer is borrowed.** It must stay valid and unmodified from the call
   until its callback.
2. **One operation per direction.** `zn_send`, `zn_recv`, `zn_recvfrom` and
   `zn_accept` return `ZN_EBUSY` instead of queueing.
3. **Callbacks are deferred.** They run only from `zn_run()` (and queued posts
   may be drained by `zn_close`), never from the call that armed the operation.
4. **Delete once.** `zn_del*` for sockets and listeners, `zn_deltimer` for
   timers, `zn_close` for the state. After the call the pointer is dead. Sockets
   are not reused or reconnected -- call the factory again for a new one.

## Optional headers

These headers are independent. Include the ones you need in the implementation
unit next to `znet.h`; one `ZN_IMPLEMENTATION` covers all of them.

### `zn_addrinfo.h` -- asynchronous DNS

| Function | Usage |
| --- | --- |
| `int zn_getaddrinfo(S, node, service, flags, h, ud)` | Queue a lookup. `flags` is `ZN_TCP`, `ZN_ACCEPT` or `ZN_UDP`, optionally ORed with `ZN_IPV4` / `ZN_IPV6`. Returns `ZN_OK` if queued. |
| `const char *zn_aierror(int err)` | Text for a lookup error. The handler's `err` is 0 on success, otherwise a `getaddrinfo` code. |
| `void zn_closeaddrinfo(zn_State *S)` | Required before `zn_close(S)`: it drains this state's pending lookups and waits for an in-flight one. May block for one lookup. |

The handler is `void h(void *ud, int err, unsigned count, zn_PeerInfo *peers)`.
`peers` is a temporary array owned by the library: copy what you need before
returning. Passing `S = NULL` stops the worker thread.

### `zn_buffer.h` -- buffers, framing, send queue

| Function | Usage |
| --- | --- |
| `zn_newbuffcache(f, ud)` / `zn_delbuffcache(bc)` | Create or destroy a size-class buffer cache. `f` is a realloc-like allocator; `NULL` uses `malloc`/`realloc`. |
| `zn_prepbuffcache(bc, sz, count)` | Preallocate `count` blocks of size `sz`. Returns how many were cached. |
| `zn_initbuffer(b, bc)` / `zn_resetbuffer(b)` | Initialize a buffer (2000-byte inline storage) or return its heap block to the cache. |
| `zn_resizebuffer(b, len)` | Grow to at least `len` bytes. Returns the capacity after the call; a value below `len` means the grow failed. |
| `zn_prepbuffsize(b, len)` | Return a writable pointer to `len` reserved bytes, or `NULL` on out-of-memory. |
| `zn_addlstring(b, s, len)` / `zn_addchar(b, ch)` | Append; return 0 when the buffer could not grow. |
| `zn_initrecvbuffer(b, bc)` / `zn_resetrecvbuffer(b)` | Initialize or reset a framing receive buffer. |
| `zn_recvonheader(b, h, ud)` | Install the header handler: `size_t h(void *ud, const char *buff, size_t len)`. It returns the whole frame length, prefix included, or 0 if more bytes are needed. |
| `zn_recvonpacket(b, h, ud)` | Install the packet handler: `void h(void *ud, const char *buff, size_t len)`, called once per complete frame. |
| `zn_recvfinish(b, count)` | Consume the bytes just received. Returns 0 when done, 1 when more are needed, -1 when the accumulator could not grow. |
| `zn_initsendbuffer(b, bc)` / `zn_resetsendbuffer(b)` | Initialize or reset the two-buffer send queue. |
| `zn_sendprepare(b, buf, len)` | Queue a payload. Returns 1 when nothing is in flight and the caller should send now, 0 when queued behind it, -1 on out-of-memory. |
| `zn_sendfinish(b, count)` | Account for a completed send and move queued data forward. Returns non-zero while bytes remain to send. |

`zn_initrecvbuffer` installs no packet handler, and that is the documented
default: complete frames are consumed and dropped, never queued. The receive
buffer is a framing helper, not a packet store -- install `zn_recvonpacket`
before receiving if you want the payloads, and `zn_recvonpacket(b, NULL, NULL)`
puts the drop back when you do not. When `zn_recvfinish` returns -1, the framing
state is undefined; reset the buffer before reusing it.

The accessor macros (`zn_buffer`, `zn_bufflen`, `zn_addsize`, `zn_recvbuff`,
`zn_recvsize`, `zn_sendbuff`, `zn_sendsize`, `zn_curbuffer`, `zn_needsend`) are
defined in the header next to the structs.

### `zn_task.h` -- fixed-size thread pool

| Function | Usage |
| --- | --- |
| `zn_TaskPool *zn_newtaskpool(int nthread)` | Start the pool. Values outside 1..`ZN_MAX_THREAD_COUNT` (4) become 4. |
| `int zn_addtask(ws, h, ud)` | Queue `h(ud, ws)`. Returns 1 on success, 0 while closing or on out-of-memory. |
| `int zn_taskcount(zn_TaskPool *ws)` | Approximate queued task count; a hint, not a synchronization point. |
| `void zn_pausetasks(ws, int pause)` | Stop or resume dequeuing. Queued tasks stay queued. |
| `void zn_deltaskpool(zn_TaskPool *ws)` | Stop, join and free the pool. A task that never returns blocks this call. |

### `znet.hpp` -- C++11 wrapper

A thin `std::shared_ptr` wrapper in namespace `zsummer::network`, no extra
logic. Each handler is moved out and consumed by its operation, so install a new
`std::function` to continue. `TcpAccept::doAccept` creates one `TcpSocketPtr` per
accepted connection and passes it to the handler; an accept error passes an empty
pointer, and dropping the pointer closes the socket. `EventLoop::runLoop()`
wraps `ZN_RUN_LOOP` and returns when no operation or timer is left, while
`runOnce()` polls once. The C test suite targets `znet.h`;
[`examples/example.cpp`](examples/example.cpp) covers the wrapper
(`just example-cpp`).

## Lua binding

`zn_lua.c` builds a Lua module on top of `znet.h`, `zn_addrinfo.h` and
`zn_buffer.h`. Lua 5.3 or newer is required.

```sh
# Linux/BSD (the macro keeps addrinfo visible under -std=c99 on glibc)
cc -std=c99 -D_XOPEN_SOURCE=600 -O2 -fPIC -shared -pthread \
   $(pkg-config --cflags lua5.4) -I. -o znet.so zn_lua.c
# macOS
cc -std=c99 -O2 -fPIC -bundle -undefined dynamic_lookup -pthread \
   $(pkg-config --cflags lua5.4) -I. -o znet.so zn_lua.c
```

`just test-lua` builds it and runs the loopback smoke test in `tests/test.lua`;
`just example-lua` runs the standalone demo in `examples/example.lua`.
Put `znet.so` where `package.cpath` finds it, then:

```lua
-- talks to the echo server above
local znet = require "znet".new()

local tcp = znet:tcp("127.0.0.1", 12345, function(self, err)
    if err then error(err) end
    self:send("hello"):receive(function(_, chunk)
        print(chunk)
        self:close()           -- closes once queued sends drain
        return #chunk          -- one whole chunk is one frame
    end)
end)

znet:run()                     -- returns when no operation is in flight
tcp:delete()
znet:delete()
```

Close or delete the objects you created before `znet:delete()`: the state owns
their C objects, and the Lua wrappers are not invalidated when it goes away.

`require "znet"` returns a factory table; `.new()` creates a state. Errors are
reported as `nil, message, code`; object-returning methods chain.

| Object | Methods |
| --- | --- |
| state | `:tcp([addr, port, onconnect])`, `:accept(onaccept)`, `:udp([addr, port])`, `:timer(ontimer)`, `:getaddrinfo(node, service [, flags], fn)`, `:run([mode])` (`loop`, `once` or `check`), `:delete()` |
| tcp | `:connect(addr, port, fn)`, `:send(data [, i [, j]])`, `:receive(onchunk [, onpacket])`, `:close()`, `:delete()`, `:getpeerinfo()`, `:onerror(fn)` |
| accept | `:listen(addr, port)`, `:close()`, `:delete()` |
| udp | `:connect(addr, port)`, `:send(data [, i [, j]])`, `:sendto(addr, port, data [, i [, j]])`, `:receivefrom(fn)`, `:delete()` |
| timer | `:start(ms)`, `:cancel()`, `:delete()` |

Callbacks:

- `onconnect(tcp)` on success, `onconnect(tcp, message, code)` on failure.
- `onaccept(self, tcp)` receives a connected `zn_Tcp`; return `true` to accept
  the next connection, anything else closes the listener.
- `onchunk(self, data)` is the receive header handler; return the total frame
  length, or the chunk length (or nothing) to treat the received chunk as one
  frame. `onpacket(self, data)` receives each complete frame when supplied.
- `ontimer(self, elapsed)` returns a delay in milliseconds to re-arm, or
  `false`/0 to stop.
- `onrecvfrom(self, data, addr, port)`; return `true` to keep receiving.
- `onerror(self, message, code)` reports a send/receive error; the binding then
  closes the tcp object.

`tcp:close()` closes the socket once the queued send data has drained.
`znet:engine` holds the backend name, as `zn_engine()` does in C.

## Build and test

`just` drives everything; the batch files in `tests/` and `bench/` do the same
on Windows without `just`.

| Command | What it does |
| --- | --- |
| `just check` | Full suite on the native backend and on select, with ASan/UBSan. |
| `just test tcp` | One suite on the native backend. |
| `just smoke` | Build and run `examples/example.c`. |
| `just example-cpp` | Build and run `examples/example.cpp` (C++11 wrapper). |
| `just example-lua` | Build the Lua module and run `examples/example.lua`. |
| `just examples` | All three example programs. |
| `just check-c89` / `just check-cpp03` | Strict C89 / C++03 compile checks. |
| `just test-lua` | Build the Lua module and run `tests/test.lua`. |
| `just cov` | lcov coverage report. |
| `just bench` / `just bench echo 5` | Build the benchmarks / run one. |

On Windows, from any shell:

```bat
tests\check.bat        full suite with cl /W4 /WX (IOCP)
tests\run.bat tcp      one suite
bench\build.bat        build the benchmarks
```

The suite passes on macOS (kqueue and select), Linux (epoll and select) and
Windows x64 (IOCP, VS2022, `cl /W4 /WX`). [tests/README.md](tests/README.md) documents the harness and
[bench/README.md](bench/README.md) the benchmarks.

## Files

| Path | Contents |
| --- | --- |
| `znet.h` | The library: public API and the single-expansion implementation. |
| `znet.hpp` | Optional C++11 wrapper. |
| `zn_buffer.h` | Optional buffer cache, receive framing, send queue. |
| `zn_task.h` | Optional fixed-size thread pool. |
| `zn_addrinfo.h` | Optional asynchronous DNS. |
| `zn_lua.c`, `lbind.h` | Lua binding and the vendored binding helpers. |
| `examples/` | The TCP/UDP/timer demo in C, C++ (`znet.hpp`) and Lua. |
| `tests/`, `bench/` | Regression suite and benchmarks. |
| `misc/` | Logo and helper scripts. |

## Status

This is the v2 API: objects come from factories, completion callbacks take
`int err`, and UDP has a connected form. There is no compatibility layer for
pre-v2 code.

Known limits:

- `zn_deque.h` is an unfinished draft and does not compile. Do not use it.
- The C++ wrapper is a convenience shim; the C API is the tested surface.
- Timers with identical deadlines have no ordering guarantee.

## Contributing

Run `just check` before sending a patch. A bug report is easier to reproduce
when it names the platform and the backend from `zn_engine()`.

## License

MIT. See [LICENSE](LICENSE).

znet is inspired by Yawei Zhang's [zsummerX][zsummerx]. The Lua binding builds
on Lua and the vendored `lbind.h`.

[zsummerx]: https://github.com/zsummer/zsummerX
