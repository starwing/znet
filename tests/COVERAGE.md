# znet 行覆盖率报告（2026-10-28）

## 结论

`just cov`（macOS，kqueue + select 合并；ASan 关闭、`--coverage` 打开）的结果：

| 平台 / 后端 | `znet.h` 行 | `zn_buffer.h` 行 | 合计 |
| --- | --- | --- | --- |
| macOS：kqueue + select | 95.2%（1069/1123） | 99.2%（242/244） | 95.9%（1311/1367） |
| Linux：epoll + select | 95.1%（995/1046） | 99.1%（217/219） | 95.8%（1212/1265） |
| POSIX 合并：kqueue + epoll + select | 95.0%（1202/1265） | 99.2%（242/244） | 95.7%（1444/1509） |

macOS 明细：`znet.h` 函数 100%（97/97）、分支 78.6%（491/625）；
`zn_buffer.h` 函数 100%（26/26）、分支 85.8%（115/134）。
POSIX 合并的分支覆盖为 86.8%（685/789）。

**没有达到 100% 行覆盖。** 以 macOS 的报告计，剩余未执行行是 `znet.h` 54 行、
`zn_buffer.h` 2 行；把 Linux 的 epoll 行加进来做并集后是 `znet.h` 63 行、
`zn_buffer.h` 2 行。它们不是“还没写到测试”，而是分属三类当前测试架构无法或不应触发的
代码：

1. 只有 Windows/IOCP 才会走到的公共 API 失败清理（9 行）；
2. 需要故障注入或操作系统资源耗尽的错误路径（macOS 45 行；POSIX 并集再加 9 行）；
3. 剩下的防御性代码（`znet.h` 0 行；`zn_buffer.h` 2 行，见 §C）。

2026-10-28 的两次修复（`notes/changes.md` §1.3/§1.4）之后，原来的“死代码”7 行已经处理完：三处
`default: ;` 删除、select 的重复 `FD_SETSIZE` 检查改成 assert、select 的 except 派发
send 错误分支用 `tcp_select_except_send_error` 证明可达。同一天的 L6/L7 修复（`notes/changes.md` §1.8）
把 accept/UDP 的 `EAGAIN`、监听器/UDP 的错误事件变成可测分支（4 个新测试），所以行覆盖
从 93.9/94.2/94.6 升到 95.2/95.1/95.0；B9 覆盖了 select 的 send/recv park 路径。
新增但不可确定覆盖的只剩 `EINTR`/`ECONNABORTED` 和 `fd >= FD_SETSIZE`/
`zn_tcpfromfd` 失败这几条（§B）。剩余两行都在 `zn_buffer.h`，
是刻意保留的（§C）。

## 统计口径

- 命令：`just cov`（根目录）→ `tests/justfile` 的 `cov`，先用
  `--coverage -g -O0` 重建每个 suite，再逐个测试进程运行，最后用 `lcov` 合并
  `tests/build/cov-native` 与 `tests/build/cov-select`。
- “行覆盖”是 `lcov` 的 `DA` 行：只统计**当前平台实际编译进二进制**的行。
  macOS 上 `ZN_USE_BACKEND_IOCP` 和 `ZN_USE_BACKEND_EPOLL` 不编译，所以 IOCP 和
  epoll 的行不在 macOS 的数字里；Linux 上反之（kqueue 不编译）。跨平台不能直接把
  两边的 `.gcda` 混在一起（编译器版本/格式不同），但两边各自生成的 `.info`
  tracefile 可以在同一台机器上 `lcov --add-tracefile` 合并；上表的“POSIX 合并”
  就是这样得到的（kqueue + epoll + select 的行取并集）。
- `lcov` 的 `inconsistent` 警告曾来自 `znet.h` 的三次自包含（同一源文件被预处理三次，
  行号不同）。单次展开后这个来源已经没有了。
- `just uncovered` 按函数列出未覆盖行（函数边界来自源码扫描：合并后的 FN 区间会跨到
  无关代码，不能直接用）；`just cov-lines` / `just cov-unbranched` 是平铺的行/分支版本。
- 分支覆盖只作为参考：本任务的门槛是行覆盖；分支低是因为大量 `&&`/`||` 的短路组合
  没有被穷举，不代表行为错误。

## 已补上的覆盖

| 测试 | 覆盖的行为 / 函数 |
| --- | --- |
| `tcp_echo_loop` | 从已删除的 `test.c` 迁移：timer 驱动 4 轮 connect、accept 重挂、echo、在 accept 仍 pending 时删除 listener、`waitings` 归零 |
| `tcp_send_error` | 对端关闭后 `send` 失败：select `zn_onsend` 与 reactive 的 `znR_add` 错误路径 |
| `tcp_recv_error` | RST 后 `recv` 失败：select `zn_onrecv` 与 reactive 的 `znR_add` 错误路径 |
| `tcp_select_oob_recv_error` | select 的 `exceptfds` 派发：`zn_onrecv(tcp, err=1)` |
| `tcp_select_except_send_error` | select 的 `exceptfds` 派发：`zn_onsend(tcp, err=1)`（原来被当成死代码的分支） |
| `tcp_connect_unreachable` | `connect()` 同步失败 |
| `tcp_recv_result_after_close` | recv 结果在 close 后完成 |
| `state_newstate_init_failure` | `RLIMIT_NOFILE=0` 使后端初始化失败 |
| `timer_cancel_sift_up` | 手工构造堆，取消节点后最后元素需要上筛 |
| `timer_cancel_updates_root` | 取消根节点后 `keys[0]` 是新根 |
| `epoll_timeout_clamp` | epoll 的 `int` 超时钳到 `INT_MAX` |
| `recv_default_handlers`（扩展） | 默认 packet handler / `packet_ud` 初始化 |
| `sendbuffer_merge_oom` | `sendfinish` 合并 pending 时分配失败 |
| `recv_split_header`（扩展） | 慢路径保留不完整头部 |
| `tcp_listener_is_nonblocking` | reactive `znP_listen` 的 `znU_set_nonblock`（L7） |
| `tcp_accept_spurious_ready` | 三个后端的 `zn_onaccept` 在 `EAGAIN` 时解除 armed 并回调 `ZN_ERROR` |
| `udp_recvfrom_spurious_ready` | 三个后端的 `zn_onrecvfrom` 同上 |
| `tcp_accept_after_aborted_clients` | 连续 RST 后监听器仍可用（`ECONNABORTED` 若出现则走“保持 armed”） |
| `tcp_accept_error_event` / `udp_recvfrom_error_event` | 监听器/UDP 的错误事件（select exceptfds、`EPOLLERR`、`EV_ERROR`）只解除 armed，socket 不关闭 |
| `tcp_select_send_spurious_ready` / `tcp_select_recv_spurious_ready` | select `zn_onsend`/`zn_onrecv` 的 `EAGAIN` park 路径，以及随后完成（B9） |

## 剩余未覆盖行与原因

### A. 只有 Windows/IOCP 会走到（9 行）

`zn_send` / `zn_recv` / `zn_recvfrom` 在 `znP_send` / `znP_recv` / `znP_recvfrom`
返回非 `ZN_OK` 时清空借用缓冲区。POSIX 的三个后端里这三个函数**总是返回 `ZN_OK`**
（真正的错误在完成回调里报告），只有 IOCP 的 `WSASend`/`WSARecv`/`WSARecvFrom`
同步失败才会返回错误。macOS/Linux 的覆盖率二进制里这些行永远不会执行。
要覆盖它们必须跑 Windows CI（IOCP），本机不能声称验证。

### B. 需要故障注入 / 资源耗尽（macOS 45 行；POSIX 并集 54 行）

这些行都要求某个系统调用或分配在特定时刻失败。测试套件没有 mock libc，也不打算为了
覆盖率去改写分配器。具体行号会随代码漂移，用 `just uncovered` 查当前清单；
按失败类型分组如下。

| 失败类型 | 触发行（函数） |
| --- | --- |
| 分配/扩容失败 | `zn_starttimer`（堆扩容 `realloc`）、各后端的 `zn_tcpfromfd`（`zn_newtcp` 返回 NULL）、reactive 的 `znF_register_all`/`znF_register_in` 失败 |
| fd 耗尽 / `FD_SETSIZE` | select 的 `znP_connect`、`zn_onaccept`、`znP_init`（`socketpair` 编号越界） |
| `accept`/`recvfrom`/`send` 的系统错误 | 各后端的 `zn_onaccept`（EMFILE 等）、`zn_onrecvfrom`（非 EAGAIN）、`zn_onrecv`/`zn_onsend` 的 `znU_error` 分支；`EINTR`/`ECONNABORTED` 的“保持 armed”分支（构造不出确定性的信号/半开连接） |
| bind/listen/close 失败 | `znP_listen`（`listen` 失败）、`znP_closetcp`/`znP_closeaccept` 的 `close` 失败 |
| 事件循环本身出错 | select `znP_poll` 的 `select(2)` 错误、kqueue `znP_poll` 的 `kevent(2)` 错误、epoll `znP_poll` 的 `epoll_wait(2)` 错误 |
| 状态查询失败 | select `zn_onconnect` 的 `getsockopt(SO_ERROR)` 失败（置 `so_error = 0`） |
| 平台时钟 | Linux `zn_time` 的 `clock_gettime(CLOCK_MONOTONIC)` 失败 |

其中 `state_newstate_init_failure` 已经用 `RLIMIT_NOFILE` 确定性地覆盖了
`znP_init` 的失败分支，说明“资源类错误”并非完全不能测；再往下的 fd 编号/SETSIZE/
EMFILE 路径需要把进程开到约 1024 个 fd，并依赖 `ulimit`、ASan/LSan 自身打开的 fd，
容易变成环境相关的 flaky 测试。因此这里停手，把边界写清楚，而不是用脆弱的测试换一个
数字。

### C. 保留的防御性代码（`znet.h` 0 行；`zn_buffer.h` 2 行）

| 行（函数） | 为什么保留 |
| --- | --- |
| `zn_recvfinish` 快路径的 `return -1` | 唯一的失败条件是累加器扩容失败；快路径要求 `readed.used == 0`，而调用约定保证 `count <= zn_recvsize(b)`，所以尾部一定放得进内联缓冲区。它是数据路径上唯一的错误返回：删掉后若调用方违约（`count` 超过接收缓冲），release 版会静默丢帧。 |
| `znBC_putcache` 的“超过最大档位”分支 | 不是死代码：`zn_resizebuffer` 对 >2 GiB 的请求走回退分配，`b->size` 可以超过 2 GiB；此时必须把块还给同一个分配器，否则泄漏或错误释放。测试不实际分配 >2 GiB 只是不想让 CI 依赖虚拟内存。 |

已经处理掉的旧 C 类：三处 `znP_dispatch` 的 `default: ;`（改为枚举类型 +
`-Wswitch` 保护）、select `zn_tcpfromfd` 的重复 `FD_SETSIZE` 检查（改为 assert，
不变式由调用者建立）、select `zn_onsend` 的 `err` 分支（其实可达，已加回归）。
IOCP 的 `default: ;` 仍在，因为本机不能编译 IOCP，留给 Windows CI 一起处理。

## 如何复现

```sh
just cov            # macOS：kqueue + select
just uncovered      # 按函数列出未覆盖行（= just test uncovered）
just cov-lines      # znet.h 中 DA:...,0 的行号 + 源码
just cov-unbranched # znet.h 中分支计数为 0 的行

orb just cov        # Linux：epoll + select（gcc；数字与 macOS 不可直接比较）
```

覆盖率目录 `tests/build/cov-*` 会被 `cov` 先清空；两个平台的 `.gcda` 不兼容，
必须串行跑，不要把两边的产物混在一起。要复现 POSIX 并集：

```sh
just cov && cp tests/build/znet.info /tmp/znet-macos.info
orb just cov && cp tests/build/znet.info /tmp/znet-linux.info
lcov --add-tracefile /tmp/znet-macos.info --add-tracefile /tmp/znet-linux.info \
     --rc branch_coverage=1 --ignore-errors inconsistent,corrupt \
     --output-file /tmp/znet-merged.info
lcov --list /tmp/znet-merged.info --rc branch_coverage=1 \
     --ignore-errors inconsistent,corrupt
```

## 建议

- 行覆盖率的下一步不是继续堆测试，而是补 Windows CI：IOCP 的 9 行和更多完成路径
  只有在 Windows 上才有意义。
- 如果项目需要“100% 行覆盖”作为硬门槛，剩下的 `zn_buffer.h` 两行应该用
  `LCOV_EXCL_LINE` 加理由，§B 的故障注入路径另做可替换 `socket`/`send`/`accept`/
  `realloc` 的测试桩，而不是靠 fd 耗尽测试。
- 分支覆盖不是本任务的目标；真正值得补的是 `znU_error` 映射和
  `zn_buffer` 分帧的组合边界，可在后续单独评估。
