<p align="center">
  <img src="misc/znet-logo.svg" width="112" height="112" alt="znet logo">
</p>

# znet

znet 是一个单头文件的 C 网络库，核心是一个事件循环。同一套 API 可以跑在
IOCP（Windows）、epoll（Linux）、kqueue（BSD 与 macOS）和 select（兜底）上。
它是 [zsummerX][zsummerx] 的 C 重写，除 libc、socket API 和平台的线程库外没有依赖。

它处理 TCP、UDP、定时器和跨线程投递，可选头文件再提供缓冲、线程池和异步 DNS。
它不替调用方藏缓冲区：内存由你分配，把地址告诉 znet，操作完成时收到回调。

[English](README.md) | **中文**

<div align="center">

[![Build](https://github.com/starwing/znet/actions/workflows/ci.yml/badge.svg)](https://github.com/starwing/znet/actions/workflows/ci.yml) [![Coverage Status](https://coveralls.io/repos/github/starwing/znet/badge.svg?branch=master)](https://coveralls.io/github/starwing/znet?branch=master)

</div>

## 实现亮点

- **一个头文件，一个实现单元。** 只在一个文件里定义 `ZN_IMPLEMENTATION` 并包含
  `znet.h`，其余文件正常包含即可。POSIX 上只需加 `-pthread`，Windows 上 MSVC 会
  自动链接 `ws2_32`，此外不需要链接别的库。
- **四种后端，一种行为。** Windows 用 IOCP，Linux 用边沿触发的 epoll，BSD/macOS
  用 kqueue，其他平台退回 select。POSIX 上定义 `ZN_USE_SELECT` 可强制使用 select。
- **不隐藏缓冲。** `zn_send`、`zn_recv` 只借用调用方的缓冲区，从不复制；该缓冲区
  必须保持有效且不被改动，直到对应回调发生。`zn_buffer.h` 在这一层之上提供可选的
  缓冲，库核心仍然是零拷贝。
- **每个方向只有一个在途操作。** 同一方向已有操作时，再调用发送或接收会返回
  `ZN_EBUSY`。需要排队就显式排，队列归调用方管。
- **回调不会在武装它的调用里执行。** I/O 回调都从 `zn_run()` 投递，所以操作完成
  后 handler 可以立即释放。
- **只注册一次。** 连接建立时把 fd 注册进 epoll/kqueue；之后每次武装操作最多一次
  系统调用，不存在按操作重复注册。
- **Clean C。** 同时按 C89 和 C++03 编译通过且无警告：不用 `long long`、VLA 和
  designated initializer。`zn_Time` 是 `size_t`，单位毫秒。
- **所有权明确。** 对象由工厂函数创建，`zn_del*` 恰好销毁一次，调用之后指针即失效。
  `zn_del*(NULL)` 是空操作。

## 性能

测试环境：Apple M4 Mac mini（macOS 27.2，Apple clang 21，`-O2`），loopback，
1 KiB 消息，运行 4–5 秒。基准程序输出的是 MiB，只是标签写成 MB，表中沿用这个数字。

| 负载 | 吞吐 |
| --- | --- |
| echo，1 个连接 | ~47 MiB/s |
| echo，4 个连接 | ~120 MiB/s |
| echo，8 个连接 | ~155 MiB/s |
| flood，1 个连接，单向 | ~400 MiB/s |
| 经 `zn_buffer.h` 分帧的 echo | ~47 MiB/s |
| 定时器堆 100k：arm / cancel / fire | 9.8 / 18.0 / 25.6 ns 每次操作 |

单连接 echo 是延迟指标，不是吞吐上限：1 KiB 下 47 MiB/s 约等于每秒 4.8 万次往返，
每次往返 21 µs，几乎都花在事件循环的唤醒上，而不是库的代码里。加连接可以让往返
重叠起来：4 个连接约 120 MiB/s，8 个约 155 MiB/s，而每条消息的系统调用数几乎不变。
flood 去掉往返，单向约 400 MiB/s。

库自身的开销很小：每条 1 KiB 消息，一侧约四次系统调用（一次 send、一到两次 recv、
一次 poll 等待），夹在中间的用户态代码约 0.1 µs。定时器是带索引的二叉小顶堆，
cancel 是 O(log n) 而不是线性扫描。定时器的数字每次运行有 10–15% 的波动。

复现：

```sh
just bench              # 构建 bench/build/*
just bench echo 5
just bench flood 5
just bench timer        # 1k/10k/100k 的 arm/cancel/fire/clock
```

## 快速上手

把 `znet.h` 复制进项目，在一个编译单元里定义实现：

```c
#define ZN_IMPLEMENTATION
#include "znet.h"
```

其他文件直接包含头文件，不要再定义这个宏。Windows 上先调用 `zn_initialize()`；
POSIX 上它是空操作，但成对写上 `zn_deinitialize()` 可以让代码保持可移植。

一个完整的 echo 服务器：

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
    (void)n;                     /* 1 KiB 的帧放得进 socket 缓冲 */
    if (err != ZN_OK) {          /* 库已经把 socket 关掉了 */
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
    if (err != ZN_OK) return;     /* 监听器仍在，但已解除武装 */
    c = (Conn *)malloc(sizeof *c);
    if (c == NULL || zn_recv(tcp, c->buf, sizeof c->buf, on_recv, c) != ZN_OK) {
        free(c);
        zn_deltcp(tcp);
    }
    zn_accept(a, on_accept, ud);  /* 每次调用只接受一个连接 */
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

编译并试一下：

```sh
cc -std=c99 -O2 demo.c -pthread -o demo
./demo
printf 'hello\n' | nc 127.0.0.1 12345
```

[`examples/example.c`](examples/example.c) 是完整的 C 演示：两个 TCP 客户端、一对 UDP
收发、一个定时器，以及中间的错误处理。`just smoke` 会构建并运行它。
[`examples/example.cpp`](examples/example.cpp) 是同一演示的 C++ 包装版本，用 C++11
lambda 写成（`just example-cpp`）；[`examples/example.lua`](examples/example.lua) 是 Lua
版本（`just example-lua`）。`just examples` 会依次跑这三个程序。

## C API

声明都在 `znet.h` 里，`S` 是 `zn_State *`。I/O 操作返回 `ZN_OK` 或 `ZN_*` 错误码，
回调收到的是同一套错误码。地址参数是数字形式的 IPv4/IPv6 字符串，不是主机名；
需要解析域名时用 `zn_addrinfo.h`。

### 状态与事件循环

| 函数 | 用法 |
| --- | --- |
| `void zn_initialize(void)` | 初始化 socket 层（Windows 上是 Winsock，其他平台为空）。在创建任何 state 之前调用一次。 |
| `void zn_deinitialize(void)` | 所有 state 关闭之后释放它。 |
| `const char *zn_strerror(int err)` | 取 `ZN_*` 错误码的文字说明。 |
| `const char *zn_engine(void)` | 当前后端：`"IOCP"`、`"epoll"`、`"kqueue"` 或 `"select"`。 |
| `zn_State *zn_newstate(void)` | 创建事件循环和对象池。内存不足或后端初始化失败时返回 `NULL`。 |
| `void zn_close(zn_State *S)` | 删除所有存活对象并释放 `S`。可以在回调里调用，此时清理会在本轮 poll 结束后完成。调用之后指针失效。 |
| `int zn_lasterror(zn_State *S)` | 上一次 `zn_listen`、`zn_connect` 或 `zn_bind` 返回 `NULL` 的原因。下一次这类调用会覆盖它，要立刻读取。 |
| `void *zn_getuserdata(zn_State *S)` | 读取挂在 state 上的调用方指针。 |
| `void zn_setuserdata(zn_State *S, void *ud)` | 写入它。每个 state 有一个槽位。 |
| `unsigned zn_retain(zn_State *S)` | 保活计数加一。计数非零时 `ZN_RUN_LOOP` 不会退出。 |
| `unsigned zn_release(zn_State *S)` | 保活计数减一。每次 retain 都必须配一次 release。 |
| `int zn_run(zn_State *S, int mode)` | `ZN_RUN_ONCE` 阻塞等一轮 poll；`ZN_RUN_CHECK` 不阻塞；`ZN_RUN_LOOP` 一直跑到没有事件、定时器和保活计数。空闲时返回 0。 |
| `int zn_post(zn_State *S, zn_PostHandler *h, void *ud)` | 把 `h(ud, S)` 排到循环线程执行。循环运行时可以从其他线程调用，但不能和 `zn_close` 并发。close 已开始或唤醒失败时返回 `ZN_ERROR`。 |
| `zn_Time zn_time(void)` | 单调递增的毫秒数，起点未定义。32 位构建约 49.7 天回绕一次。 |

### 定时器

| 函数 | 用法 |
| --- | --- |
| `zn_Timer *zn_newtimer(S, h, ud)` | 分配一个定时器；state 正在关闭或内存不足时返回 `NULL`。`h` 的类型是 `zn_Time h(void *ud, zn_Timer *t, zn_Time elapsed)`。 |
| `void zn_deltimer(zn_Timer *t)` | 取消并释放定时器。 |
| `int zn_starttimer(zn_Timer *t, zn_Time delayms)` | 武装或重新武装。延迟为 0 表示下一轮 poll 触发；`ZN_FOREVER` 及以上返回 `ZN_EPARAM`，扩容失败返回 `ZN_ERROR`。 |
| `void zn_canceltimer(zn_Timer *t)` | 取消触发但保留对象，之后可以再次武装。 |

handler 的返回值是下一次触发的毫秒数：返回 0 就停止触发，定时器对象仍然存在，直到
`zn_deltimer`。`elapsed` 是本次武装之后经过的时间。同一个截止时间上的多个定时器，
触发顺序不作保证。

### TCP 监听器

| 函数 | 用法 |
| --- | --- |
| `zn_Accept *zn_listen(S, const char *addr, unsigned port)` | 创建监听 socket。`addr` 必须是数字地址。失败返回 `NULL` 并写入 `zn_lasterror`。 |
| `int zn_accept(zn_Accept *a, zn_AcceptHandler *h, void *ud)` | 武装一次 accept。`h(ud, a, err, tcp)` 只运行一次，要接受下一个连接就再调用一次。已有在途 accept 时返回 `ZN_EBUSY`。 |
| `void zn_delaccept(zn_Accept *a)` | 关闭监听 socket 并交出对象，恰好用一次。 |

如果 accept 回调报了错，`tcp` 是 `NULL`，监听 socket 仍然打开但已不再武装。可以退避
一段时间后重新武装，或者直接删除。接到的 `tcp` 归回调所有，用完由回调删除。

### TCP 连接

| 函数 | 用法 |
| --- | --- |
| `zn_Tcp *zn_connect(S, addr, port, h, ud)` | 发起非阻塞连接。`h(ud, tcp, err)` 报告 `ZN_OK` 后对象才能收发；报错时对象已损坏，必须删除。 |
| `void zn_deltcp(zn_Tcp *tcp)` | 关闭 socket 并释放对象。每个对象恰好一次，调用之后指针失效。 |
| `void zn_getpeerinfo(zn_Tcp *tcp, zn_PeerInfo *info)` | 取对方的数字地址和主机序端口。连接出错之后仍然可读。 |
| `int zn_send(zn_Tcp *t, buf, len, h, ud)` | 从 `buf` 发 `len` 字节。`h(ud, t, err, count)` 报告实际发出的字节数；可能只发一部分，剩余部分由调用方续发。 |
| `int zn_recv(zn_Tcp *t, buf, len, h, ud)` | 把数据收到 `buf`。`h(ud, t, err, count)` 报告收到的字节数。 |

连接还在建立时，`zn_send`/`zn_recv` 返回 `ZN_ESTATE`；同一方向已有在途操作时返回
`ZN_EBUSY`。如果回调收到的是终止性错误（`ZN_ECLOSED`、`ZN_EHANGUP` 等），socket
已经被库关闭，此时唯一还能做的是 `zn_deltcp`。

### UDP 套接字

| 函数 | 用法 |
| --- | --- |
| `zn_Udp *zn_bind(S, addr, port)` | 创建并绑定数据报 socket。端口 0 表示让系统分配临时端口。 |
| `void zn_deludp(zn_Udp *udp)` | 关闭并释放 socket，恰好一次。 |
| `int zn_connectudp(zn_Udp *udp, addr, port)` | 设置或更换默认对端。同步调用，没有回调，可以多次调用。 |
| `int zn_sendto(zn_Udp *udp, buf, len, addr, port)` | 发送一个数据报。`addr == NULL` 且 `port == 0` 时发给已连接的对端，否则是未连接形式。这个调用本身不会让 socket 变成已连接。 |
| `int zn_recvfrom(zn_Udp *udp, buf, len, h, ud)` | 武装一次接收。`h(ud, udp, err, count, addr, port)` 报告发送方和字节数；收下一个数据报要重新武装。 |

`zn_sendto` 是同步的：数据报交给系统后立即返回，没有回调。`zn_connectudp` 之后只能
用 `NULL`/0 的形式，给已连接的 socket 传地址是不支持的。超过 `ZN_MAX_UDPSEND`
（默认 1200 字节，低于 IPv6 最小 MTU）的数据报会被拒绝，除非在包含头文件之前把
这个宏调大。

### 生命周期规则

四条规则贯穿整个 API：

1. **缓冲区是借用的。** 从调用开始到对应回调发生，它必须保持有效且不被修改。
2. **每个方向只有一个在途操作。** `zn_send`、`zn_recv`、`zn_recvfrom`、`zn_accept`
   在已有操作时返回 `ZN_EBUSY`，不会替你排队。
3. **回调是延迟的。** 它们只从 `zn_run()` 出来（`zn_close` 可能排空已入队的 post），
   不会在武装操作的那个调用里执行。
4. **只删一次。** socket 和监听器用 `zn_del*`，定时器用 `zn_deltimer`，state 用
   `zn_close`。之后指针失效。socket 不会复用，也不会重连；要再来一次就重新调用工厂。

## 可选头文件

这些头文件彼此独立。需要哪个就在实现单元里和 `znet.h` 一起包含；一个
`ZN_IMPLEMENTATION` 就能覆盖它们全部。

### `zn_addrinfo.h`：异步 DNS

| 函数 | 用法 |
| --- | --- |
| `int zn_getaddrinfo(S, node, service, flags, h, ud)` | 排入一次解析。`flags` 取 `ZN_TCP`、`ZN_ACCEPT` 或 `ZN_UDP`，可以和 `ZN_IPV4` / `ZN_IPV6` 按位或。入队成功返回 `ZN_OK`。 |
| `const char *zn_aierror(int err)` | 取解析错误的文字说明。handler 的 `err` 成功时为 0，否则是 `getaddrinfo` 的错误码。 |
| `void zn_closeaddrinfo(zn_State *S)` | 在 `zn_close(S)` 之前必须调用：排空该 state 的待处理查询，并等待在途查询落地。最多阻塞一次解析。 |

handler 的形式是 `void h(void *ud, int err, unsigned count, zn_PeerInfo *peers)`。
`peers` 是库拥有的临时数组，返回前把需要的内容复制走。传 `S = NULL` 会停掉 worker
线程。

### `zn_buffer.h`：缓冲、分帧与发送队列

| 函数 | 用法 |
| --- | --- |
| `zn_newbuffcache(f, ud)` / `zn_delbuffcache(bc)` | 创建或销毁按尺寸档位复用的缓冲缓存。`f` 是类似 realloc 的分配器，传 `NULL` 用 `malloc`/`realloc`。 |
| `zn_prepbuffcache(bc, sz, count)` | 预分配 `count` 个 `sz` 大小的块，返回实际缓存的块数。 |
| `zn_initbuffer(b, bc)` / `zn_resetbuffer(b)` | 初始化缓冲区（内联 2000 字节），或把堆块还给缓存。 |
| `zn_resizebuffer(b, len)` | 扩到至少 `len` 字节。返回调用后的容量；返回值小于 `len` 表示扩容失败。 |
| `zn_prepbuffsize(b, len)` | 返回一段可写的保留空间，失败（含溢出）返回 `NULL`。 |
| `zn_addlstring(b, s, len)` / `zn_addchar(b, ch)` | 追加内容，无法扩容时返回 0。 |
| `zn_initrecvbuffer(b, bc)` / `zn_resetrecvbuffer(b)` | 初始化或重置分帧接收缓冲区。 |
| `zn_recvonheader(b, h, ud)` | 安装头处理函数：`size_t h(void *ud, const char *buff, size_t len)`。它返回整个帧长（含前缀），返回 0 表示还要更多字节。 |
| `zn_recvonpacket(b, h, ud)` | 安装包处理函数：`void h(void *ud, const char *buff, size_t len)`，每个完整帧调用一次。 |
| `zn_recvfinish(b, count)` | 消费刚收到的字节。返回 0 表示处理完，1 表示还要更多，-1 表示累加器无法扩容。 |
| `zn_initsendbuffer(b, bc)` / `zn_resetsendbuffer(b)` | 初始化或重置双缓冲的发送队列。 |
| `zn_sendprepare(b, buf, len)` | 排入一段数据。返回 1 表示当前没有在途发送、应立即发；0 表示排在后面；-1 表示内存不足。 |
| `zn_sendfinish(b, count)` | 记录一次完成并推进队列。还有数据要发时返回非零。 |

`zn_initrecvbuffer` 不安装包处理函数：接收之前先调用 `zn_recvonpacket`，否则完整的帧
会被丢弃。`zn_recvfinish` 返回 -1 时分帧状态未定义，复用前先重置缓冲区。

访问宏（`zn_buffer`、`zn_bufflen`、`zn_addsize`、`zn_recvbuff`、`zn_recvsize`、
`zn_sendbuff`、`zn_sendsize`、`zn_curbuffer`、`zn_needsend`）与结构体定义放在一起。

### `zn_task.h`：固定大小线程池

| 函数 | 用法 |
| --- | --- |
| `zn_TaskPool *zn_newtaskpool(int nthread)` | 启动线程池。超出 1..`ZN_MAX_THREAD_COUNT`（4）的值按 4 处理。 |
| `int zn_addtask(ws, h, ud)` | 排入 `h(ud, ws)`。成功返回 1，正在关闭或内存不足返回 0。 |
| `int zn_taskcount(zn_TaskPool *ws)` | 大致的待处理任务数，只是提示，不是同步原语。 |
| `void zn_pausetasks(ws, int pause)` | 暂停或恢复取任务。已排队的任务留在队列里。 |
| `void zn_deltaskpool(zn_TaskPool *ws)` | 停止、join 并释放线程池。永远不返回的任务会让这个调用一直阻塞。 |

### `znet.hpp`：C++11 包装

`zsummer::network` 命名空间下的 `std::shared_ptr` 薄包装，不增加逻辑。每个 handler
在一次操作之后就被 move 出去消费掉，要继续就得重新安装 `std::function`。
`TcpAccept::doAccept` 每次 accept 都新建一个 `TcpSocketPtr` 交给 handler；accept 出错时
传入空指针，丢掉这个指针就会关闭连接。`EventLoop::runLoop()` 包装 `ZN_RUN_LOOP`，在
没有任何操作和定时器时返回；`runOnce()` 只 poll 一次。测试套件针对的是 `znet.h`，包装层
由 [`examples/example.cpp`](examples/example.cpp) 覆盖（`just example-cpp`）。

## Lua 绑定

`zn_lua.c` 在 `znet.h`、`zn_addrinfo.h` 和 `zn_buffer.h` 之上构建 Lua 模块，要求
Lua 5.3 及以上。

```sh
# Linux/BSD（glibc 下 -std=c99 会隐藏 addrinfo，靠这个宏打开）
cc -std=c99 -D_XOPEN_SOURCE=600 -O2 -fPIC -shared -pthread \
   $(pkg-config --cflags lua5.4) -I. -o znet.so zn_lua.c
# macOS
cc -std=c99 -O2 -fPIC -bundle -undefined dynamic_lookup -pthread \
   $(pkg-config --cflags lua5.4) -I. -o znet.so zn_lua.c
```

`just test-lua` 会构建它并跑 `tests/test.lua` 里的 loopback 冒烟测试；
`just example-lua` 跑 `examples/example.lua` 里独立成篇的演示。把 `znet.so`
放到 `package.cpath` 能找到的位置，然后：

```lua
-- 对上文的 echo 服务器说话
local znet = require "znet".new()

local tcp = znet:tcp("127.0.0.1", 12345, function(self, err)
    if err then error(err) end
    self:send("hello"):receive(function(_, chunk)
        print(chunk)
        self:close()           -- 排队的数据发完后关闭
        return #chunk          -- 一整块当作一帧
    end)
end)

znet:run()                     -- 没有在途操作时返回
tcp:delete()
znet:delete()
```

删除 state 之前，先 close 或 delete 自己创建的对象：它们的 C 对象归 state 所有，
而 state 消失时 Lua 包装层不会被自动置空。

`require "znet"` 返回一个工厂表，`.new()` 创建一个 state。出错时返回
`nil, message, code`，会返回对象的方法可以链式调用。

| 对象 | 方法 |
| --- | --- |
| state | `:tcp([addr, port, onconnect])`、`:accept(onaccept)`、`:udp([addr, port])`、`:timer(ontimer)`、`:getaddrinfo(node, service [, flags], fn)`、`:run([mode])`（`loop`、`once` 或 `check`）、`:delete()` |
| tcp | `:connect(addr, port, fn)`、`:send(data [, i [, j]])`、`:receive(onchunk [, onpacket])`、`:close()`、`:delete()`、`:getpeerinfo()`、`:onerror(fn)` |
| accept | `:listen(addr, port)`、`:close()`、`:delete()` |
| udp | `:connect(addr, port)`、`:send(data [, i [, j]])`、`:sendto(addr, port, data [, i [, j]])`、`:receivefrom(fn)`、`:delete()` |
| timer | `:start(ms)`、`:cancel()`、`:delete()` |

回调：

- `onconnect(tcp)` 表示成功，`onconnect(tcp, message, code)` 表示失败。
- `onaccept(self, tcp)` 收到一个已连接的 `zn_Tcp`。返回 `true` 继续接受下一个连接，
  返回其他值会关闭监听器。
- `onchunk(self, data)` 是接收的头处理函数：返回总帧长；返回块长度或不返回，则把收到
  的这块当作一帧。给了 `onpacket` 时，`onpacket(self, data)` 收到每个完整帧。
- `ontimer(self, elapsed)` 返回下一次触发的毫秒数，返回 `false`/0 停止。
- `onrecvfrom(self, data, addr, port)`，返回 `true` 继续接收。
- `onerror(self, message, code)` 报告发送或接收错误，随后绑定会关闭这个 tcp 对象。

`tcp:close()` 在已排队的发送数据排空后关闭 socket。`znet:engine` 里是后端名字，对应
C 的 `zn_engine()`。

## 构建与测试

构建、测试和基准都由 `just` 驱动；`tests/` 和 `bench/` 里的批处理文件在 Windows
上提供等价入口，不装 `just` 也能用。

| 命令 | 作用 |
| --- | --- |
| `just check` | 原生后端 + select 的完整套件，带 ASan/UBSan。 |
| `just test tcp` | 只跑一个套件（原生后端）。 |
| `just smoke` | 构建并运行 `examples/example.c`。 |
| `just example-cpp` | 构建并运行 `examples/example.cpp`（C++11 包装）。 |
| `just example-lua` | 构建 Lua 模块并跑 `examples/example.lua`。 |
| `just examples` | 依次运行三个示例程序。 |
| `just check-c89` / `just check-cpp03` | 严格 C89 / C++03 编译检查。 |
| `just test-lua` | 构建 Lua 模块并跑 `tests/test.lua`。 |
| `just cov` | 生成 lcov 覆盖率报告。 |
| `just bench` / `just bench echo 5` | 构建基准 / 跑一个基准。 |

Windows 上任意命令行窗口：

```bat
tests\check.bat        完整套件，cl /W4 /WX（IOCP）
tests\run.bat tcp      单个套件
bench\build.bat        构建基准程序
```

套件在 macOS（kqueue 与 select）、Linux（epoll 与 select）和 Windows x64
（IOCP，VS2022，`cl /W4 /WX`）上通过。测试结构见 [tests/README.md](tests/README.md)，基准见
[bench/README.md](bench/README.md)。

## 文件

| 路径 | 内容 |
| --- | --- |
| `znet.h` | 库本体：公共 API 和单次展开的实现。 |
| `znet.hpp` | 可选的 C++11 包装。 |
| `zn_buffer.h` | 可选的缓冲缓存、接收分帧和发送队列。 |
| `zn_task.h` | 可选的固定大小线程池。 |
| `zn_addrinfo.h` | 可选的异步 DNS。 |
| `zn_lua.c`、`lbind.h` | Lua 绑定与随附的绑定辅助代码。 |
| `examples/` | TCP/UDP/定时器示例，分别用 C、C++（`znet.hpp`）和 Lua 写。 |
| `tests/`、`bench/` | 回归测试套件与基准程序。 |
| `misc/` | Logo 和辅助脚本。 |

## 状态

这是 v2 API：对象由工厂函数创建，完成回调统一收 `int err`，UDP 有已连接形式。
旧版代码没有兼容层。

已知限制：

- `zn_deque.h` 是未完成的草稿，无法编译，不要使用。
- C++ 包装只是便利层；经过测试的是 C API。
- 同一截止时间的定时器之间没有顺序保证。

## 参与贡献

提交补丁前先跑 `just check`。报告问题时写明平台和 `zn_engine()` 给出的后端，
会更容易复现。

## 许可证

MIT，见 [LICENSE](LICENSE)。

znet 受 Yawei Zhang 的 [zsummerX][zsummerx] 启发。Lua 绑定基于 Lua 和随附的
`lbind.h`。

[zsummerx]: https://github.com/zsummer/zsummerX
