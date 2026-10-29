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
| **P1** | C3、U1 | 签名 <80 重构（M3 前置）、`zn_deque.h` 的修/删决定 |
| **P2** | L10/L11、B8/B14、T5、U7/U8/U11/U15、F1/F3 | 健壮性、可维护性、资源边界 |
| **P3** | L13、M5（收尾） | 清理；M4 已试并按基准回退（见 1.16） |

**建议执行顺序**：L8 → P1 正确性与平台修复 → Clean C/MSVC/签名（C1–C3）→ M3 全量
格式化 → M5 文档/示例收尾（M4 已在 1.16 用基准否决）。CI（至少 Linux/macOS + Windows
`cl /W4 /WX`）应尽早补上，但它是独立工作流，不阻塞上面的顺序。

---

## 1. 已完成

### 1.1 正确性修复（已修，已验证）

验证矩阵：macOS `just check`（kqueue + select）与 `orb just check`（epoll + select）
全绿，默认 ASan/UBSan/LSan。IOCP 相关条目只做了代码审查，**未在 Windows 上验证**。

| ID | P | 内容 | 验证 |
| --- | --- | --- | --- |
| L1 | P0 | select 的 `znB_dispatch` 对 `S->infos[fd]` 判空；同一批事件里回调关闭另一个 fd 不再空指针崩溃 | `dispatch_select_close_other_fd` |
| L2 | P0 | reactive close 记账：取消“已 park（`can_* == 0`）”的方向，不再用 `can_*` 猜是否有排队结果；`zn_onevent`/`zn_onconnect` 不再“写 latch 却不产生完成” | `tcp_close_blocked_send`、`tcp_close_parked_and_queued`、`tcp_close_both_parked`；细节见 `notes/l2-latch.md` |
| L3 | P0 | 四个后端 UDP close 都置 `udp->socket = ZN_INVALID_SOCKET`；close 幂等，关闭后 `sendto`/`recvfrom` 返回 `ZN_ESTATE` | `udp_double_close`、`udp_sendto_after_close`、`udp_close_reused_fd` |
| L4 | P0 | IOCP `znB_closeudp` 置 `INVALID`，保留 `recv_handler` 直到完成事件；完成时不调用用户回调 | 仅代码审查（Windows 未验证） |
| L5 | P0 | `zn_accept` 在已有 accept 在途时返回 `ZN_EBUSY`，不再泄漏 retain/覆盖 handler | `tcp_accept_twice` |
| T1 | P1 | timer handler 返回值用 `zn_Time` 保存，不再截断到 `int` | `timer_64bit_handler_return`、`timer_handler_returns_int_max` |
| B1 | P0 | epoll `znB_signal` 的 `eventfd_write` 返回值判断修正；`zn_post` 成功返回 `ZN_OK` | `post_returns_ok_epoll`（Linux） |
| B2 | P1 | kqueue `zn_onrecvfrom` 的地址长度用 `sizeof(remote_addr)`，不再用指针大小截断 IPv6 | `udp_ipv6_loopback` |
| B5 | P1 | IOCP `znB_listen` 对监听 socket（而不是仍是 `INVALID_SOCKET` 的 `accept->socket`）设置 `SO_REUSEADDR` | 仅代码审查（Windows 未验证） |
| U2 | P1 | `znZ_upperbound`/`znZ_lowerbound` 的档位索引钳到真实数组范围，>2 GiB 请求走 fallback | `buffcache_boundaries`、`buffcache_huge_index` |
| U3 | P1 | `zn_addlstring`/`zn_addchar` 检查 `zn_prepbuffsize` 返回 NULL，OOM 不再越界写 | `buffer_append_oom`、`recv_oom_slow_path` |
| U5 | P2 | header handler 返回 0/帧跨读时，缓冲下来的字节会参与下一次 header 解析 | `recv_split_header`、`recv_partial_header_then_frames` |
| C1 | P1 | 删除枚举尾逗号；`zn_Time` 改为 `size_t`（见 1.7）；`znet.h`/`zn_buffer.h` 通过 `-std=c89 -pedantic-errors` 和 `-std=c++03 -pedantic-errors` | `just check-c89` 的 suite 部分（`example.c` 见 C4；`zn_lua.c` 豁免见 1.10） |
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
| T3 | 空堆时 `znT_gettimeout` 返回 `ZN_FOREVER`，不再返回 `ZN_FOREVER - now` | `timer_root_and_timeout` |
| T4 | 取消根节点/最后一个 timer 后最早 deadline 不失效（后改为直接读 `keys[0]`，缓存已删） | `timer_cancel_updates_root` |
| T8 | 删掉 `zn_MemPool timers;` 行末多余的 `\` | 编译 |
| B6 | epoll 超时经 `znB_timeoutms` 钳到 `INT_MAX`，>24.8 天不再回绕成“永久等待” | `epoll_timeout_clamp`（Linux） |
| B7 | select/reactive 的 `znB_initudp` 对 UDP socket 调用 `znU_set_nonblock` | `udp_socket_is_nonblocking`（POSIX） |
| B10 | select 的 `select()` 出错（含 `EINTR`）直接返回 0，不再遍历未定义的 `fd_set` | 无回归：旧代码在 Linux/macOS 上恰好也无害，只有 MSan 能发现；改动本身是严格安全的 |
| B11 | IOCP 把 0 字节 UDP 数据报按成功接收投递 | 无回归；仅代码审查，**未在 Windows 验证** |
| B12 | epoll `EPOLL_CTL_DEL` 前清零 `struct epoll_event` | 无回归；纯加固（当前内核只读 `events`） |
| U4 | `zn_initrecvbuffer`/`zn_resetrecvbuffer` 清零 `packet_ud`，去掉重复赋值 | `recv_default_handlers` |
| U6 | `zn_prepbuffcache` 返回实际缓存的块数 | `buffcache_alloc_free` |
| U9 | Lua timer 的整数返回值分支先 `lua_pop` 再 return | `tests/test.lua` 数字返回值用例（覆盖分支；栈槽泄漏在 Lua 侧不可观测） |
| U13/U14/C4 | `example.c` 修 `"%sn"`、`MyData *data` 移到块首、23 处 `%p` 配 `(void*)p`；`zn_task.h` 去掉 C89 非法的 typedef 重定义 | `just check-c89` / `just check-cpp03` 全绿（recipe 现在也编译 `zn_task.h`） |

`check-c89`/`check-cpp03` 因此首次全绿；Linux VM 没有 C++ 编译器，cpp03 只在 macOS
验证过。C4 的 `zn_lua.c` 部分改为豁免（见 1.10）。

### 1.4 覆盖报告里的“死代码”复核（2026-10-28）

- 三个 `znB_dispatch` 的 `default: ;`（select/epoll/kqueue）删除；`zn_SocketInfo.type`
  改为 `zn_SocketType`，这样以后新增 socket 类型时 `-Wswitch` 会报错（保留 `default`
  恰好会压掉这个保护）。IOCP 的同名 `default` 未动（本机不能编译 IOCP）。
- select `zn_tcpfromfd` 的 `fd >= FD_SETSIZE` 分支不可达（唯一调用者 `zn_onaccept`
  已经先关闭这种 fd），改成 `assert(fd < FD_SETSIZE)`：不变式来自调用者，assert 行
  在每次 accept 时都会执行，不再是一条永远为 0 的覆盖记录。
- select 的 except 派发 send 错误分支不是死代码：`tcp_select_except_send_error` 直接
  派发 except 集，证明 `zn_onsend(tcp, err=1)` 可达并完成 `ZN_ERROR`。
- `zn_buffer.h` 的两行刻意保留：快路径 OOM 的 `-1`（数据路径上唯一的错误返回）和
  `znZ_putcache` 的 >2 GiB 回退（真实可达）。理由见 `tests/COVERAGE.md` §C。
- 新增 `just uncovered`（= `just test uncovered`）：按函数分组列出上次 `just cov` 的
  未覆盖行；函数边界来自源码扫描，因为双后端合并后的 lcov FN 区间不可用。

### 1.5 已消除、不要重新引入

- kqueue 的 `znB_poll` 在结果队列有积压时必须返回 `1`（`znet.h:2762`）。之前的本地
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

### 1.8 L6/L7 就绪语义与 L9 IOCP 资源所有权（2026-10-28）

实测复现推翻了两条原始描述：

- **L6（select）**：`accept`/`recvfrom` 返回 `EAGAIN` 时，旧代码已经消费了 armed 操作
  （清 handler、release、注销 fd）却不回调，监听器/UDP 静默死亡。白盒注入一次就绪报告
  即可确定性复现。
- **L7（epoll/kqueue）**：accept 的 `EAGAIN` 分支其实**不可达** —— reactive 的
  `znB_listen` 从来没把监听 socket 设成 non-blocking（select 有），所以过期就绪是把整个
  循环卡在 `accept()` 里（epoll 上实测 `timeout` 退出 124），不是"空转"。UDP 的 `EAGAIN`
  分支是活的（B7 已修 nonblock），行为与 L6 相同：静默丢弃。
- **决策**：`EAGAIN` 是"就绪报告与队列矛盾"，一律解除 armed 并回调 `ZN_ERROR`（socket
  不关闭，回调里可重新发起）；`ECONNABORTED`/`EINTR` 属于正常事件，保持 armed、不回调。
  不引入重试计数。规则见 `notes/backends.md` 的"就绪不是完成"。
- **L9**：五处资源所有权修复，外加 TODO 未列的一处真实泄漏（AcceptEx 失败后
  `accept->client` 不关闭，重挂 accept 即泄漏一个 socket）。

| 内容 | 结果 |
| --- | --- |
| L6 | select 的 `zn_onaccept`/`zn_onrecvfrom` 改为"先系统调用、后按结果消费"；`EAGAIN` 回调错误、`ECONNABORTED`/`EINTR` 保持 armed；`fd >= FD_SETSIZE` 只关连接、不再杀监听器；`zn_tcpfromfd` 失败回调 `ZN_ERROR` |
| L7 | reactive `znB_listen` 补 `znU_set_nonblock`；两个后端采用同一语义；解除 armed 时**不注销** fd（注册是 per-listen） |
| L9 | `znB_connect` 扩展指针失败关 socket；`!bSuccess` / `GetAcceptExSockaddrs` 失败 / `zn_newtcp` 返回 NULL 三处关 `accept->client` 并回调；完成端口关联失败改用 `zn_deltcp`；入口 `assert(cb)` |
| 测试 | 新增 6 个：nonblock 断言、accept/UDP 的假就绪、accept 的错误事件、accept/UDP 的错误事件回归（前 4 个在修复前失败/挂死），以及 RST 风暴守门（修复前也通过，用来否决"重试 N 次"设计） |
| 验证 | macOS：`just check`、`just check-c89`、`just check-cpp03`、`just smoke`；Linux：`orb just check`。IOCP 只做代码审查，**未在 Windows 验证** |

### 1.9 B9：select 的 send/recv 就绪语义（2026-10-28）

`zn_onsend`/`zn_onrecv` 以前在系统调用之前就消费操作，`EAGAIN` 时静默丢弃（没有回调、
retain 已经还了）。现在只有结果才消费：`EINTR`/`EAGAIN`/`EWOULDBLOCK` 保持 armed
（fd 不注销，下一次就绪重试）；真错误或 EOF 才消费，并按不变量 6 关闭 TCP。

第一版把 `EAGAIN` 当 accept 那样报错并关连接，这是错的：TCP 方向本来就有 park 状态
（reactive 的 `can_*` 在边沿后遇到 `EAGAIN` 就是重新 park，不报错），而且 TCP 的可写
是"半个 `SO_SNDBUF` 空着"这样的阈值条件、`SO_SNDBUF` 应用可改小，所以"可写后仍没
空间"可能是真实瞬时状态。select 于是改成 park，与 reactive 一致；accept/UDP 仍报错
（它们没有 park 语义）。规则见 `notes/backends.md` 的"就绪不是完成"。

| 内容 | 结果 |
| --- | --- |
| 代码 | `znB_finishsend`/`znB_finishrecv` + 两个 handler 重写；只有结果才消费，`EAGAIN` 留在注册位等下次就绪 |
| 测试 | `tcp_select_send_spurious_ready` / `tcp_select_recv_spurious_ready`；修复前都因 `send_handler == NULL` / `recv_handler == NULL` 失败，随后同样的就绪把 park 的操作完成（send 用填满的内核发送缓冲确定性触发 `EAGAIN`） |
| 验证 | macOS `just check`、`just check-c89`/`check-cpp03`；Linux `orb just check`；覆盖率见 `tests/COVERAGE.md` |

### 1.10 M2 收尾：B3/B4/B13/U10 + C4 豁免（2026-10-28）

| ID | 修复 | 回归 / 验证 |
| --- | --- | --- |
| B3 | 三个 `znB_sendto`（IOCP / select / reactive）检查 `sendto` 返回值：POSIX 返回 `znP_error(errno)`，Win32 用 `WSAGetLastError()`，不再无条件 `ZN_OK` | `udp_sendto_error`：IPv4 socket 发给 IPv6 地址（EINVAL / EAFNOSUPPORT / WSAEAFNOSUPPORT）→ `ZN_ERROR`；修复前该断言拿到 `ZN_OK` |
| B4 | 1200 改成可覆盖常量 `ZN_MAX_UDPSEND`（`#ifndef`，默认 1200），`zn_sendto` 用它做上限；`README.txt` 与 `notes/backends.md` 写明“不产生 IP 分片”的理由 | `udp_sendto_max_payload` 改用 `ZN_MAX_UDPSEND`；`+1` 仍返回 `ZN_EPARAM` |
| B13 | Darwin 在 `_XOPEN_SOURCE=600` 下既没有 `MSG_NOSIGNAL` 也看不到 `SO_NOSIGPIPE`。新增 `ZN_SO_NOSIGPIPE`（可见时用 `SO_NOSIGPIPE`，Darwin 否则用 ABI 值 `0x1022`），`znU_set_nosigpipe` 改用它；并把设置时机从“connect 完成”提前到 socket 创建（`znB_connect` 建 fd 后），accept 路径不变 | `tcp_send_error` 删掉测试里的 `SIG_IGN`，进程必须活下来；新增 `tcp_nosigpipe_option`（macOS）直接用 `getsockopt` 断言客户端与服务端 socket 都是 1 |
| U10 | `lzn_onaccept`：Lua 回调真值才重新 `zn_accept`，否则 `zn_closeaccept`；accept 失败时回调收到 `(self, nil, errstr, err)`，返回真仍可重试；`pcall` 失败也关闭 | `tests/test.lua` 新增第 4 节：只接受一次并返回 false，之后再连同一端口必须失败。修复前报 “a stopped listener still accepted a connection” 并在空转中等到超时 |
| C4 | `zn_lua.c` 从严格 C89 审计中豁免：它必须包含 Lua 头，而 Lua 自己用 `long long`，实际编译始终是 C99。代码仍按 C89 风格写 | `AGENTS.md` 与 `justfile` 注释写明；`check-c89` 本来就没包含 `zn_lua.c`，现在不再把它当待办 |

同轮补充：

- `tests/check.bat` / `tests/run.bat` / `bench/build.bat` / `bench/run.bat`：Windows/MSVC 入口
  （`cl /nologo /W4 /WX`；套件列表来自 `tests\*_test.c` 的 glob，不会与 POSIX 的 `SUITES`
  漂移）。`misc/msvc-env.bat` 在 `cl` 不在 PATH 时用 vswhere 找 Visual Studio 并
  `call vcvars64.bat`，所以普通 cmd / ssh 会话也能直接跑这些脚本；justfile 里 POSIX-only 的
  recipe 标了 `[unix]`，Windows 用 `[windows]` 分发到这些脚本（`smoke` 在同一条命令行里
  先 call 环境脚本）。**未在 Windows 上验证**。
- `notes/utils.md` 的 U10 描述与已修的 U9 记录一起更新；`notes/backends.md` 的
  “UDP 发送错误不可见”删掉（已修）。
- 同类问题仍在：Lua 绑定的 `lzn_onrecvfrom` 返回假值时同样留下 handler-less 的已注册 fd，
  记为 U15（见 2.3）。
- 顺手修掉 1.7 留下的 `-Wformat`：`bench_client.c` / `bench_server.c` 用 `%d` 打印
  `zn_Time`，改成 `%u` + `(unsigned)`，与 `bench_echo.c` 一致；`just bench` 现在无警告。

验证：macOS `just check`、`just test-lua`；Linux `orb just check`（结论见 1.11）。

### 1.11 验证基线与 clang-format 评估（2026-10-28）

本机 macOS：`just check`（kqueue + select）、`just check-c89`、`just check-cpp03`、
`just smoke`、`just test-lua` 全绿。Linux（OrbStack）：`orb just check`（epoll + select）
全绿。IOCP / Windows 当时的结论是“只有代码审查 + 新加的 `.bat` 入口”；
**1.12 已在真机上验证，见下。**

clang-format 现在不能跑，原因与数据见 M3 的“2026-10-28 评估”。

### 1.12 Windows / IOCP 首次验证（2026-10-28）

第一次在真机（`ssh wx-miku`，VS2022 x64）上跑 `tests\check.bat`（`cl /nologo /W4 /WX`，
IOCP）：**全绿**。之前的所有“未在 Windows 验证”条目现在有了实际结果。发现的问题全部是
**既有代码/测试在 Windows 上从没跑过**造成的，不是本轮 M2 修改引入的（对照 HEAD：
`WSAStringToAddress`、IOCP 监听 socket 的 `SO_REUSEADDR`、accept 填地址的写法都在 HEAD 里）：

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| C4244 ×6（`znet.h` 816/1021/1026/1324、`zn_addrinfo.h` 181/279） | Win32 段 `int`→`ADDRESS_FAMILY`、`unsigned`→`u_short`、`uintptr_t`→`unsigned` | `htons((unsigned short)port)` 前先 `port > 0xFFFFu` 返回 0（POSIX 共用这段，行为一致了）；`family` 显式 cast；`_endthreadex(0)` 替掉 `(uintptr_t)znA_thread`（语义问题见 U16） |
| C4702（`znA_worker` 的 `return` 不可达） | `for (;;)` 里 return | 改成 `ret` + `break` |
| `tcp_connect_echo` 的 peer 端口/地址是垃圾 | IOCP accept 用 `znP_setinfo((zn_SockAddr*)&paddr2, ...)`：取了指针变量的地址 | 改成 `paddr2`（真 bug；`GetAcceptExSockaddrs` 给的 remote sockaddr） |
| `addr_parse_invalid`：`"127.0.0"` 被接受 | Windows 用 `WSAStringToAddress`，它接受部分点分形式；POSIX 的 `inet_pton` 拒绝 | Win32 `znP_pton` 改 `InetPtonA`，`znP_ntop`/`znA_ntop` 改 `InetNtopA`（与 POSIX 的 inet_pton/ntop 对齐） |
| `tcp_listen_conflict`：第二次 listen 成功 | Windows 的 `SO_REUSEADDR` 允许第二个 socket 抢占端口 | IOCP 监听 socket 改 `SO_EXCLUSIVEADDRUSE`（POSIX 的“绑定冲突”语义） |
| `addr_test` 在 Windows 上 `znP_parseaddr` 全部失败 | 地址转换走 Winsock，旧实现需要 `WSAStartup` | 换成 `InetPtonA`/`InetNtopA` 后不需要初始化（实测无需 `WSAStartup`），测试的“纯转换”前提保留 |
| `dispatch_test` 编译失败：`zn_State` 没有 `result_queue` | 该断言只对 reactive 后端成立，守卫漏了 `_WIN32` | 守卫改成 `!ZN_USE_SELECT && !_WIN32` |
| 假绿：套件里 abort 了却报 “all suites passed” | MSVC 的 `abort()` 退出码是 `0xC0000409`（负数），cmd 的 `if errorlevel 1` 按有符号比较 | `check.bat` 改判 `if not "!errorlevel!"=="0"`；并且每个测试一个进程，abort 不再隐藏同一套件后面的用例 |
| `cl: error D8003 缺少源文件名` | `/I"%SRC%"` 的 `%SRC%` 以 `\` 结尾，`\"` 吃掉参数 | `/I"%SRC%/."` |
| Windows 上的 “select 那一遍” 是假的 | `znet.h` 在 `_WIN32` 上固定 `ZN_USE_BACKEND_IOCP`，`-DZN_USE_SELECT` 只影响测试宏 | `check.bat` 只跑 native；`select` 参数明确报错退出 2 |
| `tcp_send_after_close`：close 后立刻 reconnect 返回 `ZN_EBUSY` | IOCP 的挂起 connect 完成仍持有 handler/retain，直到完成被取出 | 测试按平台断言（见 B14）；`waitings == 0` 也只在 POSIX 断言 |

### 1.13 验证基线更新（2026-10-28，Windows 并入）

- 本机 macOS：`just check`、`check-c89`、`check-cpp03`、`smoke`、`test-lua`、`just bench`。
- Linux（OrbStack）：`orb just check`。
- **Windows（wx-miku，VS2022 x64）**：`just check` → `tests\check.bat`（IOCP，
  `cl /W4 /WX`）全绿：115 个用例 OK、1 个 SKIP（`addr_error_mapping` 是 POSIX errno
  语义），0 失败。`just smoke`（`example.c` 在 `cl /W4 /WX` 下）通过；`just bench`
  能构建（仍有 C5 的 `/W4` 警告）。IOCP 不再是“只做代码审查”。
- 注意：这台机器上的仓库走 Synology Drive 同步，未落地的云占位文件会让 `cl` 报
  C1083 “Invalid argument / No such file”；用 `Get-Content` 读过一次（或整个目录）即可。

### 1.14 Windows 残余修复：DNS worker 生命周期与任务池终止（2026-10-28）

1.13 之后仍然只在 Win32 上成立的三处缺陷，外加一处让它们“假绿”的测试漏洞。全部在
wx-miku 上复现、修复并验证：

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| `zn_closeaddrinfo(NULL)` 之后调用者再也不返回：主线程被结束，worker 还活着，进程挂住 | Win32 用 `_endthreadex(0)` 想停 worker，实际终止的是**调用者线程** | worker 增加 `znA_stop`；置标志 + `SetEvent` 唤醒，`WaitForSingleObject` join 之后才关事件/锁。`znA_stop` 在 `znA_init` 里复位，停止后可以重新起 worker |
| `zn_closeaddrinfo(S)`（`S != NULL`）之后全局初始化标记被清 0，下一次查询重新 `InitializeCriticalSection`/`CreateEvent`/起第二个 worker（UB + 句柄/线程泄漏） | Win32 在 `S != NULL` 时也把 `znA_initalize` 写成 0；POSIX 没有这个问题 | `S != NULL` 只清该 state 的请求，然后把标记恢复成 1；只有 `S == NULL` 才走完整 teardown |
| POSIX `zn_closeaddrinfo(NULL)` 死锁 | 持锁调 `pthread_cancel` + `pthread_join`：worker 在 `pthread_cond_wait` 的取消点要先重新拿到这把锁；即使取消成功，worker 也会持锁退出 | 两个平台统一成协作式停止：置标志 + `pthread_cond_signal`，解锁后 join。worker 退出前把手上未处理的请求按 `ZN_ERROR` post 回去 |
| `zn_task.h` 的 Win32 分支从来没被编译过：`_beginthreadex` 没有声明（缺 `<process.h>`，C4013/C4312），`zn_tasker` 用 `DWORD WINAPI` 与 `_beginthreadex` 期望的 `unsigned (__stdcall *)(void *)` 不匹配（C4057）；另外 `WaitForMultipleObjects` 失败的兜底路径调 `_endthreadex(handle)` 会终止调用者线程，`CreateEvent` 失败时会对未初始化的 `CRITICAL_SECTION` 调 `DeleteCriticalSection` | 缺包含、线程过程类型不一致，以及与 U16 同一类的 `_endthreadex` 误用 | 补 `<process.h>`；`zn_tasker` 改成 `unsigned __stdcall zn_tasker(void *)`；只在事件创建成功（锁已初始化）时删锁；join 失败时 `assert` + 放弃 teardown，**不再强杀 worker**（见下面的评审修正） |

测试与工具：

- `addrinfo_close_state_keeps_worker`：`zn_closeaddrinfo(S)` 之后 `znA_initalize` 仍为 1，
  下一个 state 的查询复用同一个 worker（修复前 Windows 上是 0）。
- `addrinfo_stop_all`：`zn_closeaddrinfo(NULL)` 必须返回调用者，之后还能重新起 worker 并
  完成查询。用 `zt_guard_start/stop` 把“worker 不退出”变成失败而不是挂住；修复前 Windows
  上进程挂住（主线程被杀、worker 存活），macOS 上 join 死锁。
- `tests/check.bat` 现在要求每个测试进程的输出里有 harness 的 ` OK`/` SKIP` 行：进程以 0
  退出但没有跑完（主线程被杀之类）不再算通过。判决逻辑用临时脚本验证过 OK/SKIP/缺行
  三种输入。它还会用 `/W4 /WX` 编译一遍 `zn_task.h`（套件都不包含它，Win32 分支
  因此从来没进过编译器）；临时探针在 wx-miku（`cl /W4 /WX`）和 macOS（`cc -Werror`）上
  各跑通一次 8 个任务的建池/投递/回收/删池。

评审修正（同日）：第一版把 `WaitForMultipleObjects` 失败的兜底改成 `TerminateThread`，
那只是把“终止调用者”换成“终止 worker” —— `TerminateThread` 同样跳过 `_endthreadex`，
CRT 每线程块不回收，而且按文档线程持有的 critical section / 堆锁都不会释放，紧接着的
`DeleteCriticalSection` + `free(ws)` 反而更危险。现在两个平台在 join 失败时只 `assert`
并放弃 teardown（泄漏池），不再强杀线程；`_beginthreadex` 的选择才自洽。

验证（2026-10-28）：

- Windows（wx-miku，VS2022 x64，`cl /W4 /WX`）：`tests\check.bat` 全绿（117 个用例 OK、
  1 个 SKIP，套件源码和 `zn_task.h` 都用 `/W4 /WX` 编过），`tests\run.bat addrinfo`
  六个用例全 OK；`example.c` 按 `just smoke` 的命令构建并在 `/W4 /WX` 下运行通过；
  `zn_task.h` 的临时探针（8 个任务的建池/投递/回收/删池）通过。
- macOS：`just check`（kqueue + select）、`just check-c89`、`just check-cpp03`、`just smoke`。
- Linux（OrbStack）：`orb just check`（epoll + select）。
- 评审修正只动了 `zn_task.h`：之后 `check.bat` 的 `zn_task.h` 编译门和池探针（Windows /
  macOS 各一次）重新跑过；套件本身不包含该头文件，其余结论不受影响。

仍然只在 Windows 上存在的：B14（IOCP close 时在途 connect 要等一次 poll 才能重连，
属 L8 同族的完成事件代际问题）。C5 紧接着在 1.15 收掉了。

### 1.15 C5 + Windows 上的 `just` 入口（2026-10-28）

M2 剩下的“C2 收窄分类 + `example.c`/`bench` 的 MSVC 确认 + CI 化”在这里收尾。这里的
“CI 化”指**能用 `just` 一条命令在 Windows 上跑 example/bench**，GitHub Actions 矩阵仍是
M5 之后的事（2.5）。

| 内容 | 结果 |
| --- | --- |
| C5：`bench/*.c` 的 `/W4` 清理 | 49 条 → 0（C4100 ×35 用 `(void)param;`、C4267 ×11 给 `zn_sendsize()` 加显式 `(unsigned)` cast（上界是 `ZN_BUFFERSIZE`）、C4459 ×3 把 `zn_State *S` 参数改名 `state`）；`bench\build.bat` 改成 `cl /W4 /WX` |
| `bench` 在 Windows 上从来没跑过 | `setvbuf(stdout, NULL, _IOLBF, 0)` 在 MSVC 上**直接 abort**（0xC0000409，且不 flush，所以连 “znet engine: IOCP” 都看不到）。六个程序改成 `_IONBF, 0`：run 脚本用 `taskkill` 收尾，缓冲输出本来就会丢 |
| bench 的 MSVC 构建没开优化 | `bench\build.bat` 只有 `/W4`，没有 `/O2` —— 基准程序是 `/Od` 跑出来的。补上 `/O2`；实测 echo 35 → 36.5 MB/s、flood 不变，所以它不是平台差距的原因，但基准不优化就没有意义 |
| `bench\run.bat` 不能无头运行 | `timeout /t N /nobreak` 在 stdin 被重定向时直接报 “Input redirection is not supported”（ssh/CI 必然踩），退化成 0 秒等待；改成 `ping -n` |
| C2 的 MSVC 部分 | `check.bat`（znet.h + 套件）、`example.c`、`bench` 三者都在 `cl /W4 /WX` 下干净；clang `-Wconversion` 不是门槛（不在 `-Wall` 里），按约定不修 |
| `just` 入口验证（Windows） | `just smoke`、`just bench`、`just bench echo 2` / `flood 1` / `echo-buffer 1`、`just test tcp` 全部在 wx-miku 上跑通；bench 输出约 35 MB/s（echo/echo-buffer）、75 MB/s（flood），说明不仅构建成功、也确实在跑 |

验证（2026-10-28）：Windows（wx-miku）上以上命令逐条通过，分离的 `bench_server` +
`bench_client` 也用 netstat 确认过确实 accept（server 端 5 s 内 connect=334）；macOS 上
`just bench` / `just bench echo 2` / 分离 pair 通过（`_IONBF` 在 POSIX 上同样可见）。
`bench/*.c` 另外逐个过了 `-std=c89 -pedantic-errors` 和 `-std=c++03 -pedantic-errors`
（收 `(void)` 时一度把 `zn_Tcp *tcp` 挤到语句后面，C89 检查抓到后已移回块首）。
`just check` / `check-c89` / `check-cpp03` / `smoke` 不受 bench 改动影响（bench 不在
这些目标里）。

**性能标定（2026-10-28）**：先用两个数字回答“macOS 50 MB/s、Windows 35 MB/s 正常吗”。

| 场景（1 KiB） | macOS（M4 Mac mini） | Windows（i9-9900 OptiPlex） |
| --- | --- | --- |
| 裸 socket 阻塞 ping-pong（基线，TCP_NODELAY） | ~100 MB/s | ~68 MB/s |
| znet echo（kqueue / IOCP） | ~50 / — | — / ~36.5 |
| znet echo（select） | ~46 | 无 select 后端 |
| znet echo-buffer | ~48 | ~36 |
| znet flood（单向） | ~410 | ~75 |

结论：

1. **跨机器不能比**：两台机器差一代以上（M4 vs 2019 的 i9-9900），loopback + 系统调用成本本来
   就差 1.5 倍（上表基线）。
2. **同机看，znet 是裸 socket 的 ~50%**（macOS 50/100，Windows 36.5/68）—— 两个平台的
   “事件循环开销”一致，没有 Windows 专属退化。echo 本质是 RTT 基准：50 MB/s = 51k 往返/s
   ≈ 19.6 µs RTT；36.5 MB/s = 37k 往返/s ≈ 26.8 µs。
3. **flood 的 5.4 倍差距来自完成模型**，不是实现 bug：reactive 的 `znB_send` 里直接
   `send()`，成功就把结果塞进 result queue，事件循环在同一次 poll 里把回调链跑完（一次
   kevent 可以推动多条消息，2.4 µs/消息）；IOCP 每条消息都要 `WSASend` + 等一次完成
   （`GetQueuedCompletionStatusEx`），而“每方向只有一个在途操作”（不变量 2）让它无法流水线，
   13 µs/消息。要改只能允许每方向多个在途 send，那是契约改动，不在 M2/M3 范围。
4. IOCP 的 `znB_dispatch` 每条完成都调一次 `GetOverlappedResult`（bWait=FALSE，等于读
   overlapped 的 Internal 状态）；77k/s 下是白白的函数调用，但换成读未文档化的
   `Internal` 不值当，暂不动。

**为什么 1 KiB 只有 ~50 MB/s（不是库的问题）**：同一条 loopback 上把消息从 1 KiB 换成
64 KiB，消息率几乎不变（56k/s → 46k/s），吞吐从 54 MB/s 涨到 **~2.5 GB/s** —— 说明瓶颈是
“每条消息的固定成本”，不是数据路径。把 1 KiB 那次的 CPU 拆开（macOS，client 侧，6 s）：
user 0.17 s、sys 1.93 s，约 33.5 万条消息 → **用户态 0.08 µs/条（库的代码），内核
5.8 µs/条**。每侧每条消息 4 个系统调用（`kevent` 等待 / `recv` / `send` / 重新
`EV_ADD`），macOS 上每次 ~1.4–2 µs，RTT ≈ 2×6 + 调度 ≈ 20 µs；阻塞 socket 只要 2 个
系统调用/侧（9.8 µs RTT、100 MB/s），差距正好是事件循环多出来的那两个调用。M4 的快
是算力和内存带宽，不体现在系统调用延迟上。

---

### 1.16 M4：T6/T7 修复、timer 基准、property test，4 叉按基准回退（2026-10-29）

| ID | 内容 | 验证 |
| --- | --- | --- |
| T6 | `zn_starttimer` 值域：`delayms >= ZN_FOREVER` 返回 `ZN_EPARAM`，失败零副作用（先校验后 cancel）；`delayms == 0` 仍合法；handler 返回哨兵不重挂。Lua `Ltimer_start` 的反向 ref 判断一并修掉 | `timer_start_param` |
| T7 | 相同 deadline 的触发顺序不定义（只文档化）；测试改成"组间有序、组内不比" | `timer_fire_same_deadline` |
| 基准 | 新增 `bench/bench_timer.c`：arm/cancel/fire/clock，无网络；`just bench timer` | `just bench`、严格 C89/C++03 |
| property | 256 个 timer × 2000 次随机 arm/cancel/fire，检查堆序、index、used、root（`keys[0]`）和触发顺序；确认写错 sift-up/sift-down 都会红 | `timer_heap_property` |
| M4 | 4 叉实现完成并实测，`fire` 全面退化超过 5% 门槛 → **回退**，保留二叉堆 | 7 轮交替 A/B |

4 叉实验（4 叉/二叉的比值中位数，越小越好，7 轮交替 A/B，macOS arm64）：

| 操作 | 1k | 10k | 100k |
| --- | --- | --- | --- |
| arm | 1.004 | 0.997 | 0.993（这项没测到 sift-up） |
| cancel | 0.841 | 0.768 | **0.456** |
| fire | **1.368** | **1.087** | **1.055** |

`cancel` 的 2.2 倍收益不抵消 `fire`（每个定时器必经的 pop-min）在所有规模上的退化；
根因是 sift-down 的比较次数与叉数无关，4 叉只省移动和 cache line。完整数据和三种
sift-down 变体见 `notes/m4-4ary-heap.md` §5；重新评估的前提（最坏情况 arm 基准、
键拷贝/索引堆）见 §8。

提交：`timer: reject delays at or above ZN_FOREVER (T6)`、`test: pin the same-deadline
contract (T7)`、`bench: timer-heap microbenchmark`、`test: randomized model check for the
timer heap`、`bench: document what arm actually measures`。4 叉实现没有提交。

验证（2026-10-29，最终树）：macOS `just check` / `check-c89` / `check-cpp03` / `smoke` /
`test-lua`；Linux `orb just check`；Windows `wx-miku just check`（IOCP，`/W4 /WX`）。

---

### 1.17 M4b：timer 堆键拷贝（SoA，2026-10-29）

`zn_TimerState.heap`（`zn_Timer **`）拆成 `keys[]`（`zn_Time`，排序键）和 `nodes[]`
（`zn_Timer *`）：sift 比较只读连续的 key 数组，不再每层解引用 48 B 的 timer。句柄、
`index`、`emit` 语义都不变；`znT_resize` 用 realloc 扩两个数组（keys 成功后立即提交，nodes 失败时堆仍一致）。

7 轮交替 A/B（键拷贝/原指针堆的中位数比值，macOS arm64）：

| 操作 | 1k | 10k | 100k |
| --- | --- | --- | --- |
| arm | 1.050 | 1.054 | 1.046 |
| cancel | 0.938 | 0.820 | 0.792 |
| fire | **0.684** | **0.662** | **0.639** |
| clock（对照） | 0.998 | 0.998 | 1.000 |

保留：fire 快 32-36%；arm 约 5% 的退化来自每层多一次 8 B 存储。随后又删掉了
`zn_Timer.emit`（deadline 只在 `keys[]`，Timer 48 → 40 B），所以**净内存与改动前持平**
（40 + 8 + 8 = 56 B/timer，原来 48 + 8），arm 也回到原来的水平。4 叉堆仍不采用
（1.16）；稳定 id / 索引堆留给 B4（结论和原型数据见 `notes/m4-4ary-heap.md` §8）。

同轮还删掉了 `zn_TimerState.nexttime` 缓存：它恒等于 `used ? keys[0] : ZN_FOREVER`，
是 T3/T4 两个 bug 的来源；`znT_gettimeout`/`znT_update` 改成直接读 `keys[0]`，测试相应
改名（`timer_root_and_timeout`、`timer_cancel_updates_root`）。

验证：macOS `just check` / `check-c89` / `check-cpp03` / `smoke` / `test-lua`、
`orb just check`（epoll + select）、`wx-miku just check`（IOCP，`/W4 /WX`）。

---

## 2. 待做

### 2.1 P0 — 崩溃/内存安全

| ID | 位置 | 缺陷与影响 | 建议 |
| --- | --- | --- | --- |
| L8 | `znet.h:2661,2855` | epoll/kqueue 的事件批保存 `zn_SocketInfo *` 裸指针；回调里删除另一个对象后，同一批后续事件会拿到悬空指针（UAF）。kqueue 的 `znB_dispatch` 还不判 `info` 是否为 NULL。现有 `dispatch_delete_other_in_callback` 只让一个 client 写，另一个 fd 不在本批，所以没有证明同批安全。 | 先写“两个 fd 同批就绪、回调删除另一个”的失败用例（reactive-only），再选方案：`zn_State` 增加待释放队列，`ZN_PUTOBJECT` 只入队，`znB_poll` 在一批 dispatch 完后统一 flush；或引入对象代际/活跃检查。不要只加 NULL 检查。 |

### 2.2 P1 — 正确性、平台差异与硬约束

**生命周期 / 关闭 / 后端**

M1/M2 的条目（L1–L7、L9、B1/B2/B5–B7、B10–B12）都已修完并有回归；剩下的 L8 仍在
上面的 P0 表里，这一栏暂无其他待做项。

**定时器**

T2 已在 1.7 关闭：`zn_Time` = `size_t`，64 位平台不再回绕。32 位平台的 49.7 天回绕是
文档化限制（`notes/timers.md`），不再是待修项；`start + delayms` 的溢出检查仍见 T6。

**Clean C / MSVC / 签名**

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| C3 | `znet.h`、`zn_addrinfo.h`、`zn_lua.c` | 签名 <80 且不换行：4 个回调 typedef（`zn_AcceptHandler` 87、`zn_SendHandler` 82、`zn_RecvHandler` 82、`zn_RecvFromHandler` 119）、`zn_connect` 114、`zn_sendto` 100、`zn_recvfrom` 97、`zn_send` 95、`zn_recv` 89（声明+定义各一次）、`znT_newtimer` 81、`znP_ntop` 81、三个 `znB_sendto` 85–87；`zn_addrinfo.h` 另有 4 个，`zn_lua.c` 的 `lzn_ongetaddr` 87；`bench/*.c` 里还有 3 个 80–81 列的回调签名（`bench_echo.c`、`bench_echo_buffer.c`、`bench_flood.c` 的 `on_client_send`/`on_client_recv`）。`zn_connect` 必须改 API（参数结构体/去掉 `packet`/新入口），不能只折行。 |

**工具 / Lua**

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| U1 | `zn_deque.h:302,245-261,279` | 不能编译（`znD_frontU`/`znD_backU` 先用后定义）；POSIX `zn_deldeque` 二次 unlock 未加锁的 mutex；`pthread_cond_timedwait` 收到相对 `timespec`，正 `waitms` 会立即超时。 |

### 2.3 P2 — 健壮性与可维护性

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| L10 | `znet.h:316` | mempool 的 `assert` 在 `NDEBUG` 下消失；非法 `size` 会静默破坏内存。 |
| L11 | `zn_addrinfo.h:146` | 不在 `zn_close` 前调用 `zn_closeaddrinfo` 时，迟到的 post 可能触碰已释放 state；`znA_clearreq` 忽略 `zn_post` 失败，请求和 retain 会泄漏。 |
| B8 | `znet.h:2352` | reactive 的 `znB_listen` 先注册 fd 再 bind/listen；失败时不注销，留下 stale `S->infos[fd]`/epoll 注册。 |
| T5 | `znet.h:53` | 定时器堆只增长、不收缩；没有数量上限。 |
| U7 | `bench/zn_bufferpool.h` | `zn_putbuffer` 回收节点时没有 `zn_resetrecvbuffer`/`zn_resetsendbuffer`；下一个连接继承旧分帧状态。 |
| U8 | `znet.hpp` | `TcpAccept` 把 client 存在 accept 对象里，连续 accept 会修改同一个 `TcpSocket`；handler 运行一次即被消费，容易误用。 |
| U11 | `zn_task.h` | `zn_deltaskpool` 遇到永久阻塞的任务会永久阻塞；需要文档化或提供超时/取消语义。**强杀已经否决**（`TerminateThread`/`pthread_cancel` 跳过线程自己的清理，还会留下被持有的 `ws->lock`，见 1.14 的评审修正）；真要能停只能做协作式取消。 |
| B14 | `znet.h` IOCP `znB_closetcp`/`zn_connect` | close 时仍有一个在途 connect，它的完成事件还没被取出；这期间 `zn_connect` 返回 `ZN_EBUSY`，`S->waitings` 也仍是 1。POSIX 的 close 会取消 armed 操作，所以契约（不变量 6“close 后可以重新 connect”）在 IOCP 上要等一次 poll 才成立。要修得给完成事件加代际/取消（`CancelIoEx`），和 L8 一起考虑。 |
| U15 | `zn_lua.c` `lzn_onrecvfrom` | 回调返回假值时只 unref，不关闭也不注销 fd；reactive 的注册是 per-listen，剩下的 datagram 会让 epoll/kqueue 空转（U10 的同类问题）。关闭 socket 会连带毁掉 `sendto`，改成 per-operation 注册又会动 reactive 模型，所以先要定“停止接收”的语义。 |
| F1 | `.clang-format.new` | 尚未纳入版本控制/应用；`AlignAfterOpenBracket: AlwaysBreak` 会产生难看的折行；`IncludeBlocks`/`SortIncludes` 可能重排 `znet.h` 的平台 include。M3 最终排版提交时改回 `.clang-format`。 |
| F3 | `AGENTS.md, notes/*` | 部分行号已漂移；修 bug 后需再校对一遍。 |

### 2.4 P3 — 清理、性能与收尾

| ID | 位置 | 缺陷与影响 |
| --- | --- | --- |
| L13 | `znet.h:256` | `znL_empty(h)` 展开成 `(h)->prev == &(h)`：传头指针或头变量都编不过。当前无人使用；要用先改成 `(void*)(h)->next == (void*)(h)`。 |
| M6 | `znet.h` reactive 后端 | 每次 arm 操作都发一次 `kevent(EV_ADD)` / `epoll_ctl(MOD)`：1 KiB 回环基准里这是每消息 4 个系统调用之一（1.15 的标定：用户态只占 0.08 µs/条，内核 5.8 µs）。改成“每连接注册一次 + `EV_CLEAR`/`EPOLLET` + `can_*` latch”，或一次 dispatch 里把缓冲读空，预计 1 KiB RTT 20 → ~15 µs，主要收益是每消息 CPU 降 20–25%。风险：动 reactive 状态机与 L2 的 latch 记账，必须和 L8 一起评估，排在 M3 之后。 |
| M5 | 文档/示例 | 更新 `README.txt` 的约束矩阵、校对 `notes/*` 行号、补 CI 脚本。`test.c` 的清理已完成，`just smoke` 现在只跑 `example.c`。 |

### 2.5 验证缺口（不是代码缺陷）

- **没有 CI 服务**：目前只有本机 + `orb` + wx-miku 的手工矩阵（1.15 之后 `just smoke`/
  `bench`/`test` 在 Windows 上可直接跑，这是“CI 化”在 M2 的定义）。GitHub Actions 的
  ubuntu/macos/windows 三矩阵排在 M5 之后：Windows job 需要 `cl /W4 /WX` 的 C 和 C++
  编译、IOCP 测试，以及 `misc/gen_entries.lua` 依赖的 lua。
- **覆盖率门槛**：当前报告解释了剩余行为什么不可覆盖；如果要把 100% 行覆盖作为硬门槛，
  应对结构性死代码使用 `LCOV_EXCL_LINE`，对资源错误做 fault-injection 构建，而不是靠
  fd 耗尽测试。详见 `tests/COVERAGE.md`。

### 2.6 为什么这些不是“一行修复”

按阻塞原因分组 —— 每组的共同点是：改一处会牵动契约、多个后端或外部验证。

| 阻塞原因 | 条目 | 需要先定的事 |
| --- | --- | --- |
| 内存安全方案选择 | L8 | 延迟释放队列 vs 对象代际/活跃检查；还要先写出“两个 fd 同批就绪”的失败用例 |
| 跨后端完成语义 | B3（已修） | UDP 发送失败直接由同步返回值报告，不需要完成事件 |
| 公共 API/ABI 决策 | C3 | 签名 <80 要重构 `zn_connect`（B4/B13 已在 1.10 决策） |
| 工具/绑定重构 | U1、U7、U8、U11、U15 | `zn_deque.h` 是修还是删（第 7.5 节）；Lua/pool 的语义要先写进文档 |
| 记账与资源边界 | L10、L11、B8、T5 | 非法参数/失败路径要引入错误码或上限，属契约改动 |
| 机械但多站点或需外部环境 | M3/F1、F3、CI | 单独提交（格式化）或需要 Windows/CI，不适合混进语义提交 |

---

## 3. 六个改进项的优先级

| 用户项 | 优先级 | 放在哪个里程碑 | 说明 |
| --- | --- | --- | --- |
| 1. 单测套件 | **P0** | M0（已完成） | 套件、后端矩阵、ASan/UBSan、覆盖率都已落地；余下只有 CI。 |
| 2. timer 改为四叉树 | **P3** | M4 | 已实现并实测（1.16）：pop-min 退化超过门槛，按计划回退，保留二叉堆。 |
| 3. 跑 clang-format | **P2** | M3 | 必须等 P0/P1 功能修复稳定后单独提交；否则格式 diff 会淹没真正的 bug 修复。 |
| 4. 修复函数签名折行 | **P1** | M3 | 新约束要求“单行 <80 且不能折行”；`zn_connect` 等需要 API 重构。 |
| 5. 修复当前可能有的 bug | **P0/P1** | M1/M2 | 只剩 L8（P0）、C3 与 U1；IOCP 已在 wx-miku 上验证（1.12/1.14/1.15）。 |
| 6. 其他文档缺陷修复 | **P0–P3** | 分散 | 纯文档/示例/过时测试放 P3；`test.c` 已删除，`example.c` 的 `"%sn"`/`%p` 已修（1.3）。 |

C1/C2（Clean C、MSVC `/W4`）和 C3（签名 <80 不换行）都是 P1 硬约束；C4 已按 1.10 豁免；
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

### M2 — P1 正确性与平台修复（已完成；只剩 U1 的取舍）

已完成：B2、B5、C1（枚举尾逗号 + 64 位 timer，见 1.7）、C4（`test.c`/`example.c`/
`zn_task.h` 按 C89 通过；`zn_lua.c` 明确豁免，见 1.10）、B6/B7/U4/U6/U9/T3/T4（见 1.3）、
L6/L7/L9（见 1.8）、B3/B4/B13/U10（见 1.10）、Windows 的 `zn_addrinfo`/`zn_task` 残余
缺陷（见 1.14）、C5 + C2 的 MSVC 部分 + Windows 的 `just` 入口（见 1.15）。

C2 的 MSVC `/W4` 部分随 1.15 关闭：`tests\check.bat`、`example.c`、`bench` 三者都在
`cl /W4 /WX` 下干净；clang `-Wconversion` 不是门槛（不在 `-Wall` 里），按约定不修。
“CI 化”按约定指 `just` 一条命令能在 Windows 上跑 example/bench/test，已在 wx-miku 上
逐条验证；GitHub Actions 矩阵不属于 M2（见 2.5，排在 M5 之后）。

剩余：

1. U1（`zn_deque.h`）：修还是删，仍待决定（第 7.5 节）——不阻塞 C3 与 M3 的签名重构；
2. C3（签名 <80 不换行）是 M3 的前置，不在 M2 收尾。

### M3 — 代码精炼（命名 + 签名 + 控制流）+ 全量 clang-format（进行中）

前置：M2 完成、测试绿。

**2026-10-28 范围扩展**：用户把 M3 扩展成"代码精炼"，规范写在
[`notes/refine.md`](notes/refine.md)（命名 / category 表 / 改名表 / 签名压缩 / 控制流 /
注释 / 门限），`AGENTS.md` 已引用。批次：

- **B0（完成）**：`notes/refine.md` + `AGENTS.md` 的「代码精炼」一节。
- **B1（完成）**：37 个纯内部改名 + 9 个 PP 守卫/实现开关按文件名统一（`zn_buffer_h`/`zn_task_h`/`zn_implemented` 等），特性开关改 `ZN_HAVE_*`；删除 0 使用的
  `zn_reverse` 和 `znL_insert`/`znL_remove` 的行尾多余续行符；`znT_newtimer`→`znT_new`
  顺带修掉 81 列。
- **B1b（完成，2026-10-28 用户复核后）**：category 重整 —— **B = backend、P = platform**
  （原 `znP_*` 后端操作全改 `znB_*`，原 `znU_*` 平台函数全改 `znP_*`，U 删除）；F 的
  `register*` 改 `reg*`；`znBC_*`/`znRB_*` → `znZ_*`/`znX_*`；`zn_initialized` →
  `znP_started`。验证：macOS `just check`、`orb just check`（epoll + select）、
  `just check-c89`、`just check-cpp03`、`just smoke` 全绿。
- **B2（完成，2026-10-29）**：`zn_use_*` → `ZN_USE_*`（大写下划线，保留
  `backend`/`platform` 前缀）；同时把三次自包含折叠成单次展开，无条件段去掉开关。
  派生 flag `ZN_USE_BACKEND_SELECT` 没有再保留：select 段用
  `POSIX && !ZN_USE_BACKEND_REACTIVE` 守卫（select 也是无 epoll/kqueue 平台的兜底，
  并不等于用户开关 `ZN_USE_SELECT`）。表见 `notes/refine.md` §1.6。
- **B3**：控制流（44 处**带工作的**裸 `return;`、6 处带工作的循环 `break`/`continue`、
  3 处 `for(;;)`、7 处 bool 形参、5 个 do-while 宏）、一次性 helper 内联、函数前注释压缩。
  按文件/后端分片提交。
- **B4（待裁决）**：公共 API 签名（`notes/refine.md` §2.4 的 A/B/C/D）。用户决定先跳过，
  先把简单的做完。
- **B5**：`.clang-format` 配置改回 + 全量排版（单独提交）。

**2026-10-28 已完成全量 clang-format（B5）。** 实际执行：

- 配置修了 F1：`SortIncludes: Never`、`IncludeBlocks: Preserve`、
  `AlignAfterOpenBracket: Align`；`.clang-format` 从 `.clang-format.new` 恢复，
  `.clang-format.new` 删除。
- 范围：`znet.h`、`zn_buffer.h`、`zn_task.h`、`zn_addrinfo.h`、`zn_deque.h`、`zn_lua.c`、
  `example.c`、`znet.hpp`、`tests/*.c`、`tests/*.h`、`bench/*.c`；排除 `lbind.h`、
  `tests/*.gen.inc`、`*.bat`、`*.lua`、`*.md`。
- 规模：27 个文件，+2454 / −2484 行（`znet.h` 约 2000 行）。格式幂等（每个文件重跑无
  diff）；`tests/tcp_test.c` 里一处宏内注释被 clang-format 反复重排，已手工把注释移到
  `#define` 上方消除歧义。
- 验证：macOS `just check`（kqueue + select）、`orb just check`（epoll + select）、
  `ssh wx-miku 'cd C:\Work\Code\znet; just check'`（IOCP，`/W4 /WX`）、
  `just check-c89`、`just check-cpp03`、`just smoke`、`just test-lua` 全绿。
- 遗留：**28 行 / 21 个签名折行**（公共 API 11 个：`zn_connect`/`zn_send`/`zn_recv`/
  `zn_sendto`/`zn_recvfrom`/`zn_getaddrinfo` + 5 个回调 typedef；其余是 `znB_sendto`×3、
  `znP_ntop`、`znA_makereq`/`znA_ntop`、4 个 `lzn_*`），以及 `zn_closeaccept` 恰好 80 列。折行是
  临时的：B4 按 `notes/refine.md` §2.4 修完之后，G3 才回到 0/0。
- 后续：增加签名/门限检查脚本，接入 `just format-check`；格式化提交若单独做，提交信息写
  “format only” 并加 `.git-blame-ignore-revs`。

### M4 — timer 四叉堆（已实测，按门槛回退；见 1.16）

2026-10-29 完成：基准 → 语义前置（T6/T7）→ property test → 4 叉实现 → 7 轮交替 A/B。
结论是**保留二叉堆**：`cancel` 在 100k 快 2.2 倍，但 `fire`（pop-min，每个定时器必经）
在所有规模都更慢（1k +37% / 10k +9% / 100k +5.5%），超过计划的 ≤5% 门槛，
所以 4 叉实现整体回退（未提交），保留了 T6/T7 修复、property test 和基准。
数据和根因见 `notes/m4-4ary-heap.md` §5、`notes/timers.md` 的"结构演进"。

重新评估的前提（现在都不满足）：① 基准的 `arm` 换成递减 deadline 的最坏情况变体
（现在的 `arm` 全是尾部追加，没测到 sift-up）；② 先试"键拷贝 / 索引堆"这类和叉数
正交的方案（`notes/m4-4ary-heap.md` §8）。

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
| T3 | `timer_root_and_timeout` | 空堆时 `znT_gettimeout` 恰好返回 `ZN_FOREVER` |
| T3/收窄 | `timer_timeout_clamp` | `znS_clampms` 把非 `ZN_FOREVER` 等待钳到 `INT_MAX`，`ZN_FOREVER` 原样保留 |
| T4 | `timer_cancel_updates_root` | 取消根后 `keys[0]` 是新根；清空后 `gettimeout` 为 `ZN_FOREVER` |
| B6 | `epoll_timeout_clamp`（Linux） | 超时 > `INT_MAX` 被钳位，不回绕成负数 |
| B7 | `udp_socket_is_nonblocking`（POSIX） | UDP fd 带 `O_NONBLOCK` |
| U4/U6 | `recv_default_handlers` / `buffcache_alloc_free` | `packet_ud` 初始化；预分配返回实际块数 |
| 死代码复核 | `tcp_select_except_send_error` | except 集上的 pending send 以 `ZN_ERROR` 完成（不是死分支） |
| L6 | `tcp_accept_spurious_ready` / `udp_recvfrom_spurious_ready` | 假就绪回调一次 `ZN_ERROR`，`waitings` 归零，socket 仍可重新 arm |
| L6 | `tcp_accept_error_event` / `udp_recvfrom_error_event` | 错误事件只解除 armed、不关闭 socket，之后仍能 accept/recvfrom |
| L7 | `tcp_listener_is_nonblocking` | 监听 socket 带 `O_NONBLOCK`，过期就绪不会卡住循环 |
| L6/L7 | `tcp_accept_after_aborted_clients` | 20 个 RST 连接之后，健康客户端仍被 accept |
| B9 | `tcp_select_send_spurious_ready` / `tcp_select_recv_spurious_ready` | 就绪后 `EAGAIN` 保持 armed（park），随后同样的就绪完成操作 |
| L8 | 待补 | 两个 fd 同批就绪、回调删除另一个，ASan 干净 |
| L9 | 待补（Windows） | 五处所有权修复仅代码审查，未在 Windows 验证（1.8） |
| B13 | `tcp_send_error` / `tcp_nosigpipe_option` | 对端断开后 send 回调收到错误且进程不被 SIGPIPE 杀死；macOS 上 `SO_NOSIGPIPE` 确实为 1 |
| B3 | `udp_sendto_error` | 发送失败的 `zn_sendto` 返回 `ZN_ERROR`，不是 `ZN_OK` |
| B4 | `udp_sendto_max_payload` | `ZN_MAX_UDPSEND` 可发；`+1` 返回 `ZN_EPARAM` |
| U10 | `tests/test.lua` 第 4 节 | 返回 false 的 accept 关闭监听 socket，后续连接被拒绝 |
| U16/1.14 | `addrinfo_close_state_keeps_worker` / `addrinfo_stop_all` | `zn_closeaddrinfo(S)` 只丢该 state 的请求、worker 仍为 1；`zn_closeaddrinfo(NULL)` 返回调用者（看门狗兜底不挂住），之后能重新起 worker 并完成查询 |
| 1.14 | `tests\check.bat` 的判定 | 每个测试进程必须打印 ` OK`/` SKIP`（否则失败）；`zn_task.h` 用 `cl /W4 /WX` 编译通过 |

---

## 6. 风险与“不要做”的事

1. **不要先跑全量 `clang-format`**：一千行格式 diff 会让后续 bug 修复无法 review。
2. **不要在 timer 语义 bug 未修时换四叉堆**：测试失败时无法区分“堆错”还是“语义错”。
3. **不要为了迁就旧测试改库 API**：过时测试应移植；`test.c` 已删除。
4. **不要用“加一个 NULL 检查”解决 L8**：对象内存可能已被复用；要延迟释放或引入
   代际/活跃检查。
5. **不要在没有 Windows/Linux 环境时声称 IOCP/epoll 已验证**：只写“代码审阅 + 待 CI”。
   （2026-10-28 起 IOCP 已在 wx-miku 上跑过 `tests\check.bat`，见 1.12；换了机器/工具集
   仍要重新验证。）
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
14. **不要用 `TerminateThread`/`pthread_cancel` 给线程池或 DNS worker 收尾**：前者跳过
    `_endthreadex`（CRT 每线程块不回收，线程持有的 critical section/堆锁也不释放），后者在
    取消点之外同样不会归还资源；收尾只能是“置标志 → 唤醒 → join”。语义与代价见 1.14、
    `notes/utils.md`。

---

## 7. 待确认的设计取舍

1. **虚假 `EAGAIN`**：已决策（1.8/1.9）：`EAGAIN` 绝不静默消费操作 —— 有 park 状态的
   TCP 方向保持 armed，没有 park 状态的 accept/recvfrom 报 `ZN_ERROR` 但不关 socket；
   `ECONNABORTED`/`EINTR` 保持 armed。不设重试预算。理由见 `notes/backends.md`。
2. **UDP `zn_sendto` 长度上限**：已决策（B4，1.10）：保留 1200 作为 `ZN_MAX_UDPSEND` 的
   默认值，可在包含 `znet.h` 前覆盖；文档说明“默认不产生 IP 分片”，分片风险归调用者。
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
10. **B13 的 SIGPIPE 策略**：已决策（1.10）：只给 socket 设 `SO_NOSIGPIPE`，不改全局信号
    处理；`_XOPEN_SOURCE=600` 隐藏该常量时用 Darwin 的 ABI 值（`ZN_SO_NOSIGPIPE`），
    并在建 socket 时就设置，避免 connect 完成前的 send 窗口。
