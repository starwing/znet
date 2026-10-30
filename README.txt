znet - a simple C network library
---------------------------------

This library is inspired by Yawei Zhang's ZsummerX[1] library, but this one is
a pure C version. Now it implement a IOCP (IO Completion Port) backend on
Windows, and a epoll backend on Linux, a kqueue backend on BSD and Mac OS X
systems, and select backend for others.

The ZNet library itself is a single-header library[2], and have some utils
single-header libraries for common usages. all liraries are standalone and can
used without other headers.

See example.c for an example, and tests/ plus bench/ for the suite and
benchmarks.

This release makes three deliberate ABI breaks (the zn_Time widening, the
factory rewrite and the `int err` change below); the last two land together:
zn_newtcp/zn_newaccept/zn_newudp,
zn_closetcp/zn_closeaccept/zn_closeudp and the old zn_listen(accept, addr, port)
/ zn_connect(tcp, addr, port, cb, ud) are gone, replaced by the factories above
and zn_lasterror().  Completion callbacks (and zn_AddrInfoHandler) now report
`int err` instead of `unsigned err`, and zn_aierror() takes it back, so an
error compares against the ZN_* enum without a cast: every callback signature
changes too.  Recompile against the new znet.h; there is no compatibility
layer.

Build and test (just is the only build driver; there is no make wrapper):

    just check      full suite: native backend + select (ASan/UBSan)
    just test tcp   one suite, native backend
    just smoke      build and run example.c
    just test-lua   build the Lua binding and run tests/test.lua
    just cov        lcov coverage report for znet.h
    just bench      build the benchmark programs in bench/
    just bench echo 5   run the echo benchmark for 5 seconds

Windows (works from a Developer Command Prompt, a plain cmd window or an ssh
session: misc\msvc-env.bat finds Visual Studio with vswhere and loads the MSVC
environment when cl.exe is not on PATH.  lua must be on PATH for the runner
generator; just is optional):

    tests\check.bat      full suite with cl /W4 /WX (IOCP; select is POSIX-only)
    tests\run.bat tcp    one suite; tests\run.bat tcp tcp_connect for one test
    bench\build.bat      build the benchmarks with cl
    bench\run.bat echo 5 run the echo benchmark for 5 seconds

The IOCP suite passes on a Windows machine (VS2022 x64, `cl /W4 /WX`,
2026-10-28).  `znet.h` always uses IOCP on `_WIN32`, so there is no separate
select run there.  Re-run `tests\check.bat` after changing platform code: it
runs one process per test and rejects a run whose output has no " OK" line
(a test that dies without aborting would otherwise exit 0).

tests/README.md documents the suite, bench/README.md the benchmarks.

Utils:

zn_buffer.h: mainly based the luaL_Buffer in Lua code. But the interface is
targeted by send/recv method, so it's very useful to make znet send data
continually and receive packets by packets.

zn_deque.h: a double-direct pipe implement that can be used a queue or stack
cross threads. it implements block API like push/pop from deque, and non-block
API to fetch the front and back items from deque.

zn_task.h: a task thread pool implement. you can create a task pool and post
tasks to pool, task will run in other thread, you can use zn_deque.h to notice
original thread the completion of tasks.

Objects are created by factories, and the operation is part of the creation:

    zn_Accept *zn_listen(zn_State *S, const char *addr, unsigned port);
    zn_Tcp    *zn_connect(zn_State *S, const char *addr, unsigned port,
                          zn_ConnectHandler *cb, void *ud);
    zn_Udp    *zn_bind(zn_State *S, const char *addr, unsigned port);
    int        zn_lasterror(zn_State *S);

Each returns a live object or NULL, and on failure the reason is in
zn_lasterror(S) (ZN_EPARAM, ZN_ESTATE, ZN_ESOCKET, ZN_EBIND, ZN_ECONNECT,
ZN_EPOLL); a successful call sets it back to ZN_OK.  zn_lasterror() has to be
read right after the call: the next factory call overwrites it, like errno.
The only way to destroy an object is zn_deltcp()/zn_delaccept()/zn_deludp():
it closes the fd and gives up ownership, and the slot goes back to the pool
when the last completion it still owes has landed.  Call each of them exactly
once per object; after the call the pointer is dead, exactly like a freed
pointer.  zn_del*(NULL) is a no-op.

Objects are not reused and not reconnected: a tcp that has connected, failed
or been closed is finished.  To connect again, ask the factory for a new one.
zn_send()/zn_recv() are legal only once the connect callback said ZN_OK
(while CONNECTING they return ZN_ESTATE); after a terminal error the fd is
already closed, they return ZN_ESTATE, zn_getpeerinfo() still works, and the
next step is zn_deltcp().

UDP sockets are datagram sockets: zn_bind() creates and binds one (port 0 asks
the OS for an ephemeral port), and there is no listen/accept for UDP.
zn_connectudp() sets or changes the default peer; it is a plain synchronous
connect(), with no callback and no "one connect per object" rule.  After it,
zn_sendto(udp, buf, len, NULL, 0) sends to the connected peer, while
zn_sendto(udp, buf, len, addr, port) is the unconnected form and is not
supported on a connected socket.  zn_recvfrom() receives in both cases.

zn_sendto(), including the NULL/0 connected form, rejects datagrams larger
than ZN_MAX_UDPSEND (default 1200 bytes, which stays under the IPv6 minimum
MTU, so a datagram is never fragmented by the IP layer).  Define
ZN_MAX_UDPSEND to a larger value before including znet.h if the application
wants to own the fragmentation risk.

zn_starttimer() rejects a delay of ZN_FOREVER or larger with ZN_EPARAM: a delay
must stay below the "wait forever" sentinel, which doubles as "no deadline"
inside the timer heap.  A delay of 0 is valid and fires on the next poll.
Timers armed with the same deadline fire in an unspecified order; use distinct
delays when the order matters.

The znet library and utils have the same license when Lua[3], have fun :)


[1]: https://github.com/zsummer/zsummerX
[2]: https://github.com/nothings/stb
[3]: https://www.lua.org/license.html

