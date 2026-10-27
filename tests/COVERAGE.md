# znet 行覆盖率报告（2026-10-28）

## 结论

`just cov`（macOS，kqueue + select 合并，ASan 关闭、`--coverage` 打开）的结果：

| 平台 / 后端 | `znet.h` 行 | `zn_buffer.h` 行 | 合计 |
| --- | --- | --- | --- |
| macOS：kqueue + select | 93.9%（1007/1072） | 99.2%（241/243） | 94.9%（1248/1315） |
| Linux：epoll + select | 93.6%（923/986） | 99.1%（216/218） | 94.6%（1139/1204） |
| POSIX 合并：kqueue + epoll + select | 93.3%（1123/1204） | 99.2%（241/243） | 94.3%（1364/1447） |

macOS 明细：`znet.h` 函数 100%（92/92）、分支 76.1%（456/599）；
`zn_buffer.h` 函数 100%（26/26）、分支 85.1%（114/134）。
POSIX 合并的分支覆盖为 86.1%（772/897）。

**没有达到 100% 行覆盖。** 以 macOS 的报告计，剩余未执行行是 `znet.h` 65 行、
`zn_buffer.h` 2 行；把 Linux 的 epoll 行加进来做并集后是 `znet.h` 81 行、
`zn_buffer.h` 2 行（多出来的 16 行是 epoll 专有的错误分支，见 §B）。
它们不是“还没写到测试”，而是分属三类当前测试架构无法或不应触发的代码：

1. 只有 Windows/IOCP 才会走到的公共 API 失败清理（9 行）；
2. 需要故障注入或操作系统资源耗尽的错误路径（macOS 49 行；POSIX 并集再加 16 行）；
3. 当前代码结构下不可达的死代码/防御性检查（7 行），以及 `zn_buffer.h` 的 2 行。

本会话新增的测试已经把“正常 API 可触发”的缺口补完；下面逐条说明剩余行为什么没有覆盖。
统计口径、平台差异和验证命令也一并记录。

## 统计口径

- 命令：`just cov`（根目录）→ `tests/justfile` 的 `cov`，先用
  `--coverage -g -O0` 重建每个 suite，再逐个测试进程运行，最后用 `lcov` 合并
  `tests/build/cov-native` 与 `tests/build/cov-select`。
- “行覆盖”是 `lcov` 的 `DA` 行：只统计**当前平台实际编译进二进制**的行。
  macOS 上 `zn_use_backend_iocp` 和 `zn_use_backend_epoll` 不编译，所以 IOCP 和
  epoll 的行不在 macOS 的数字里；Linux 上反之（kqueue 不编译）。跨平台不能直接把
  两边的 `.gcda` 混在一起（编译器版本/格式不同），但两边各自生成的 `.info`
  tracefile 可以在同一台机器上 `lcov --add-tracefile` 合并；上表的“POSIX 合并”
  就是这样得到的（kqueue + epoll + select 的行取并集）。
- `lcov` 的 `inconsistent` 警告来自 `znet.h` 的三次自包含（同一函数在文件里出现多次，
  行号不同），是本项目单头文件结构的已知噪声，不影响 `DA` 统计。
- 分支覆盖只作为参考：本任务的门槛是行覆盖；分支低是因为大量 `&&`/`||` 的短路组合
  没有被穷举，不代表行为错误。

## 本会话补上的覆盖

| 测试 | 覆盖的行为 / 行 |
| --- | --- |
| `tcp_echo_loop` | 从已删除的 `test.c` 迁移：timer 驱动 4 轮 connect、accept 重挂、echo、在 accept 仍 pending 时删除 listener、`waitings` 归零 |
| `tcp_send_error` | 对端关闭后 `send` 失败：`znet.h:1913-1917`（select）与 `2302`（reactive） |
| `tcp_recv_error` | RST 后 `recv` 失败：`1943-1946`、`2319` |
| `tcp_select_oob_recv_error` | select 的 `exceptfds` 派发：`1932-1934`、`2137` |
| `tcp_connect_unreachable` | `connect()` 同步失败：`1856-1857`、`2283-2284` |
| `tcp_recv_result_after_close` | recv 结果在 close 后完成：`2385-2386` |
| `state_newstate_init_failure` | `RLIMIT_NOFILE=0` 使后端初始化失败：`539-540`、`2144`（select）、`2811`（kqueue） |
| `timer_cancel_sift_up` | 手工构造堆，取消节点后最后元素需要上筛：`460` |
| `recv_default_handlers`（扩展） | 默认 packet handler 被真正调用：`zn_buffer.h:356-357` |
| `sendbuffer_merge_oom` | `sendfinish` 合并 pending 时分配失败：`zn_buffer.h:491` |
| `recv_split_header`（扩展） | 慢路径保留不完整头部：`zn_buffer.h:437-439` |

## 剩余未覆盖行与原因

### A. 只有 Windows/IOCP 会走到（9 行）

`675-677`、`693-695`、`746-748`

`zn_send` / `zn_recv` / `zn_recvfrom` 在 `znP_send` / `znP_recv` / `znP_recvfrom`
返回非 `ZN_OK` 时清空借用缓冲区。POSIX 的三个后端里这三个函数**总是返回 `ZN_OK`**
（真正的错误在完成回调里报告），只有 IOCP 的 `WSASend`/`WSARecv`/`WSARecvFrom`
同步失败才会返回错误。macOS/Linux 的覆盖率二进制里这些行永远不会执行。
要覆盖它们必须跑 Windows CI（IOCP），本机不能声称验证。

### B. 需要故障注入 / 资源耗尽（macOS 49 行；POSIX 并集 65 行）

这些行都要求某个系统调用或分配在特定时刻失败。测试套件没有 mock libc，也不打算为了
覆盖率去改写分配器；下面按失败类型分组。

| 行 | 触发条件 |
| --- | --- |
| `440` | `zn_starttimer` 扩容 timer 堆时 `realloc` 失败（OOM） |
| `1816-1817` | select 的 `zn_tcpfromfd` 里 `zn_newtcp` 返回 NULL（OOM） |
| `2222-2223` | reactive 的同一个 OOM 分支 |
| `1846-1847` | select `znP_connect` 的 `socket()` 返回 `fd >= FD_SETSIZE`（需要同时打开约 1024 个 fd） |
| `2000` | select `zn_onaccept` 接受出 `fd >= FD_SETSIZE` 的连接 |
| `2007`、`2011-2012` | `accept()` 返回 EMFILE 等非 EAGAIN 错误（fd 耗尽） |
| `2146-2148` | select 初始化时 `socketpair` 成功但返回的 fd 已 `>= FD_SETSIZE`（需要精确控制 fd 编号） |
| `2726-2729` | kqueue `zn_onaccept` 的 `accept()` 遇到 EMFILE 等错误 |
| `2231-2232` | reactive `zn_tcpfromfd` 的 `znF_register_all` 失败（kqueue/epoll 注册失败） |
| `2287-2288` | reactive `znP_connect` 的 `znF_register_all` 失败 |
| `2415-2416`、`2441-2442` | reactive `znP_listen` / `znP_initudp` 的 `znF_register_in` 失败 |
| `2814-2817` | kqueue `znP_init` 在 `socketpair` 成功后 `kqueue()` 或注册失败（需要精确耗尽 fd） |
| `1980-1981`、`2424-2425` | `bind()` 成功后 `listen()` 失败（ENOBUFS/ENOMEM 等） |
| `2130` | select 的 `select(2)` 本身返回非 EINTR 错误（例如 poll fd 被外部关闭） |
| `2803` | kqueue 的 `kevent(2)` 返回错误 |
| `1879` | select connect 完成后 `getsockopt(SO_ERROR)` 失败（fd 在 select 与回调之间失效） |
| `2062`、`2075` | select UDP 的异常就绪 / `recvfrom` 非 EAGAIN 错误 |
| `2743-2744`、`2759` | kqueue UDP 的 `EV_EOF/EV_ERROR` / `recvfrom` 非 EAGAIN 错误 |
| `2713-2715` | kqueue accept 的 `EV_EOF/EV_ERROR` 分支 |
| `972` | Linux 的 `clock_gettime(CLOCK_MONOTONIC)` 失败（只在 Linux 覆盖率里出现） |
| `2531-2533` | epoll accept 的 `EPOLLERR/EPOLLHUP` 分支 |
| `2543-2546` | epoll `accept()` 遇到 EMFILE 等非 EAGAIN 错误 |
| `2561-2562` | epoll UDP 的 `EPOLLERR/EPOLLHUP` 分支 |
| `2576-2577` | epoll `recvfrom` 非 EAGAIN 错误 |
| `2616` | epoll `epoll_wait(2)` 返回非 EINTR 错误 |
| `2627-2629` | epoll `znP_init` 在 `eventfd` 成功后 `epoll_create`/注册失败（需要精确耗尽 fd） |
| `1835`、`1961`、`2267`、`2405` | 对有效 fd 调用 `close()` 返回错误（POSIX 上不可确定性触发；Linux 的 EINTR 也不保证，且 fd 实际已关闭） |

其中 `state_newstate_init_failure` 已经用 `RLIMIT_NOFILE` 确定性地覆盖了
`znP_init` 的失败分支（`539-540`、`2144`、`2811`），说明“资源类错误”并非完全不能测；
再往下的 fd 编号/SETSIZE/EMFILE 路径需要把进程开到约 1024 个 fd，并依赖
`ulimit`、ASan/LSan 自身打开的 fd，容易变成环境相关的 flaky 测试。因此这里停手，
把边界写清楚，而不是用脆弱的测试换一个数字。

### C. 当前结构下不可达 / 防御性代码（7 行）

| 行 | 为什么不可达 |
| --- | --- |
| `1808-1809` | select 的 `zn_tcpfromfd` 里又一次检查 `fd >= FD_SETSIZE`，但唯一调用者 `zn_onaccept` 在 `1999-2000` 已经先检查并 `close`，不会把这样的 fd 传进来。这是重复的防御检查。 |
| `1906-1908` | select 的 `zn_onsend(tcp, err=1)` 分支。已连接的 TCP 只要发送缓冲区未满，`select` 的 writefds 就会置位；派发顺序是 read→write→except，send handler 总会在 write 轮先完成，轮不到 except 轮。要让 send handler 活到 except 轮，只能让写方向一直不可写，而当前 select 的 `zn_onsend` 在 EAGAIN 时已经清掉 handler（B9），所以今天没有可达路径。 |
| `2114`、`2788` | `znP_dispatch` 的 `default: ;`。`zn_SocketInfo.type` 只会是 `ZN_SOCK_ACCEPT`/`ZN_SOCK_TCP`/`ZN_SOCK_UDP`，三个 case 已穷举。 |
| `zn_buffer.h:423` | `zn_recvfinish` 快路径里 `zn_addlstring` 的 OOM 分支。调用约定保证 `count <= zn_recvsize(b) == ZN_BUFFERSIZE`，且快路径要求 `readed.used == 0`，所以待追加的 `count - used <= ZN_BUFFERSIZE`，一定放得进 `readed` 的内联缓冲区，不会分配。这个检查是防御性的。 |
| `zn_buffer.h:271` | `znBC_putcache` 的“超过最大档位”分支。最大档位是 `1 << 31`（2 GiB），要进入这里必须先成功分配一块 >2 GiB 的缓冲区；测试明确不实际分配 >2 GiB（见 `buffcache_huge_index`），因此实际不可达。 |

## 如何复现

```sh
just cov            # macOS：kqueue + select
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
- 如果项目需要“100% 行覆盖”作为硬门槛，建议对 C 类结构性死代码引入
  `LCOV_EXCL_LINE`（并写清理由），对 B 类资源错误另做 fault-injection 构建
  （可替换 `socket`/`send`/`accept`/`realloc` 的测试桩）。这会把“代码不可达”和
  “环境不可得”分开，而不是靠脆弱的 fd 耗尽测试。
- 分支覆盖 77.8% 不是本任务的目标；真正值得补的是 `znU_error` 映射和
  `zn_buffer` 分帧的组合边界，可在后续单独评估。
