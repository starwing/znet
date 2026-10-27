# TODO — znet 缺陷清单与改进优先级

> 基线：HEAD `4380d8b` 之上的未提交修复（`znet.h`/`zn_buffer.h`/`zn_lua.c`）、
> `tests/` 回归套件和 `bench/` 基准；`git status` 是当前状态的唯一依据。
> 构建入口只有根 `justfile`（`just check` / `test` / `cov` / `bench`），`GNUmakefile`
> 已删除。
> `.clang-format.new` 仍未跟踪、未应用，M3 的最终排版提交会改回 `.clang-format`
> 并纳入版本控制。
>
> 本文基于对当前 `znet.h`（约 2.8k 行）和 `notes/*.md` 的逐条复核。文档不是金科玉律；
> 下表中标“本次复核新增”的条目是文档没有写、但代码确实存在的问题。
> 行号以当前 HEAD 为准，修改后可能漂移，定位时以函数名为准。

## 0. 结论先行

| 优先级 | 内容 | 理由 |
| --- | --- | --- |
| **P0** | 单测/回归基线；会崩溃、挂死、double close、UAF、错误返回的 bug | 没有测试和 CI，后面所有改动都不可验证；这些 bug 会直接破坏正确性或让进程卡死 |
| **P1** | 其余正确性、资源泄漏、平台差异；Clean C、MSVC `/W4`、签名 <80 重构 | 影响可靠性或硬性构建约束；应在发布前修完 |
| **P2** | 全量 `clang-format`；少量缓冲区/文档健壮性问题 | 纯质量与可维护性；必须在功能改动稳定后单独提交，避免淹没语义 diff |
| **P3** | timer 四叉堆（4-ary heap）；示例/文档/过时测试清理 | 性能优化和收尾；当前二叉堆在常规规模下可用，必须先有基准和测试 |

**建议执行顺序**：P0 测试基线 → P0 高危修复 → P1 正确性/Clean C/MSVC → P1 签名重构 →
P2 格式化 → P3 timer 四叉堆 → P3 文档/示例收尾。  
注意：P2 的“优先级”低于 P1，但**执行位置在 timer 重构之前**，因为 timer 是结构性改动，
在已格式化的基线上做，diff 才可读。

---

## 1. 当前基线（已实际验证）

- 本机（macOS）用 kqueue 后端构建并运行 `example.c`：通过，退出码 0。
- 用 `-DZN_USE_SELECT` 构建并运行 `example.c`：通过，退出码 0。
- `test.c` 能用统一选项构建，只有 7 个 `%p` 类型警告。
- `znet.h` 本身在统一选项下无警告。
- `bench/*.c` 已移植到当前 API（`packet = 0`），六个程序都能编译并在本机跑起来。
- `zn_deque.h` 不能编译：`zn_popfront`/`zn_popback` 在 `znD_frontU`/`znD_backU` 定义之前
  使用它们。
- `tests/` + `tests/justfile` 是回归套件；`just check` 在本机 macOS（kqueue + select）
  与 `orb just check` 的 Linux（epoll + select）全绿。
- 没有 CI；构建入口是根 `justfile`（`GNUmakefile` 已删除），`just cov` 在本机与
  `orb` 上都能出报告。
- `.clang-format.new` 未跟踪、未应用；它由 `.clang-format` 临时改名而来，当前配置
  `AlignAfterOpenBracket: AlwaysBreak` 会把签名折成 `fn(\n        params)` 的形式，
  不符合本项目的列对齐风格。M3 之前不要用它排版。
- `znet.h`、`zn_buffer.h` 已通过 `-std=c89 -pedantic-errors` 和
  `-std=c++03 -pedantic-errors`；`example.c`/`test.c` 仍有 C4 的混合声明和 `void*` 转换。
  `ZN_USE_64BIT_TIMER` 不是默认 Clean C。
- `znet.h` 有 17 行超过 80 列，最长的是 `zn_connect` 定义（114 列）；多个函数签名已经
  折行，按新约束需要重构而不是继续折行（C3）。
- MSVC `/W4` 未在本机验证；clang `-Wconversion -Wsign-conversion` 近似得到 `znet.h`
  17 条、`example.c` 9 条警告（含收窄和 signedness，需按 MSVC 实际 warning 分类，C2）。
- IOCP/Windows 无法在本机构建；epoll 也无法在本机运行。相关条目标为“待 CI/对应平台
  验证”，不能声称已验证。

已消除、不要重新引入的问题：

- kqueue 的 `znP_poll` 在结果队列有积压时必须返回 `1`（`znet.h:2762`）。之前的本地
  `return 0` 已经还原；回归测试要覆盖“结果队列 >100 批”的场景。

---

### 1.1 2026-10-28 修复状态

已修（macOS kqueue+select 与 Linux orb epoll+select 的全量套件验证；IOCP 仅代码审查）：
L1、L2、L3、L4、L5、T1、B1、B2、B5、U2、U3、U5，以及 C1 的枚举尾逗号。
另有 TODO 未单列的修复：`zn_newtimer` 在 close 开始后返回 NULL；`zn_connect` 先查
`connect_handler` 再查 socket（重复 connect 返回 `ZN_EBUSY`）；select 的 `nfds` 计入
post socketpair；select 的 connect 完成用 `SO_ERROR` 判断被拒绝的连接。

L2 的修法见 `notes/l2-latch.md`：close 直接取消“已 park（`can_* == 0`）”的方向，
不再插入合成结果；同时修正 `zn_onevent`/`zn_onconnect` 中“写 latch 却不产生完成”的
两处，使 `can_* == 1 && handler != NULL` 重新等价于“结果已排队”。

同一天还做了仓库结构整理：

- 根 `justfile` 成为唯一入口，`mod tests` / `mod bench` 把两个子目录导入成模块
  （`just check`、`just tests check`、`just bench echo 5` 都成立）；`GNUmakefile` 删除。
- 测试源码从 `tests/check/` 移到 `tests/`（`tests/<topic>_test.c`）。
- 基准程序从 `tests/` 移到 `bench/`，并配了自己的 `justfile`；`run_*.bat`/
  `build_bench.bat` 删除。六个基准都移植到当前 API（`packet = 0`），其中
  `bench_echo_buffer` 补了帧格式（header 处理函数必须返回整帧长度）。
- 覆盖率：znet.h 91%（行）/ 100%（函数）/ 73%（分支），见 `just cov`。

仍待修：本表其余条目。
行内标“（已修）”的位置列代表该条已关闭，缺陷描述保留作历史上下文。

## 2. 缺陷清单

优先级说明：P0 = 崩溃/挂死/UB/错误 API 结果；P1 = 正确性或资源泄漏；P2 = 健壮性/
可维护性；P3 = 清理项。  
“来源”列中 `AGENTS`、`backends`、`lifecycle`、`timers`、`buffers`、`utils` 分别指
`AGENTS.md` 和 `notes/` 下对应文件；“新增”指本次复核发现。

### 2.1 生命周期、关闭、回调

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| L1 | P0 | `znet.h:2068`（已修） | select 的 `znP_dispatch` 直接解引用 `S->infos[fd]`，不判空。回调里关闭另一个 fd 后，同一批 `select` 事件再 dispatch 该 fd 会空指针崩溃。 | lifecycle 回调顺序 #5 |
| L2 | P0 | `znet.h:2247`（已修） | reactive 的 `znP_closetcp` 只为“阻塞的 recv”插入合成 `ZN_ECLOSE` 结果，也就是用 `can_*` 猜“这个方向是否已有排队结果”；只阻塞 send、recv 也有排队结果、或 `EV_EOF`/`EPOLLHUP` 落在另一个 filter 上把 `can_*` 置 1 时，send 的 retain 永不释放，`waitings` 到不了 0 → `ZN_RUN_LOOP` 不退出，`zn_deltcp` 也不回收。修法：close 直接取消“park 且无排队结果”的方向（release + 清 handler/buffer），并让 `zn_onevent`/`zn_onconnect` 不再“写 latch 却不产生完成”。见 `notes/l2-latch.md`。 | backends（原文过于乐观）+ 新增 |
| L3 | P0 | `znet.h:1555,2008,2386`（已修） | UDP close 不把 `udp->socket` 置为 `ZN_INVALID_SOCKET`；`zn_closeudp()` 后再 `zn_deludp()`（或 `zn_close` 清理）会 double close，可能关掉已复用的 fd；关闭后的 `zn_sendto` 仍能通过 `ZN_ESTATE` 守卫（`znet.h:725`）并在死 fd 上返回 `ZN_OK`。 | backends |
| L4 | P0 | `znet.h:1555`（已修） | IOCP 的 `znP_closeudp` 既不清 `recv_handler` 也不置 `INVALID`；待处理的 `WSARecvFrom` 完成事件无法识别对象已关闭，在 `zn_close` 清理期间仍会调用用户回调。 | backends |
| L5 | P0 | `znet.h:604`（已修） | `zn_accept` 没有 `ZN_EBUSY` 检查；重复发起会泄漏一次 `zn_retain` 并丢掉旧 handler，`waitings` 多 1，`ZN_RUN_LOOP` 无法退出（只 `zn_deltcp` 时对象也不会被回收）。 | AGENTS 不变量 #2 |
| L6 | P1 | `znet.h:1966,2026` | select 的 `zn_onaccept` 每个事件最多接受一个连接；遇到虚假就绪（`accept` → `EAGAIN`）时清 handler、注销 fd、不回调，监听器静默死亡。`zn_onrecvfrom` 遇到 `EAGAIN` 同样清 handler、注销、不回调，UDP 接收静默死亡。 | backends + 新增 |
| L7 | P1 | `znet.h:2473,2669,2500,2696` | epoll/kqueue 的 `zn_onaccept`/`zn_onrecvfrom` 在 `EAGAIN` 时也清 handler、直接返回，没有回调；监听器/UDP 接收进入“无 handler”状态。监听器和 UDP 都是水平触发注册，后续事件可能空转（select 则是静默死亡）。 | 新增 |
| L8 | P0 | `znet.h:2530,2726` | epoll/kqueue 的事件批保存 `zn_SocketInfo *` 裸指针；回调里删除另一个对象后，同一批后续事件会拿到悬空指针（UAF）。kqueue 的 `znP_dispatch` 还不判 `info` 是否为 NULL。 | lifecycle 回调顺序 #5 |
| L9 | P1 | `znet.h:1337,1528,1538` | IOCP 的 `znP_connect` 在 `zn_getextension` 失败时不关闭 socket；`zn_onaccept` 不检查 `zn_newtcp` 是否返回 NULL（可能崩溃），`GetAcceptExSockaddrs` 失败时泄漏 client，完成端口关联失败时静默丢弃 accepted socket 且不回调。 | 新增 |
| L10 | P2 | `znet.h:316` | mempool 的 `assert` 在 `NDEBUG` 下消失；非法 `size` 会静默破坏内存。 | lifecycle |
| L11 | P2 | `zn_addrinfo.h:146` | 不在 `zn_close` 前调用 `zn_closeaddrinfo` 时，迟到的 post 可能触碰已释放 state；`znA_clearreq` 忽略 `zn_post` 失败，请求和 retain 会泄漏。 | utils |
| L13 | P3 | `znet.h:256` | `znL_empty(h)` 展开成 `(h)->prev == &(h)`：无论传入头指针还是头变量都编不过（对右值取地址 / 对非指针取成员）。当前无人使用，所以一直没暴露；要用先改成 `(void*)(h)->next == (void*)(h)`。 | 新增（写 tcp_close_* 测试时发现） |

### 2.2 后端与事件循环

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| B1 | P0 | `znet.h:2407`（已修） | epoll 的 `znP_signal` 把 `eventfd_write` 的返回值判断反了：成功时返回 0，当前代码却返回 `ZN_ERROR`。结果是 `zn_post` 在 epoll 上成功投递也报告失败。 | 新增 |
| B2 | P1 | `znet.h:2714`（已修） | kqueue 的 `zn_onrecvfrom` 用 `sizeof(&remote_addr)`（指针大小，8 字节）作为地址缓冲区长度；IPv6 对端地址被截断。 | backends |
| B3 | P1 | `znet.h:1582,2015,2393` | 四个后端的 `znP_sendto` 都忽略 `sendto` 返回值；UDP 发送失败不可见，仍返回 `ZN_OK`。 | backends |
| B4 | P1 | `znet.h:724` | `zn_sendto` 无条件拒绝 `len > 1200`，与路径 MTU 无关。 | backends |
| B5 | P1 | `znet.h:1456`（已修） | IOCP 的 `znP_listen` 对仍是 `INVALID_SOCKET` 的 `accept->socket` 调 `znU_set_reuseaddr`；监听 socket 实际从未设置 `SO_REUSEADDR`。 | backends |
| B6 | P1 | `znet.h:2560` | epoll 把 `zn_Time` 超时直接转成 `int`；超过 `INT_MAX` 毫秒（约 24.8 天）会变成负数，`epoll_wait` 将其视为无限等待，长定时器失准。 | 新增 |
| B7 | P1 | `znet.h:1995,2371` | select/reactive 的 `znP_initudp` 没有把 UDP socket 设为非阻塞；`recvfrom`/`sendto` 可能阻塞整个事件循环。 | 新增 |
| B8 | P2 | `znet.h:2352` | reactive 的 `znP_listen` 先注册 fd 再 bind/listen；失败时不注销，留下 stale `S->infos[fd]`/epoll 注册。 | 新增 |
| B9 | P2 | `znet.h:1872,1898` | select 的 `zn_onsend`/`zn_onrecv` 在就绪后遇到 `EAGAIN` 会清 handler、release，却不回调，操作被静默丢弃。 | 新增 |
| B10 | P3 | `znet.h:2109` | select 的 `znP_poll` 在 `EINTR` 后继续遍历未定义的 `fd_set` 内容。 | 新增 |
| B11 | P2 | `znet.h:1600` | IOCP 把 0 字节 UDP 数据报（`bSuccess && dwBytes == 0`）当成错误。 | 新增 |
| B12 | P3 | `znet.h:2423` | epoll 的 `znF_unregister` 把未初始化的 `struct epoll_event` 传给 `EPOLL_CTL_DEL`；当前内核只读 `events`，属于脆弱点而非 bug。 | backends |

### 2.3 定时器

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| T1 | P1 | `znet.h:390`（已修） | handler 返回值存进 `int`，截断 `zn_Time`；`ZN_USE_64BIT_TIMER` 或重复延迟超过 2^31 ms 时，重新发起的延迟静默错误。 | timers |
| T2 | P1 | `znet.h:43` | 默认 32 位 `zn_Time` 在进程运行约 49.7 天后回绕；所有比较都是普通 `>`/`<=`。长寿命服务器需要 64 位，但改类型对 DLL/`.so`/`znet.hpp` 是 ABI 变更。 | timers |
| T3 | P2 | `znet.h:398` | 空堆时 `nexttime == ZN_FOREVER`，`znT_gettimeout` 返回 `ZN_FOREVER - now`，并不等于 `ZN_FOREVER`；kqueue/select 会阻塞约 49.7 天而不是“永久”，后端语义不一致。 | timers + 新增 |
| T4 | P2 | `znet.h:451` | 取消根节点时不更新 `nexttime`，会造成提前唤醒；无害但浪费 CPU。 | timers |
| T5 | P2 | `znet.h:57` | 定时器堆只增长、不收缩；`ZN_MAX_TIMERPOOL` 声明未使用；没有数量上限。 | timers |
| T6 | P2 | `znet.h:443` | `start + delayms` 没有溢出检查；回绕后 `emit` 顺序错误。 | timers |
| T7 | P3 | `znet.h:390` | 相同 deadline 不保证 FIFO（严格 `>` 比较）；要么文档化，要么改成稳定顺序。 | timers |
| T8 | P3 | `znet.h:358` | `zn_MemPool timers;` 行末有多余反斜杠，是格式化残留。 | 新增 |

### 2.4 缓冲区、工具、Lua

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| U1 | P1 | `zn_deque.h:302,245-261,279` | 不能编译（`znD_frontU`/`znD_backU` 先用后定义）；POSIX `zn_deldeque` 二次 unlock 一个未加锁的 mutex；`pthread_cond_timedwait` 收到的是相对 `timespec`，正的 `waitms` 会立即超时。 | utils |
| U2 | P1 | `zn_buffer.h:175,185`（已修） | `znBC_upperbound`/`znBC_lowerbound` 把档位索引钳到 `ZN_BUFFCACHE_MAXLSIZE`（31），而数组实际只有 0..17；超过 2 GiB 的分配会越界访问 `buffs[]`。 | buffers |
| U3 | P1 | `zn_buffer.h:302`（已修） | `zn_addlstring`/`zn_addchar` 不检查 `zn_prepbuffsize` 返回 NULL；OOM 时对 NULL `memcpy` 或越界写。 | 新增 |
| U4 | P1 | `zn_buffer.h:324,331` | `zn_initrecvbuffer`/`zn_resetrecvbuffer` 给 `packet_handler` 赋值两次，第二次是 `NULL`；默认行为变成“完整包被丢弃”，`znRB_onpacket` 是死代码，`packet_ud` 未初始化。 | buffers |
| U5 | P2 | `zn_buffer.h:336`（已修） | header handler 返回 0 时缓冲下来的字节不会在下一次调用传给 header handler；跨读的长度前缀 header 不可靠。 | buffers |
| U6 | P2 | `zn_buffer.h:222` | `zn_prepbuffcache` 的 `ret` 从未赋值，成功也返回 0。 | 新增 |
| U7 | P2 | `tests/zn_bufferpool.h` | `zn_putbuffer` 回收节点时没有 `zn_resetrecvbuffer`/`zn_resetsendbuffer`；下一个连接继承旧分帧状态。 | buffers |
| U8 | P2 | `znet.hpp` | `TcpAccept` 把 client 存在 accept 对象里，连续 accept 会修改同一个 `TcpSocket`；handler 运行一次即被消费，容易误用。 | utils |
| U9 | P1 | `zn_lua.c:73` | `lzn_ontimer` 在 `lua_isinteger` 分支里、共享 `lua_pop` 之前 return；每个 tick 泄漏一个 Lua 栈槽。 | utils |
| U10 | P1 | `zn_lua.c:441` | `lzn_onaccept` 只在 Lua 回调返回真值时重新 `zn_accept`；否则监听器处于“已发起但无 handler”状态，水平触发下可能空转。 | utils |
| U11 | P2 | `zn_task.h` | `zn_deltaskpool` 遇到永久阻塞的任务会永久阻塞；需要文档化或提供超时/取消语义。 | utils |
| U12 | P3 | `bench/*.c`（已修） | 基准/echo 程序按旧 API 编写，`make all` 必然失败。已移到 `bench/` 并移植（`packet = 0`），构建与运行改由 `bench/justfile` 负责。 | AGENTS |
| U13 | P3 | `example.c:191` | 打印 `"%sn"`，应为 `"%s\n"`。 | AGENTS |
| U14 | P3 | `example.c, test.c` | 30 个 `%p` 配类型化指针的 `-Wformat-pedantic` 警告；新代码用 `(void*)p`。 | AGENTS |

### 2.5 格式、构建、文档

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| F1 | P2 | `.clang-format.new` | 由 `.clang-format` 临时改名而来；未跟踪、未应用。`AlignAfterOpenBracket: AlwaysBreak` 会产生难看的折行；`IncludeBlocks: Regroup`/`SortIncludes` 可能重排 `znet.h` 的 include，需先审查。签名“不换行”还需要额外的检查脚本，不能只靠 clang-format。M3 最终排版提交时改回 `.clang-format`。 | AGENTS + 新增约束 |
| F3 | P3 | `AGENTS.md, notes/*` | 部分行号已漂移；kqueue 的“工作区未提交修改”说明已过期（本次翻译已修正）；修 bug 后需再校对一遍。 | 新增 |
| F4 | P2 | `justfile`（已修） | 构建入口分散（`GNUmakefile` + `tests/justfile`），没有 `format` 目标，也没有 CI。现在只有根 `justfile`（模块化 `tests/`、`bench/`），`GNUmakefile` 已删除；CI 仍缺。 | AGENTS |

### 2.6 Clean C、MSVC 与签名约束（新增硬约束）

| ID | P | 位置 | 缺陷与影响 | 来源 |
| --- | --- | --- | --- | --- |
| C1 | P1 | `znet.h:899`、`znet.h:45`（枚举部分已修） | 枚举尾逗号已删，`znet.h` 在 `-std=c89/-std=c++03 -pedantic-errors` 下通过。`ZN_USE_64BIT_TIMER` 仍依赖 `unsigned long long`，默认配置不启用。 | 新增约束 |
| C2 | P1 | `znet.h:2264,2279,2309,2326,2655,2664,2691,2715,2720,2722,2731` | MSVC `/W4` 风险要分类：收窄转换（`ssize_t`→`int`、`size_t`→`unsigned`、`uintptr_t`→`int` 等）需要显式 cast + 值域检查；同宽 signedness（`int`↔`unsigned`、`%d` 配 `unsigned`）优先改类型/比较/printf 格式，`%p` 配 `(void*)`，不要机械全量加 cast。clang `-Wconversion -Wsign-conversion` 近似给出 `znet.h` 17 条、`example.c` 9 条；MSVC 实际清单需 Windows CI。 | 新增约束 |
| C3 | P1 | `znet.h:103,107,417,486,640,660,678,722,730,791,1582,2015,2393` | 签名 <80 且不换行：`zn_connect` 114 列、`zn_send` 95、`zn_sendto` 100、`zn_recvfrom` 97、`zn_recv` 89、`zn_closeaccept` 81；`zn_AcceptHandler` 89、`zn_RecvFromHandler` 119；内部 `znT_newtimer`/`znU_ntop` 81、`znP_sendto` 87/85。`zn_connect` 即使参数名缩到 1 个字母仍 >80，必须改 API（参数结构体/去掉 `packet`/新入口），不能只折行。 | 新增约束 |
| C4 | P1 | `example.c:201`、`test.c:25,31,34,47,66,127`、`zn_task.h:281`、`zn_buffer.h:257`、`zn_lua.c` | `znet.h` 之外的 Clean C 问题：`example.c`/`test.c` 的 C89 混合声明；`test.c` 的 `void*`→`char*`、`zn_send` 的 `void*` 实参；`zn_task.h` 的 typedef 重定义（C11 特性）；`zn_buffer.h` 的 `void*`→`char*`；`zn_lua.c` 尚未审计（依赖 Lua 头）。 | 新增约束 |

---

## 3. 六个改进项的优先级

| 用户项 | 优先级 | 放在哪个里程碑 | 说明 |
| --- | --- | --- | --- |
| 1. 单测套件 | **P0** | M0 | 没有它，后面每一项都无法证明没坏。先做最小冒烟 + 回归，再补单元/集成。 |
| 2. timer 改为四叉树 | **P3** | M4 | 当前二叉堆在常规规模可用；四叉堆只是常数优化。先修 T1/T2 等语义 bug，再拿基准决定是否值得换。 |
| 3. 跑 clang-format | **P2** | M3 | 必须等 P0/P1 功能修复稳定后，单独提交；否则一千行格式 diff 会淹没真正的 bug 修复。当前配置名是 `.clang-format.new`，M3 最终排版提交时改回 `.clang-format`。 |
| 4. 修复函数签名折行 | **P1** | M3 | 新约束要求“单行 <80 且不能折行”；`zn_connect` 等需要 API 重构，不是加换行。 |
| 5. 修复当前可能有的 bug | **P0/P1** | M1/M2 | 按严重度拆：崩溃/挂死/UAF 是 P0；错误结果/泄漏/平台差异是 P1。 |
| 6. 其他文档缺陷修复 | **P0–P3** | 分散 | 文档里已有的缺陷按上表优先级处理；纯文档/示例/过时测试放 P3。 |

新增的 **C1/C4（Clean C）、C2（MSVC /W4 收窄 cast）、C3（签名 <80 不换行）都是 P1 硬约束**；
其中 C3 与第 4 项是同一件事：先重构签名（语义/API 改动），再做全量格式化（P2）。
C1/C2/C4 在 M2 完成。

### 3.1 为什么 timer 四叉堆排最后

- 四叉堆（4-ary heap）相对二叉堆只改善常数因子和缓存局部性，不改变 O(log n)。
- 当前 timer 代码还有 **语义** 问题（T1 的 `int` 截断、T2 的 32 位回绕、T3 的空堆哨兵），
  这些必须先修；在错误语义上换数据结构，测试很难判断是“堆写错了”还是“语义本来就错”。
- 没有 timer 基准之前，无法证明四叉堆有收益。先加基准，再决定是否保留。
- 术语提醒：这里应理解为“四叉堆/4-ary heap”，不是空间四叉树（quadtree）。定时器只有
  一个 deadline 维度，不需要二维空间索引。如果确实指空间四叉树，需要先说明索引维度，
  否则不建议。

### 3.2 为什么格式化排在功能修复和签名重构之后、timer 重构之前

- 格式化会重写约 1000 行，`git blame` 会失效；如果和 bug 修复混在一起，review 无法分辨
  语义改动。
- 签名重构会改公共 API（尤其 `zn_connect`），是语义改动，必须先于全量格式化；否则
  clang-format 会把旧签名折成多行，违反新约束。
- timer 四叉堆会大改 `znT_*`；在签名稳定、格式化后的基线上改，diff 更干净。
- 因此执行顺序是：**先修正确性 → 重构签名 → 在冻结点单独格式化 → 再做 timer 结构
  改动**。优先级标签仍是 P1/P2/P3，不要误解为“先做 P3 再做 P1”。

---

## 4. 里程碑与具体步骤

### M0 — 测试与回归基线（P0，先做）

目标：一条命令能在本机跑完“构建 + 冒烟 + 关键回归”；后续每个 bug 修复都先有失败用例。

步骤：

1. **保留现有冒烟**：把 `example.c` / `test.c` 的构建和运行包装成 `just smoke`；
   同时支持 `ZN_USE_SELECT=1`。
2. **新建测试目录**：建议 `tests/check/`（与过时的基准程序分开），用无依赖、C89/C++03
   都能编译的 `assert`/`CHECK` 宏；`GNUmakefile` 增加 `check`、`check-select`、
   `check-asan`、`check-64bit-timer`、`check-c89`、`check-cpp03`、`check-msvc` 目标，
   `all` 不再构建过时的 `tests/*`。
3. **单元测试**：
   - mempool：分配/归还/整页释放；`ZN_MAX_PAGESIZE` 边界。
   - timer：顺序、取消根/中间/末尾、handler 内重挂、handler 内删除、0 延迟、64 位延迟。
   - result queue：超过 100 批的级联、close 时仍有排队结果。
   - addr：IPv4/IPv6 解析与格式化、非法输入。
   - buffer：尺寸档位边界（不实际分配 >2 GiB，用内部 helper 或失败路径）、分帧拆包、
     发送队列部分写/重置。
4. **集成测试（loopback）**：
   - TCP connect/accept/send/recv/close；close 时仍有 pending send/recv/accept/connect。
   - UDP sendto/recvfrom/close；double close；关闭后 sendto。
   - timer close；`zn_post` 跨线程；`zn_addrinfo("localhost")`。
   - `zn_accept` 重复发起；UDP 重复 close。
5. **后端与语言矩阵**：
   - macOS：kqueue + `-DZN_USE_SELECT`；同时用 `-std=c89` 和 `-std=c++03` 各编译一遍。
   - Linux：epoll + `-DZN_USE_SELECT`；同样做 C89/C++03 双编译。
   - Windows：IOCP（CI 或真机；本机不能声称验证），另加 `cl /W4 /WX` 的 C 与 C++ 编译。
6. **Sanitizer**：ASan + UBSan 跑单元和集成；`-DZN_USE_64BIT_TIMER` 再跑一遍 timer；
   `-DNDEBUG` 跑一遍冒烟。
7. **回归优先**：M1/M2 的每个 P0/P1 条目先写一个会失败的测试，再修；测试名带条目 ID
   （如 `test_close_blocked_send`、`test_udp_double_close`）。

验收：

- `just check` 在 macOS 上通过（kqueue + select）。
- `just check-c89` / `just check-cpp03` 通过；`znet.h` 和示例在严格 C89/C++03 下无错误。
- ASan/UBSan 无报告。
- 至少有一个测试能在修复前稳定复现 L2（`waitings` 不归零、`ZN_RUN_LOOP` 不退出）和 L3（UDP double close）。
- CI 或本地脚本能覆盖 Linux/Windows 的构建与 MSVC `/W4`；没有环境时明确标注“未验证”。

### M1 — P0 高危修复

按以下顺序做，每个都配回归测试：

1. **B1 epoll `znP_signal` 返回值反转**（已修）：改成
   `eventfd_write(...) == 0 ? ZN_OK : ZN_ERROR`；在 Linux 上断言 `zn_post` 返回 `ZN_OK`。
2. **L3 UDP close 幂等**（已修）：四个后端的 `znP_closeudp` 都置
   `udp->socket = ZN_INVALID_SOCKET`，按后端释放/保留 `recv_handler`；`zn_closeudp` 第二次
   调用直接返回 `ZN_OK`；`zn_sendto` 的 `ZN_ESTATE` 守卫随之生效。测试：close 后 del、
   close 两次、关闭后 sendto、fd 复用。
3. **L4 IOCP closeudp**（已修）：置 `INVALID`，但保留 `recv_handler` 直到完成事件到达；
   `zn_onrecvfrom` 看到 `INVALID` 时只 `ZN_PUTOBJECT`，不调用用户回调；`zn_deludp` 延迟
   回收。测试：pending `WSARecvFrom` + `zn_close`，确认用户回调不运行、ASan 干净。
4. **L2 reactive close 记账**（已修）：`znP_closetcp` 直接取消“park 且无排队结果”的
   方向（release + 清 handler/buffer），不再插入合成结果；同时把 `zn_onevent`/
   `zn_onconnect` 改成“边沿到达就重试 park 的方向，没人 park 才把 `can_*` 记成 1”，
   使 `can_* == 1 && handler != NULL` 重新等价于“结果已排队”。不需要新增 `queued`
   位：一旦 latch 不再被凭空置 1，`can_*` 本身就是那个位。测试：`tcp_close_blocked_send`
   （真实 socket）、`tcp_close_parked_and_queued`、`tcp_close_both_parked`（白盒）。
   细节与对 Design D 的评审见 `notes/l2-latch.md`。
5. **L5 `zn_accept` EBUSY**（已修）：在 `znP_accept` 之前检查 `accept->accept_handler != NULL`；
   测试重复发起返回 `ZN_EBUSY`，`waitings` 不泄漏。
6. **L1 select dispatch 判空**（已修）：`info == NULL` 时继续；测试在回调里关闭另一个有事件的
   fd，确认不崩溃。
7. **L8 事件批 UAF**：这是结构性修复。建议在 `zn_State` 增加“待释放对象”队列，
   `ZN_PUTOBJECT` 只入队，`znP_poll` 在一批事件全部 dispatch 完之后统一 flush；或者
   在 epoll/kqueue 的 dispatch 中引入对象代际/活跃检查。先写一个“回调删除另一个对象”
   的失败用例，再选方案。不要用“只加 NULL 检查”糊弄，因为内存可能已被复用。
8. **IOCP 资源泄漏（L9）**：`zn_getextension` 失败时 `closesocket`；`zn_onaccept` 检查
   `zn_newtcp` 返回值，失败时关闭 client 并向 accept 回调报错；`GetAcceptExSockaddrs`
   失败时关闭 client、清 `accept->client`。

验收：

- 上述测试全部通过；ASan/UBSan 干净。
- `zn_close`/`zn_run(ZN_RUN_LOOP)` 在“仍有 pending send/recv/accept/connect”时都能结束。
- UDP 双重 close、关闭后 sendto、IOCP pending recv 不再触发用户回调。
- 不改变公共 API；若必须改，先记录理由并更新 `AGENTS.md`/`README.txt`。

### M2 — P1 正确性与平台修复

先完成语言/编译器硬约束（P1）：

1. **C1 `znet.h` Clean C**：去掉 `znet.h:899` 的枚举尾逗号；增加 `-std=c89` /
   `-std=c++03` 构建目标并修到严格编译通过。`ZN_USE_64BIT_TIMER` 仍用
   `unsigned long long`，默认配置不启用；如果要求该选项也 Clean C，再改用可移植的
   64 位表示（见第 7 节）。`znet.hpp` 保持 C++11，不纳入本项。
2. **C4 其余文件的 Clean C**：`example.c`/`test.c` 的声明移到块首；`test.c` 的
   `void*`→`char*` 和 `zn_send` 的 `void*` 实参加显式 cast；`zn_task.h:281` 去掉
   C11 式 typedef 重定义；`zn_buffer.h:257` 的 `void*`→`char*` 加 cast；审计
   `zn_lua.c`（依赖 Lua 头，本机未安装）。
3. **C2 MSVC `/W4` 收窄 cast**：先按 MSVC 实际警告分类（C4244/C4267 收窄，
   C4245/C4018/C4389 signedness）。只对收窄点（`ssize_t`→`int`、`size_t`→`unsigned`、
   `uintptr_t`→`int`、timer 的 `zn_Time`→`int` 等）加显式 cast + 值域检查；同宽
   signedness 优先改类型/比较方式，`%p` 改 `(void*)`，`%d` 改 `%u` 或先确认范围，不要
   机械全量加 cast。加 Windows CI 的 `cl /W4 /WX`；本机不能验证时只写“未在 MSVC
   验证”，不要用 `/wd` 静默。

然后按“先跨平台共性、后单后端”修功能：

1. **L6/L7 虚假 EAGAIN**：决定语义（自动重挂，还是回调 `ZN_ERROR`）。推荐在
   `accept`/`recvfrom` 的 `EAGAIN` 分支自动重挂（重新注册 + 重新 retain），因为
   `EAGAIN` 表示“这次就绪是假的”，不是致命错误。若选择回调错误，必须保证 handler
   不会被清空后无人负责。测试需要能注入一次 `EAGAIN`（可用非阻塞 fd + 先消费数据模拟）。
2. **B2 kqueue IPv6**：`addr_size = sizeof(remote_addr)`；加 IPv6 UDP loopback 测试，
   断言返回的地址不是截断的。
3. **B5 IOCP reuseaddr**：把 `znU_set_reuseaddr(accept->socket)` 改成
   `znU_set_reuseaddr(socket)`；Windows CI/真机验证。
4. **B6 epoll 超时**：把 `int ms` 改为 `int ms = timeout > INT_MAX ? INT_MAX :
   (int)timeout;`，或者用 `epoll_pwait2` 的 `timespec`（若可移植性允许）。加单元测试：
   构造 >INT_MAX 的超时，确认不会变成无限等待。
5. **B7 UDP 非阻塞**：select/reactive 的 `znP_initudp` 调 `znU_set_nonblock(fd)`；加
   “UDP 接收不阻塞循环”的测试。
6. **B3/B4 UDP 发送**：`znP_sendto` 返回 `sendto` 的结果（映射 `EAGAIN`/错误），
   `zn_sendto` 的 1200 限制改为由调用者/MTU 决定（至少不要硬编码；可保留一个
   `ZN_MAX_UDP_PAYLOAD` 常量并文档化）。测试：关闭 fd 后 sendto 返回错误而不是 OK。
7. **T1/T2 timer 语义**：`zn_Time ret`；补 64 位 timer 测试；在文档中明确 32 位回绕和
   ABI 影响，不擅自把默认类型改成 64 位。
8. **U1 deque**：把 `znD_frontU`/`znD_backU` 移到 `zn_popfront`/`zn_popback` 之前；
   POSIX `zn_deldeque` 只 unlock 一次；`pthread_cond_timedwait` 用
   `clock_gettime(CLOCK_REALTIME)` + `waitms` 计算绝对 deadline。加编译测试。
9. **U2/U3/U4 buffer**：档位索引钳到 `ZN_BUFFCACHE_COUNT` 并拒绝超范围；`zn_addlstring`/
   `zn_addchar` 处理 NULL；明确 `packet_handler` 默认值（建议默认丢弃就删掉死代码
   `znRB_onpacket`，默认收包就修成 `znRB_onpacket`，二选一并写测试）。
10. **U9/U10 Lua**：整数分支先 `lua_pop` 再 return；`lzn_onaccept` 明确“不返回真值就
    不重挂”的语义，并在文档/示例里写清楚，或者默认重挂。加 Lua 栈高度回归测试。

验收：

- 对应后端测试通过；本机不能跑的标“待 CI”。
- `znet.h` 和示例在严格 C89/C++03 下编译通过（C1）。
- Windows CI 的 `cl /W4 /WX` 无转换警告（C2）；本机未验证时不得声称通过。
- timer 测试覆盖 32/64 位、0 延迟、取消根、handler 内重挂。
- `zn_deque.h` 能单独编译；buffer 边界测试不越界。
- C89/C++03 下不引入新的 `-Wall -Wextra -pedantic` 警告。

### M3 — 签名重构 + 全量 clang-format（P1/P2）

前置：M2 完成、测试绿；工作区干净；签名重构和格式化分成两个独立提交。

步骤：

1. **先定格式策略**，不要直接跑全量：
   - 当前配置暂名 `.clang-format.new`，`clang-format --style=file` 不会自动发现它。
     M3 开始真正格式化时先改回 `.clang-format`；这个改名动作与格式化一起放在最终排版
     提交里（M3 之前保持 `.new`）。若要在改名之前试跑，用
     `--style=file:.clang-format.new`，不要退回默认 style。
   - `AlignAfterOpenBracket` 从 `AlwaysBreak` 改为 `Align`，否则会出现
     `fn(\n        params)`。
   - 审查 `IncludeBlocks: Regroup` / `SortIncludes`：`znet.h` 的 include 顺序有语义
     （平台/后端段），建议改成 `SortIncludes: Never`，或对 include 区加
     `// clang-format off`。
   - 决定公共 API 的列对齐是否保留：`clang-format` 会去掉 `zn_connect (` 里的空格。
     二选一：接受 formatter 风格并更新 `AGENTS.md` 的约定；或用
     `// clang-format off` 包住公共声明区。
   - 必须保留 `ColumnLimit: 80`、`IndentWidth: 4`、`UseTab: Never`。但 clang-format 只能
     在超长时折行，不能替你保证“签名不换行”；额外检查见第 3 步。
2. **签名重构（P1，语义/API 改动，先做）**。规则：整行（含返回类型和 `ZN_API`）
   严格 <80 且只有一行。当前必须处理：
   - `zn_connect`（114 列）：参数名缩短也放不下。二选一：
     - 引入 `zn_ConnectParam` 之类的参数结构体；
     - 把 `packet` 移到 `zn_newtcp`/新增 `zn_connectex`，保留旧入口做兼容包装。
     这是公共 API 改动，必须同步 `example.c`、`test.c`、`znet.hpp`、README 和本文件。
   - `zn_send`（95）、`zn_sendto`（100）、`zn_recvfrom`（97）：用更短的形参名、内部
     typedef 别名或参数结构体压到 <80；`zn_recv`（89）缩参后应能进 80。
   - 回调 typedef：`zn_AcceptHandler`（89）、`zn_RecvFromHandler`（119）缩短形参名或
     引入参数结构体；公共类型名保持不变，避免不必要的源码破坏。
   - 内部函数：`znT_newtimer`（81）改名/缩参；`znU_ntop`（81）缩参；`znP_sendto`
     （87/85）缩参；`zn_closeaccept`（81）去多余空格/缩参。
   - 每改一个签名就编译 + 跑冒烟；签名重构提交必须是独立提交，不能和格式化混在一起。
3. **增加签名检查**：写一个轻量脚本（`scripts/check-signatures.py` 或 C 程序）扫描
   `znet.h`，对每个函数声明/定义和函数指针 typedef 检查：
   - 签名行长度必须 <80；
   - 如果该行有 `(` 但闭合 `)` 不在同一行，直接失败（说明已经折行）。
   把它接入 `just format-check` 和 CI。这样即使 clang-format 将来把某行折开，也会被抓住。
4. **全量格式化（P2）**，只覆盖维护中的文件：
   `znet.h`、`znet.hpp`、`zn_buffer.h`、`zn_task.h`、`zn_deque.h`、`zn_addrinfo.h`、
   `example.c`、`test.c`。不要格式化 `lbind.h`（第三方）和已过时的 `tests/*`。
5. **格式化提交独立**，提交信息写明“format only”；在 `.git-blame-ignore-revs` 里加入该
   提交，并说明本地 `git config blame.ignoreRevsFile .git-blame-ignore-revs`。
6. 增加 `just format` 和 `just format-check`（`clang-format --dry-run --Werror` 加上第 3 步
   的签名检查）。确认 `.clang-format.new` 已改回 `.clang-format`，并且改名与 `.clang-format`
   一起出现在最终排版提交里。
7. 更新 `AGENTS.md` 的“Formatting”“公共声明列对齐”“函数签名”三节，避免下一个人再手工
   对齐或接受折行。

验收：

- `znet.h` 的每个函数声明/定义（含回调 typedef）都是单行、整行 <80；签名检查脚本通过。
- 签名重构提交的 `just check` 与重构前一致；公共 API 改动已更新示例/README/绑定/笔记。
- 格式化提交的 `git diff -w` 为空（纯空白改动），且 `just check` 与格式化前一致。
- `just format-check` 通过；没有“签名被 clang-format 折行但没人重构”的残留。

### M4 — timer 四叉堆（4-ary heap）

前置：M2 的 T1/T2 已修；M0 的 timer 测试齐全；M3 的签名重构和格式化已完成。

步骤：

1. **基准**：写一个不依赖网络的 timer benchmark，测 `start`/`cancel`/`update` 在
   n = 1k、10k、100k 下的吞吐和延迟；记录二叉堆基线。
2. **实现**：把 `zn_TimerState.heap` 改成 4 叉堆（父 `(i-1)/4`，子 `4*i+1..4*i+4`）；
   `znT_resizeheap` 不变；`zn_starttimer` 的上筛、`zn_canceltimer` 的上筛/下筛都要改，
   并继续维护每个 timer 的 `index`。`nexttime` 的维护规则不变。
3. **正确性**：写一个 reference model（有序数组）做随机 property test：随机
   arm/cancel/re-arm/fire，比较触发顺序和 `nexttime`；覆盖相同 deadline、0 延迟、
   取消根/中间/最后、handler 内重挂。
4. **性能**：只有基准显示明显收益（例如 update/cancel 提升 ≥10% 且无回归）才保留；
   否则回退，并在 `notes/timers.md` 记录“评估过，收益不足”。
5. **文档**：更新 `notes/timers.md` 的数据结构图和复杂度说明。

验收：

- 所有 timer 测试和 property test 通过；`ZN_USE_64BIT_TIMER` 矩阵通过。
- `zn_run(LOOP)` 在只有 timer 的状态下能正确退出。
- 基准报告附在提交/PR 里；没有收益就明确回退。

### M5 — 文档、示例、过时测试收尾（P3）

1. 修 `example.c:191` 的 `"%sn"`；把 `example.c`/`test.c` 的 `%p` 改成 `(void*)p`
   （或至少新代码不再增加警告）。
2. 移植或移除 `tests/*.c`：如果保留基准，传 `packet = 0` 并修 `zn_newaccept` 调用；
   如果不再维护，移到 `tests/legacy/` 并在 `GNUmakefile` 中排除。
3. 修 `zn_deque.h` 或明确标记“不支持”；不要让它保持“看起来能用但编译不过”的状态。
4. 校对 `notes/*` 的行号和“已知损坏”表；把 M1–M4 修掉的条目从“已知损坏”移到
   “已修复/回归测试”。
5. 更新 `README.txt`：补 `just check`、后端矩阵、64 位 timer、UDP 限制，以及 Clean C
   （C89/C++03）、MSVC `/W4`、签名 <80 不换行三条约束。
6. 如果仓库有远端，加 GitHub Actions 矩阵（ubuntu/macos/windows）；没有就提供
   `scripts/check.sh`，并在 README 写明本机验证范围。Windows job 必须包含
   `cl /W4 /WX` 的 C 和 C++ 编译。

验收：

- `just smoke` 不再失败；`just check` 全绿。
- 文档中的“已知损坏”与代码一致；行号抽查通过。
- 新贡献者能按 README 在 macOS/Linux 上跑通测试，并看到 Clean C/MSVC/签名检查入口。

---

## 5. 每个 P0 条目的验收测试（建议命名）

| 条目 | 测试 | 断言 |
| --- | --- | --- |
| L1 | `test_select_close_other_fd_in_callback` | 同一批事件中关闭另一个 fd，进程不崩溃（ASan 干净） |
| L2 | `test_close_blocked_send` | 填满发送缓冲后 `zn_deltcp`，`zn_run(LOOP)` 返回，`waitings == 0` |
| L3 | `test_udp_double_close` / `test_udp_sendto_after_close` | 第二次 close 返回 `ZN_OK`；关闭后 sendto 返回 `ZN_ESTATE`；不误关复用的 fd |
| L4 | `test_iocp_close_udp_pending_recv` | close 后用户回调不再运行；ASan 干净（Windows CI） |
| L5 | `test_accept_twice` | 第二次 `zn_accept` 返回 `ZN_EBUSY`；`waitings` 归零 |
| L8 | `test_epoll_delete_other_object_in_callback` | 同批后续事件不访问已释放内存（ASan 干净） |
| B1 | `test_epoll_post_returns_ok` | `zn_post` 返回 `ZN_OK`，handler 被调用（Linux CI） |

---

## 6. 风险与“不要做”的事

1. **不要先跑全量 `clang-format`**：一千行格式 diff 会让后续 bug 修复无法 review；先修
   正确性，再单独格式化。
2. **不要在 timer 语义 bug 未修时换四叉堆**：否则测试失败时无法区分“堆实现错”还是
   “原本的 `int` 截断/回绕”。
3. **不要为了迁就 `tests/*.c` 改库 API**：过时测试应移植，库不应回退。
4. **不要用“加一个 NULL 检查”解决 L8**：对象内存可能已被复用；要延迟释放或引入
   代际/活跃检查。
5. **不要在没有 Windows/Linux 环境时声称 IOCP/epoll 已验证**：只写“代码审阅 + 待 CI”。
6. **不要擅自把默认 `zn_Time` 改成 64 位**：这是 ABI 变更，需要单独决策和版本说明。
7. **不要把格式化提交和语义提交混在一起**；格式化提交必须能用 `git diff -w` 验证为空。
8. **不要删除 `.clang-format.new`/`.clang-format` 了事**：要么定策略并纳入版本控制，
   要么明确“不格式化”，不能保持“文件在、从没用、风格漂移”的状态。
9. **不要用折行、调大 `ColumnLimit` 或 `// clang-format off` 绕过签名 <80**：折行是
   重构信号，不是最终格式。
10. **不要机械给所有 `size_t`/`ssize_t`/`int`/`unsigned` 转换加 cast**：MSVC `/W4` 只对
    收窄转换强制要求显式 cast；同宽问题优先改类型/格式。也不要用隐式收窄或 `/wd` 静默；
    本机不能验证就明确标注。
11. **不要把 `znet.hpp` 拉进 Clean C**：它是显式 C++11 可选包装；Clean C 只约束
    `znet.h` 和 C 头/源文件。
12. **不要默认打开 `ZN_USE_64BIT_TIMER` 来回避 `long long` 问题**：默认配置必须 Clean C；
    该选项是显式 opt-in。

---

## 7. 待确认的设计取舍

这些不是缺陷，但会影响修复方式，动手前先定：

1. **`zn_accept` 重复发起**：返回 `ZN_EBUSY` 是明确的选择；是否允许“替换 handler”？
   推荐不允许，和 send/recv 保持一致。
2. **虚假 `EAGAIN`**：自动重挂还是回调 `ZN_ERROR`？推荐自动重挂（就绪是假的，不是
   致命错误），但要防止水平触发下忙等。
3. **UDP `zn_sendto` 长度上限**：保留 1200 作为保守默认，还是改成 65507 并由调用者
   负责 MTU？推荐保留一个可配置常量，默认 1200，文档说明分片风险。
4. **`zn_buffer` 默认 packet handler**：默认丢弃（现状）还是默认收包？推荐默认收包，
   否则 `zn_recvfinish` 的默认行为是“静默丢数据”，太容易踩。
5. **公共 API 列对齐**：formatter 化后是否保留 `zn_connect (` 的空格？二选一，写进
   `AGENTS.md`，不要每改一行都手工对齐。
6. **`zn_deque.h`**：修好并纳入测试，还是标记为不支持/删除？当前没有任何使用者，
   建议先修编译和锁，再决定是否保留。
7. **签名 <80 的计量口径**：当前 AGENTS/TODO 按“整行，含返回类型和 `ZN_API`”严格执行。
   如果你只想约束“函数名 + 参数列表”，部分函数（`zn_send`/`zn_sendto`/`zn_recvfrom`）
   可以少改；但 `zn_connect` 仍需要 API 重构。
8. **`zn_connect` 的 API 重构**：参数结构体、把 `packet` 移到 `zn_newtcp`、新增
   `zn_connectex` 三个方向；要评估源码/ABI 兼容性，不能为了压行数悄悄改语义。
9. **`ZN_USE_64BIT_TIMER` 与 Clean C**：保持为 opt-in 扩展（当前），还是改成可移植的
   64 位表示（例如两个 32 位字或平台类型）；后者涉及类型/ABI 设计。
10. **MSVC C4100（未使用参数）**：用 `(void)` 全覆盖，还是构建脚本 `/wd4100`？用户要求
    `/W4` 无警告；建议 `(void)`，豁免必须在构建文件里写明。
