# examples

The same demo in three languages: a loopback TCP echo server with three
clients, one UDP ping, a timer, and the error paths in between. Each program
runs on its own event loop and exits when everything is done.

| File | API | Run from the repo root |
| --- | --- | --- |
| `example.c` | `znet.h` (C99) | `just smoke` |
| `example.cpp` | `znet.hpp` (C++11) | `just example-cpp` |
| `example.lua` | Lua binding | `just example-lua` |

`just examples` runs all three. They share ports 12345/12346 (TCP) and 8088
(UDP), so run them one at a time; `ZNET_EXAMPLE_PORT` changes the first Lua
port.

Build without `just` (the header lives one directory up):

```sh
# C, from the repo root
cc -std=c99 -O2 -I. -pthread examples/example.c -o example-c

# C++11 wrapper, from the repo root
c++ -std=c++11 -I. -pthread examples/example.cpp -o example-cpp

# Lua binding, from the repo root; then run with LUA_CPATH pointing at it
cc -std=c99 -O2 -fPIC -shared -pthread -I. -o znet.so zn_lua.c
LUA_CPATH='./?.so' lua examples/example.lua
```

`example.cpp` is the one to read for the C++ wrapper: it shows the lambda style
the wrapper is meant to be used with. Each handler is consumed by its
operation, so a callback installs the next step; a `shared_ptr` keeps a socket
or a borrowed buffer alive until its completion, and an accepted connection
arrives as its own `TcpSocketPtr`. The demo ends with `loop->runLoop()`
(`ZN_RUN_LOOP`), which returns once nothing is in flight — there is no exit
flag or watchdog.
