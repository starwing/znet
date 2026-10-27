# TODO — znet 缺陷清单与改进优先级

> 基线：2026-10-28，工作树（`git status` 是当前状态的唯一依据）。测试套件在
> `tests/`，构建入口只有根 `justfile`（`just check` / `test` / `cov` / `smoke` /
> `test-lua`），`GNUmakefile` 已删除。
>
> 本文件按状态分成两章：**1. 已完成**、**2. 待做**。第 3 章起是执行顺序、里程碑、
> 验收测试和设计取舍。行号以当前树为准，定位时优先用函数名。
>
> 覆盖率分析单独放在 [`tests/COVERAGE.md`](tests/COVERAGE.md)：当前
> `znet.h` 93.9% / `zn_buffer.h` 99.2%（macOS kqueue+select），剩余未覆盖行已逐条
> 分类说明。

## 0. 当前结论

| 优先级 | 内容 | 理由 |
| --- | --- | --- |
| **P0** | L8：epoll/kqueue 事件批保存裸 `zn_SocketInfo *`，同批回调删除另一个对象后可能 UAF | 是唯一剩下的崩溃/内存安全项；测试套件目前只覆盖“另一个 fd 不在本批”的情况 |
| **P1** | L6/L7/L9、B3/B4/B13、C2/C3、U1/U10、C4（`zn_lua.c` 审计） | 正确性、平台差异、Clean C / MSVC / 签名硬约束 |
| **P2** | L10/L11、B8/B9、T5/T6、U7/U8/U11、F1/F3 | 健壮性、可维护性、资源边界 |
| **P3** | L13、T7、M4（timer 四叉堆）、M5（收尾） | 清理和性能优化；必须先有基准/测试 |

**建议执行顺序**：L8 → P1 正确性与平台修复 → Clean C/MSVC/签名（C1–C4）→ M3 全量
格式化 → M4 timer 四叉堆 → M5 文档/示例收尾。CI（至少 Linux/macOS + Windows
`cl /W4 /WX`）应尽早补上，但它是独立工作流，不阻塞上面的顺序。

---

## 1. 已完成

### 1.1 正确性修复（已修，已验证）

验证矩阵：macOS `just check`（kqueue + select）与 `orb just check`（epoll + select）
全绿，默认 ASan/UBSan/LSan。IOCP 相关条目只做了代码审查，**未在 Windows 上验证**。

| ID | P | 内容 | 验证 |
| --- | --- | --- | --- |
| L1 | P0 | select 的 `znP_dispatch` 对 `S->infos[fd]` 判空；同一批事件里回调关闭另一个 fd 不再空指针崩溃 | `dispatch_select_close_other_fd` |
| L2 | P0 | reactive close 记账：取消“已 park（`can_* == 0`）”的方向，不再用 `can_*` 猜是否有排队结果；`zn_onevent`/`zn_onconnect` 不再“写 latch 却不产生完成” | `tcp_close_blocked_send`、`tcp_close_parked_and_queued`、`tcp_close_both_parked`；细节见 `notes/l2-latch.md` |
| L3 | P0 | 四个后端 UDP close 都置 `udp->socket = ZN_INVALID_SOCKET`；close 幂等，关闭后 `sendto`/`recvfrom` 返回 `ZN_ESTATE` | `udp_double_close`、`udp_sendto_after_close`、`udp_close_reused_fd` |
| L4 | P0 | IOCP `znP_closeudp` 置 `INVALID`，保留 `recv_handler` 直到完成事件；完成时不调用用户回调 | 仅代码审查（Windows 未验证） |
| L5 | P0 | `zn_accept` 在已有 accept 在途时返回 `ZN_EBUSY`，不再泄漏 retain/覆盖 handler | `tcp_accept_twice` |
| T1 | P1 | timer handler 返回值用 `zn_Time` 保存，不再截断到 `int` | `timer_64bit_handler_return`、`timer_handler_returns_int_max` |
| B1 | P0 | epoll `znP_signal` 的 `eventfd_write` 返回值判断修正；`zn_post` 成功返回 `ZN_OK` | `post_returns_ok_epoll`（Linux） |
| B2 | P1 | kqueue `zn_onrecvfrom` 的地址长度用 `sizeof(remote_addr)`，不再用指针大小截断 IPv6 | `udp_ipv6_loopback` |
| B5 | P1 | IOCP `znP_listen` 对监听 socket（而不是仍是 `INVALID_SOCKET` 的 `accept->socket`）设置 `SO_REUSEADDR` | 仅代码审查（Windows 未验证） |
| U2 | P1 | `znBC_upperbound`/`znBC_lowerbound` 的档位索引钳到真实数组范围，>2 GiB 请求走 fallback | `buffcache_boundaries`、`buffcache_huge_index` |
| U3 | P1 | `zn_addlstring`/`zn_addchar` 检查 `zn_prepbuffsize` 返回 NULL，OOM 不再越界写 | `buffer_append_oom`、`recv_oom_slow_path` |
| U5 | P2 | header handler 返回 0/帧跨读时，缓冲下来的字节会参与下一次 header 解析 | `recv_split_header`、`recv_partial_header_then_frames` |
| C1 | P1 | 删除枚举尾逗号；`zn_Time` 改为 `size_t`（见 1.7）；`znet.h`/`zn_buffer.h` 通过 `-std=c89 -pedantic-errors` 和 `-std=c++03 -pedantic-errors` | `just check-c89` 的 suite 部分（`example.c` 仍见 C4） |
| F4 | P2 | 构建入口收敛到根 `justfile`（`mod tests` / `mod bench`），删除 `GNUmakefile` 和 `.bat` | `just check` / `just bench` |
| U12 | P3 | 基准/echo 程序移到 `bench/` 并移植到当前 API（`packet = 0`）；帧格式修正 | `just bench`、`bench/README.md` |

同一批修复里还包括 TODO 未单列的三项：

- `zn_newtimer` 在 close 开始后返回 NULL；
- `zn_connect` 先检查 `connect_handler` 再检查 socket，重复 connect 返回 `ZN_EBUSY`；
- select 的 `nfds` 计入 post socketpair；select 的 connect 完成用 `SO_ERROR` 判断被拒绝的
  连接。

### 1.2 本会话完成：测试迁移、Lua 与覆盖率

| 内容 | 结果 |
| --- | --- |
| `test.c` 迁移 | 新增 `tcp_echo_loop`：timer 驱动 4 轮 connect，server 每次重挂 accept，echo 后删除对象；最后一轮在 accept 仍 pending 时删除 listener。覆盖 `test.c` 的完整行为后删除 `test.c`，`just smoke` 只跑 `example.c` |
| `test.lua` 迁移 | 移到 `tests/test.lua`，改为自包含的 loopback echo 测试（含 `getaddrinfo("localhost")`，不访问外网）；`tests/justfile` 新增 `test-lua`，从 `zn_lua.c` 构建 `znet.so` 后运行 |
| 覆盖率补测 | 新增/扩展 `tcp_send_error`、`tcp_recv_error`、`tcp_select_oob_recv_error`、`tcp_connect_unreachable`、`tcp_recv_result_after_close`、`state_newstate_init_failure`、`timer_cancel_sift_up`、`sendbuffer_merge_oom`、`recv_default_handlers`、`recv_split_header` |
| 覆盖率结果 | macOS：`znet.h` 93.9% 行 / 100% 函数，`zn_buffer.h` 99.2% 行；Linux：93.6% / 99.1%；POSIX 并集：93.3% / 99.2%。未覆盖行分类见 `tests/COVERAGE.md` |
| 文档 | `AGENTS.md`、`README.txt`、`tests/README.md` 已去掉 `test.c` 引用，补上 `test-lua`、`tests/test.lua` 和覆盖率报告入口 |

### 1.3 本轮一行修复（2026-10-28）

判定标准：一处局部改动、不动公共 API/ABI、不引入新语义决策。T3/T4/U4/U6/B7 的新断言
都在 HEAD 上逐条复现过失败；B6 的可测点来自本轮新增的辅助函数，无法在 HEAD 上跑同一断言；
其余项的验证方式在表里注明。

| ID | 修复 | 回归 / 验证 |
| --- | --- | --- |
| T3 | 空堆时 `znT_gettimeout` 返回 `ZN_FOREVER`，不再返回 `ZN_FOREVER - now` | `timer_nexttime_and_timeout` |
| T4 | 取消根节点/最后一个 timer 时同步 `nexttime`，不再提前唤醒 | `timer_cancel_updates_nexttime` |
| T8 | 删掉 `zn_MemPool timers;` 行末多余的 `\` | 编译 |
| B6 | epoll 超时经 `znP_timeoutms` 钳到 `INT_MAX`，>24.8 天不再回绕成“永久等待” | `epoll_timeout_clamp`（Linux） |
| B7 | select/reactive 的 `znP_initudp` 对 UDP socket 调用 `znU_set_nonblock` | `udp_socket_is_nonblocking`（POSIX） |
| B10 | select 的 `select()` 出错（含 `EINTR`）直接返回 0，不再遍历未定义的 `fd_set` | 无回归：旧代码在 Linux/macOS 上恰好也无害，只有 MSan 能发现；改动本身是严格安全的 |
| B11 | IOCP 把 0 字节 UDP 数据报按成功接收投递 | 无回归；仅代码审查，**未在 Windows 验证** |
| B12 | epoll `EPOLL_CTL_DEL` 前清零 `struct epoll_event` | 无回归；纯加固（当前内核只读 `events`） |
| U4 | `zn_initrecvbuffer`/`zn_resetrecvbuffer` 清零 `packet_ud`，去掉重复赋值 | `recv_default_handlers` |
| U6 | `zn_prepbuffcache` 返回实际缓存的块数 | `buffcache_alloc_free` |
| U9 | Lua timer 的整数返回值分支先 `lua_pop` 再 return | `tests/test.lua` 数字返回值用例（覆盖分支；栈槽泄漏在 Lua 侧不可观测） |
| U13/U14/C4 | `example.c` 修 `"%sn"`、`MyData *data` 移到块首、23 处 `%p` 配 `(void*)p`；`zn_task.h` 去掉 C89 非法的 typedef 重定义 | `just check-c89` / `just check-cpp03` 全绿（recipe 现在也编译 `zn_task.h`） |

`check-c89`/`check-cpp03` 因此首次全绿；Linux VM 没有 C++ 编译器，cpp03 只在 macOS
验证过。C4 只剩 `zn_lua.c` 的严格 C89 审计。

### 1.4 覆盖报告里的“死代码”复核（2026-10-28）

- 三个 `znP_dispatch` 的 `default: ;`（select/epoll/kqueue）删除；`zn_SocketInfo.type`
  改为 `zn_SocketType`，这样以后新增 socket 类型时 `-Wswitch` 会报错（保留 `default`
  恰好会压掉这个保护）。IOCP 的同名 `default` 未动（本机不能编译 IOCP）。
- select `zn_tcpfromfd` 的 `fd >= FD_SETSIZE` 分支不可达（唯一调用者 `zn_onaccept`
  已经先关闭这种 fd），改成 `assert(fd < FD_SETSIZE)`：不变式来自调用者，assert 行
  在每次 accept 时都会执行，不再是一条永远为 0 的覆盖记录。
- select 的 except 派发 send 错误分支不是死代码：`tcp_select_except_send_error` 直接
  派发 except 集，证明 `zn_onsend(tcp, err=1)` 可达并完成 `ZN_ERROR`。
- `zn_buffer.h` 的两行刻意保留：快路径 OOM 的 `-1`（数据路径上唯一的错误返回）和
  `znBC_putcache` 的 >2 GiB 回退（真实可达）。理由见 `tests/COVERAGE.md` §C。
- 新增 `just uncovered`（= `just test uncovered`）：按函数分组列出上次 `just cov` 的
  未覆盖行；函数边界来自源码扫描，因为双后端合并后的 lcov FN 区间不可用。

### 1.5 已消除、不要重新引入

- kqueue 的 `znP_poll` 在结果队列有积压时必须返回 `1`（`znet.h:2762`）。之前的本地
  `return 0` 已经还原；`rq_poll_returns_one_when_backlog` 覆盖该场景。
- select 的 connect 完成必须用 `SO_ERROR` 判断，不能把“可写”直接当成成功。
- UDP close 后必须保留 `INVALID_SOCKET` 哨兵；否则会 double close 或让关闭后的
  `sendto` 通过状态守卫。

### 1.6 当前验证基线

- 本机 macOS：`just check`（kqueue + select）全绿；`just smoke` 退出码 0；
  `just test-lua` 输出 `test.lua: OK`。
- Linux（OrbStack）：`orb just check`（epoll + select）全绿。
- `znet.h`、`zn_buffer.h`、`zn_task.h`、`example.c` 与 `tests/*.c` 都通过严格
  C89/C++03（`TEST` 变参宏豁免）；`just check-c89` / `just check-cpp03` 全绿（cpp03
  只在 macOS 验证，Linux VM 没有 C++ 编译器）。
- `znet.h` 仍有超过 80 列的函数签名和已折行的签名（C3），尚未做签名重构。
- MSVC `/W4` 未在本机验证；clang `-Wconversion -Wsign-conversion` 仍是近似检查（C2）。
- IOCP/Windows 无法在本机构建；相关条目只能标“代码审查 + 待 CI”，不能声称已验证。
- 没有 CI；覆盖率/测试矩阵目前靠本机与 `orb` 手工跑。

### 1.7 本轮：`zn_Time` 统一为 `size_t`（2026-10-28）

决策：删掉 `ZN_USE_64BIT_TIMER` 这个 opt-in 标记，默认 `zn_Time` = `size_t`（`znet.h:46`）。
64 位平台上 timer 直接是 64 位毫秒；32 位平台仍是 32 位，49.7 天回绕作为文档化限制保留。
这是**刻意的 ABI 变更**：`zn_time`、`zn_starttimer`、`zn_TimerHandler` 都用 `zn_Time`，
调用方必须用 `zn_Time`，不能再用 `unsigned`/`int` 接。

| 内容 | 结果 |
| --- | --- |
| C1 的 64 位部分 | 关闭：不再需要 `unsigned long long`，默认配置保持 Clean C |
| T2 | 64 位平台关闭；32 位回绕保留并在 `notes/timers.md` 说明 |
| T5 的宏部分 | 删除未使用的 `ZN_MAX_TIMERPOOL`；堆不收缩/无上限仍在 T5 |
| 收窄点 | `znS_poll` 新增 `znS_clampms` 把非 `ZN_FOREVER` 等待钳到 `INT_MAX`；IOCP 显式 `(DWORD)`；`zn_time` POSIX 先转再乘 |
| 绑定 | `znet.hpp` 的 `timer_cb` 参数、`zn_lua.c` 的 `delayms` 改为 `zn_Time` |
| 测试 | `timer_64bit_delay`/`timer_64bit_handler_return` 从 `#ifdef` 改为 32 位运行期 skip；新增 `timer_timeout_clamp`；删除 `check-64bit-timer` 目标 |
| 验证 | macOS：`just check-c89` / `just check-cpp03` / `just smoke` / `just check`；Linux：`orb just check`。IOCP 只做代码审查，**未在 Windows 验证**；32 位未验证 |

---

## 2. 待做

### 2.1 P0 — 崩溃/内存安全

| ID | 位置 | 缺陷与影响 | 建议 |
| --- | --- | --- | --- |
| L8 | `znet.h:2530,2726` | epoll/kqueue 的事件批保存 `zn_SocketInfo *` 裸指针；回调里删除另一个对象后，同一批后续事件会拿到悬空指针（UAF）。kqueue 的 `znP_dispatch` 还不判 `info` 是否为 NULL。现有 `dispatch_delete_other_in_callback` 只让一个 client 写，另一个 fd 不在本批，所以没有证明同批安全。 | 先写“两个 fd 同批就绪、回调删除另一个”的失败用例（reactive-only），再选方案：`zn_State` 增加待释放队列，`ZN_PUTOBJECT` 只入队，`znP_poll` 在一批 dispatch 完后统一 flush；或引入对象代际/活跃检查。不要只加 NULL 检查。 |

### 2.2 P1 — 正确性、平台差异与硬约束

**生命周期 / 关闭 / 后端**

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| L6 | `znet.h:1966,2026` | select 的 `zn_onaccept` 每个事件最多接受一个连接；虚假就绪（`accept` → `EAGAIN`）时清 handler、注销 fd、不回调，监听器静默死亡。`zn_onrecvfrom` 遇到 `EAGAIN` 同理，UDP 接收静默死亡。 |
| L7 | `znet.h:2473,2669,2500,2696` | epoll/kqueue 的 `zn_onaccept`/`zn_onrecvfrom` 在 `EAGAIN` 时也清 handler、直接返回，不回调；监听器/UDP 进入“无 handler”状态。水平触发下可能空转。 |
| L9 | `znet.h:1337,1528,1538` | IOCP：`znP_connect` 在 `zn_getextension` 失败时不关闭 socket；`zn_onaccept` 不检查 `zn_newtcp` 返回 NULL；`GetAcceptExSockaddrs` 失败时泄漏 client；完成端口关联失败时静默丢弃 accepted socket 且不回调。 |
| B3 | `znet.h:1582,2015,2393` | 四个后端的 `znP_sendto` 忽略 `sendto` 返回值；UDP 发送失败不可见，仍返回 `ZN_OK`。 |
| B4 | `znet.h:724` | `zn_sendto` 无条件拒绝 `len > 1200`，与路径 MTU 无关。 |
| B13 | `znet.h:895,936` | **新增（写覆盖率测试时发现）**：在项目统一选项 `-D_XOPEN_SOURCE=600` 下，macOS 既不定义 `MSG_NOSIGNAL` 也不定义 `SO_NOSIGPIPE`，于是 `ZN_NOSIGNAL` 变成 0、`znU_set_nosigpipe` 编译为空。向已断开的 TCP `send` 会触发 SIGPIPE 杀死进程。`tcp_send_error` 只能在测试里临时忽略 SIGPIPE；Linux 上 `MSG_NOSIGNAL` 可见，所以问题只在 macOS。修法：在平台段显式启用 `_DARWIN_C_SOURCE`，或提供可选的 `SIGPIPE` 忽略入口；需评估库是否应改全局信号处理。 |

**定时器**

T2 已在 1.7 关闭：`zn_Time` = `size_t`，64 位平台不再回绕。32 位平台的 49.7 天回绕是
文档化限制（`notes/timers.md`），不再是待修项；`start + delayms` 的溢出检查仍见 T6。

**Clean C / MSVC / 签名**

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| C2 | `znet.h` 多处 | MSVC `/W4` 收窄/符号性风险未分类完成：收窄点（`ssize_t`→`int`、`size_t`→`unsigned`、`uintptr_t`→`int`）需显式 cast + 值域检查；同宽 signedness 优先改类型/比较/printf 格式。clang `-Wconversion -Wsign-conversion` 只是近似，实际清单需 Windows CI。 |
| C3 | `znet.h:103,107,417,486,640,660,678,722,730,791,1582,2015,2393` | 签名 <80 且不换行：`zn_connect` 114 列、`zn_send` 95、`zn_sendto` 100、`zn_recvfrom` 97、`zn_recv` 89、`zn_closeaccept` 81；回调 typedef 也超。`zn_connect` 必须改 API（参数结构体/去掉 `packet`/新入口），不能只折行。 |
| C4 | `zn_lua.c` | 剩余 Clean C 审计项：`zn_lua.c` 尚未按严格 C89 审计（依赖 Lua 头）。`example.c`、`zn_task.h` 已在本轮通过 `check-c89`/`check-cpp03`（见 1.3）。 |

**工具 / Lua**

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| U1 | `zn_deque.h:302,245-261,279` | 不能编译（`znD_frontU`/`znD_backU` 先用后定义）；POSIX `zn_deldeque` 二次 unlock 未加锁的 mutex；`pthread_cond_timedwait` 收到相对 `timespec`，正 `waitms` 会立即超时。 |
| U10 | `zn_lua.c:441` | `lzn_onaccept` 只在 Lua 回调返回真值时重新 `zn_accept`；否则监听器处于“已发起但无 handler”状态，水平触发下可能空转。 |

### 2.3 P2 — 健壮性与可维护性

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| L10 | `znet.h:316` | mempool 的 `assert` 在 `NDEBUG` 下消失；非法 `size` 会静默破坏内存。 |
| L11 | `zn_addrinfo.h:146` | 不在 `zn_close` 前调用 `zn_closeaddrinfo` 时，迟到的 post 可能触碰已释放 state；`znA_clearreq` 忽略 `zn_post` 失败，请求和 retain 会泄漏。 |
| B8 | `znet.h:2352` | reactive 的 `znP_listen` 先注册 fd 再 bind/listen；失败时不注销，留下 stale `S->infos[fd]`/epoll 注册。 |
| B9 | `znet.h:1872,1898` | select 的 `zn_onsend`/`zn_onrecv` 在就绪后遇到 `EAGAIN` 会清 handler、release，却不回调，操作被静默丢弃。 |
| T5 | `znet.h:53` | 定时器堆只增长、不收缩；没有数量上限。 |
| T6 | `znet.h:443` | `start + delayms` 没有溢出检查；回绕后 `emit` 顺序错误。 |
| U7 | `bench/zn_bufferpool.h` | `zn_putbuffer` 回收节点时没有 `zn_resetrecvbuffer`/`zn_resetsendbuffer`；下一个连接继承旧分帧状态。 |
| U8 | `znet.hpp` | `TcpAccept` 把 client 存在 accept 对象里，连续 accept 会修改同一个 `TcpSocket`；handler 运行一次即被消费，容易误用。 |
| U11 | `zn_task.h` | `zn_deltaskpool` 遇到永久阻塞的任务会永久阻塞；需要文档化或提供超时/取消语义。 |
| F1 | `.clang-format.new` | 尚未纳入版本控制/应用；`AlignAfterOpenBracket: AlwaysBreak` 会产生难看的折行；`IncludeBlocks`/`SortIncludes` 可能重排 `znet.h` 的平台 include。M3 最终排版提交时改回 `.clang-format`。 |
| F3 | `AGENTS.md, notes/*` | 部分行号已漂移；修 bug 后需再校对一遍。 |

### 2.4 P3 — 清理、性能与收尾

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| L13 | `znet.h:256` | `znL_empty(h)` 展开成 `(h)->prev == &(h)`：传头指针或头变量都编不过。当前无人使用；要用先改成 `(void*)(h)->next == (void*)(h)`。 |
| T7 | `znet.h:390` | 相同 deadline 不保证 FIFO（严格 `>` 比较）；要么文档化，要么改成稳定顺序。 |
| M4 | `znet.h` timer | 四叉堆（4-ary heap）优化；前置条件是 T1/T2 已修、timer 测试齐全、M3 完成；先写基准，收益不足就回退。 |
| M5 | 文档/示例 | 更新 `README.txt` 的约束矩阵、校对 `notes/*` 行号、补 CI 脚本。`test.c` 的清理已完成，`just smoke` 现在只跑 `example.c`。 |

### 2.5 验证缺口（不是代码缺陷）

- **没有 CI**：至少需要 ubuntu/macos/windows 三套矩阵；Windows job 必须包含
  `cl /W4 /WX` 的 C 和 C++ 编译，以及 IOCP 测试。本机不能声称 IOCP 已验证。
- **覆盖率门槛**：当前报告解释了剩余行为什么不可覆盖；如果要把 100% 行覆盖作为硬门槛，
  应对结构性死代码使用 `LCOV_EXCL_LINE`，对资源错误做 fault-injection 构建，而不是靠
  fd 耗尽测试。详见 `tests/COVERAGE.md`。

### 2.6 为什么这些不是“一行修复”

按阻塞原因分组 —— 每组的共同点是：改一处会牵动契约、多个后端或外部验证。

| 阻塞原因 | 条目 | 需要先定的事 |
| --- | --- | --- |
| 内存安全方案选择 | L8 | 延迟释放队列 vs 对象代际/活跃检查；还要先写出“两个 fd 同批就绪”的失败用例 |
| 虚假就绪语义 | L6、L7、B9 | 自动重挂还是回调 `ZN_ERROR`（第 7.1 节）；自动重挂还要防水平触发忙等 |
| 跨后端完成/资源语义 | B3、L9 | UDP 发送失败如何变成完成事件；IOCP 的 socket 所有权与失败清理 |
| 公共 API/ABI 决策 | C3、B4、B13 | 签名 <80 要重构 `zn_connect`；UDP 上限与 SIGPIPE 策略（第 7.2、7.10 节） |
| 工具/绑定重构 | U1、U7、U8、U10、U11 | `zn_deque.h` 是修还是删（第 7.5 节）；Lua/pool 的语义要先写进文档 |
| 记账与资源边界 | L10、L11、B8、T5、T6 | 非法参数/失败路径要引入错误码或上限，属契约改动 |
| 机械但多站点或需外部环境 | M3/F1、F3、CI、`zn_lua.c` 的 C89 审计 | 单独提交（格式化）或需要 Windows/CI，不适合混进语义提交 |

---

## 3. 六个改进项的优先级

| 用户项 | 优先级 | 放在哪个里程碑 | 说明 |
| --- | --- | --- | --- |
| 1. 单测套件 | **P0** | M0（已完成） | 套件、后端矩阵、ASan/UBSan、覆盖率都已落地；余下只有 CI。 |
| 2. timer 改为四叉树 | **P3** | M4 | 当前二叉堆在常规规模可用；四叉堆只是常数优化。先修 T1/T2 等语义 bug，再拿基准决定。 |
| 3. 跑 clang-format | **P2** | M3 | 必须等 P0/P1 功能修复稳定后单独提交；否则格式 diff 会淹没真正的 bug 修复。 |
| 4. 修复函数签名折行 | **P1** | M3 | 新约束要求“单行 <80 且不能折行”；`zn_connect` 等需要 API 重构。 |
| 5. 修复当前可能有的 bug | **P0/P1** | M1/M2 | 只剩 L8（P0）和 P1 清单；IOCP 相关仍需 Windows 验证。 |
| 6. 其他文档缺陷修复 | **P0–P3** | 分散 | 纯文档/示例/过时测试放 P3；`test.c` 已删除，`example.c` 的 `"%sn"`/`%p` 已修（1.3）。 |

C1/C2/C4（Clean C、MSVC `/W4`、其余文件）和 C3（签名 <80 不换行）都是 P1 硬约束；
其中 C3 与第 4 项是同一件事：先重构签名（语义/API 改动），再做全量格式化（P2）。

---

## 4. 里程碑与状态

### M0 — 测试与回归基线（已完成，CI 除外）

已落地：

- `tests/` 套件：mempool、timer、addr、addrinfo、state、resultqueue、tcp、dispatch、
  udp、buffer；`misc/gen_entries.lua` 生成入口；默认 ASan+UBSan。
- `just check`（native + select）、`just test`、`just check-select`、
  `just cov`、`just cov-lines`、`just cov-unbranched`。
- macOS kqueue+select 与 Linux epoll+select 全绿；L2 的多个失败面有回归。
- `just test-lua`：Lua 绑定的自包含冒烟测试。
- 覆盖率报告 `tests/COVERAGE.md`。

未完成：CI 矩阵（ubuntu/macos/windows）。

### M1 — P0 高危修复（除 L8 外已完成）

B1、L3、L4、L2、L5、L1 已修并配回归；L8 仍在待做清单。IOCP 的 L4 只做了代码审查。

### M2 — P1 正确性与平台修复（进行中）

已完成：B2、B5、C1（枚举尾逗号 + 64 位 timer，见 1.7）、C4 的
`test.c`/`example.c`/`zn_task.h` 部分、B6/B7/U4/U6/U9/T3/T4（见 1.3）。

剩余：

1. C4 的 `zn_lua.c` 严格 C89 审计；
2. C2 的 MSVC `/W4` 收窄分类与 Windows CI；
3. L6/L7 的 EAGAIN 语义（推荐自动重挂）、L9 的 IOCP 资源泄漏；
4. B3/B4/B13；
5. U1/U10。

### M3 — 签名重构 + 全量 clang-format（未开始）

前置：M2 完成、测试绿、工作区干净。步骤与验收沿用旧计划：

1. 决定格式策略：把 `.clang-format.new` 改回 `.clang-format`；`AlignAfterOpenBracket`
   从 `AlwaysBreak` 改为 `Align`；审查 `IncludeBlocks`/`SortIncludes`（建议
   `SortIncludes: Never`，或用 `clang-format off` 包住公共声明区）。
2. 先做签名重构（P1，语义/API 改动）：`zn_connect` 用参数结构体/新入口；压缩
   `zn_send`/`zn_sendto`/`zn_recvfrom`/`zn_recv`/回调 typedef 和内部函数签名到单行 <80。
3. 增加签名检查脚本（扫描 `znet.h`：签名行 <80；有 `(` 但闭合 `)` 不在同一行即失败），
   接入 `just format-check` 和 CI。
4. 全量格式化只覆盖维护中的文件；`lbind.h` 和生成文件不动。
5. 格式化提交独立，提交信息写明 “format only”，并加入 `.git-blame-ignore-revs`。
6. 更新 `AGENTS.md` 的格式/列对齐/签名三节。

### M4 — timer 四叉堆（未开始）

前置：T1/T2 已修、timer 测试齐全、M3 完成。步骤：写不依赖网络的 timer 基准（n=1k/10k/
100k）→ 改成四叉堆（父 `(i-1)/4`，子 `4*i+1..4*i+4`）→ 用有序数组做随机 property test
（arm/cancel/re-arm/fire，比较触发顺序和 `nexttime`）→ 只有基准显示明显收益才保留，
否则回退并在 `notes/timers.md` 记录。

### M5 — 文档、示例、过时测试收尾（未开始）

`test.c` 已删除并迁移，`example.c` 的 `"%sn"`/`%p`/C89 问题已修（1.3）；剩余：处理
`zn_deque.h`；校对 `notes/*` 行号；更新 `README.txt`；加 CI/`scripts/check.sh` 并写明
本机验证范围。

---

## 5. 关键验收测试（已在套件中）

| 条目 | 测试 | 断言 |
| --- | --- | --- |
| L1 | `dispatch_select_close_other_fd` | 同一批事件中关闭另一个 fd，进程不崩溃（ASan 干净） |
| L2 | `tcp_close_blocked_send` | 填满发送缓冲后 `zn_deltcp`，`zn_run(LOOP)` 返回，`waitings == 0` |
| L2 | `tcp_close_parked_and_queued` / `tcp_close_both_parked` | 只取消 parked 方向，不 double release |
| L3 | `udp_double_close` / `udp_sendto_after_close` | 第二次 close 返回 `ZN_OK`；关闭后 sendto 返回 `ZN_ESTATE` |
| L5 | `tcp_accept_twice` | 第二次 `zn_accept` 返回 `ZN_EBUSY`；`waitings` 归零 |
| B1 | `post_returns_ok_epoll` | Linux epoll 上 `zn_post` 返回 `ZN_OK`，handler 被调用 |
| B2 | `udp_ipv6_loopback` | IPv6 对端地址不截断 |
| T1 | `timer_64bit_handler_return` | handler 返回值不被截断 |
| U2/U3 | `buffcache_boundaries` / `buffer_append_oom` | 越界档位拒绝；OOM 不写坏内存 |
| U5 | `recv_split_header` | 跨读 header 正确解码 |
| T3 | `timer_nexttime_and_timeout` | 空堆时 `znT_gettimeout` 恰好返回 `ZN_FOREVER` |
| T3/收窄 | `timer_timeout_clamp` | `znS_clampms` 把非 `ZN_FOREVER` 等待钳到 `INT_MAX`，`ZN_FOREVER` 原样保留 |
| T4 | `timer_cancel_updates_nexttime` | 取消根后 `nexttime` 指向新根；清空后为 `ZN_FOREVER` |
| B6 | `epoll_timeout_clamp`（Linux） | 超时 > `INT_MAX` 被钳位，不回绕成负数 |
| B7 | `udp_socket_is_nonblocking`（POSIX） | UDP fd 带 `O_NONBLOCK` |
| U4/U6 | `recv_default_handlers` / `buffcache_alloc_free` | `packet_ud` 初始化；预分配返回实际块数 |
| 死代码复核 | `tcp_select_except_send_error` | except 集上的 pending send 以 `ZN_ERROR` 完成（不是死分支） |
| L8 | 待补 | 两个 fd 同批就绪、回调删除另一个，ASan 干净 |
| B13 | `tcp_send_error` | 对端断开后 send 回调收到错误；测试临时忽略 SIGPIPE，库侧修复待做 |

---

## 6. 风险与“不要做”的事

1. **不要先跑全量 `clang-format`**：一千行格式 diff 会让后续 bug 修复无法 review。
2. **不要在 timer 语义 bug 未修时换四叉堆**：测试失败时无法区分“堆错”还是“语义错”。
3. **不要为了迁就旧测试改库 API**：过时测试应移植；`test.c` 已删除。
4. **不要用“加一个 NULL 检查”解决 L8**：对象内存可能已被复用；要延迟释放或引入
   代际/活跃检查。
5. **不要在没有 Windows/Linux 环境时声称 IOCP/epoll 已验证**：只写“代码审阅 + 待 CI”。
6. **不要退回 32 位 `zn_Time`，也不要重新引入 `ZN_USE_64BIT_TIMER`**：1.7 已决定
   `zn_Time` = `size_t`；这是刻意的 ABI 变更，调用方必须用 `zn_Time`。
7. **不要把格式化提交和语义提交混在一起**；格式化提交必须能用 `git diff -w` 验证为空。
8. **不要删除 `.clang-format.new`/`.clang-format` 了事**：要么定策略并纳入版本控制，
   要么明确“不格式化”。
9. **不要用折行、调大 `ColumnLimit` 或 `// clang-format off` 绕过签名 <80**：折行是
   重构信号，不是最终格式。
10. **不要机械给所有 `size_t`/`ssize_t`/`int`/`unsigned` 转换加 cast**：MSVC `/W4` 只对
    收窄转换强制要求显式 cast；同宽问题优先改类型/格式。
11. **不要把 `znet.hpp` 拉进 Clean C**：它是显式 C++11 可选包装。
12. **不要用 `long long` 表示 `zn_Time`**：宽度靠 `size_t` 自适应，默认配置必须 Clean C。
13. **不要用 fd 耗尽/信号关闭等脆弱手段硬凑覆盖率**：见 `tests/COVERAGE.md` 的取舍。

---

## 7. 待确认的设计取舍

1. **虚假 `EAGAIN`**：自动重挂还是回调 `ZN_ERROR`？推荐自动重挂（就绪是假的，不是致命
   错误），但要防止水平触发下忙等。
2. **UDP `zn_sendto` 长度上限**：保留 1200 作为保守默认，还是改成 65507 并由调用者负责
   MTU？推荐保留可配置常量，默认 1200，文档说明分片风险。
3. **`zn_buffer` 默认 packet handler**：默认丢弃（现状）还是默认收包？推荐默认收包，
   否则 `zn_recvfinish` 的默认行为是“静默丢数据”，太容易踩。
4. **公共 API 列对齐**：formatter 化后是否保留 `zn_connect (` 的空格？二选一，写进
   `AGENTS.md`，不要每改一行都手工对齐。
5. **`zn_deque.h`**：修好并纳入测试，还是标记为不支持/删除？当前没有使用者；建议先修
   编译和锁，再决定是否保留。
6. **签名 <80 的计量口径**：当前按“整行，含返回类型和 `ZN_API`”严格执行。若只约束
   “函数名 + 参数列表”，部分函数可以少改；但 `zn_connect` 仍需要 API 重构。
7. **`zn_connect` 的 API 重构**：参数结构体、把 `packet` 移到 `zn_newtcp`、新增
   `zn_connectex` 三个方向；要评估源码/ABI 兼容性。
8. **`ZN_USE_64BIT_TIMER` 与 Clean C**：已决策（1.7）：删除该标记，`zn_Time` = `size_t`。
   64 位平台得到 64 位时间，32 位回绕按文档化限制处理；不再有 opt-in 分支。
9. **MSVC C4100（未使用参数）**：用 `(void)` 全覆盖，还是构建脚本 `/wd4100`？用户要求
   `/W4` 无警告；建议 `(void)`，豁免必须在构建文件里写明。
10. **B13 的 SIGPIPE 策略**：库自己忽略 `SIGPIPE`（进程级副作用），还是在 macOS 上
    显式启用 `SO_NOSIGPIPE`（需要处理 `_XOPEN_SOURCE` 的可见性）？推荐后者；不要悄悄
    改全局信号处理。
