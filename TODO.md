# TODO — znet 待做清单

> 基线：2026-11-01，工作树（`git status` 是当前状态的唯一依据）。
>
> 本文件**只放未完成的事**。已完成 / 已关闭（含“实测否决、不改代码”）的条目、验证矩阵、
> 回归测试索引都在 [`notes/changes.md`](notes/changes.md)；条目 ID 与之共用命名空间，
> 已关闭的 ID 不再分配新含义。研究性结论在
> [`notes/et-vs-lt.md`](notes/et-vs-lt.md)（ET/LT、per-arm 注册、系统调用与吞吐）和
> [`notes/m4-4ary-heap.md`](notes/m4-4ary-heap.md)（timer 堆结构与键拷贝）。
>
> 定位用函数名（笔记与本文都不再引用行号）。验证以 `just check`（macOS
> kqueue + select）、`orb just check`（epoll + select）、`wx-miku just check`（IOCP，
> `cl /W4 /WX`）三条矩阵为准；当前矩阵与覆盖率见 `notes/changes.md` §4。

## 0. 优先级总览

- **P1**：C3/B4、U1 —— 硬约束（签名 <80）、工具头不能编译。**2026-10-31 决定暂缓**
  （折行与 deque 先不动），条目保留为未完成。
- **P2**：U11、Q1、Q2、CI —— 健壮性、资源边界、测试与文档。
- **P3**：M5、format-check、覆盖率门槛 —— 清理与收尾，不动语义。（B3 已完成，见
  `notes/changes.md` §1.41）
- **决策**：见 §5。D3/D4 随 C3 暂缓，D5 随 U1 暂缓；D1 已关闭（不装 packet handler
  时丢帧是设计，`notes/changes.md` §1.42）、D2 已定（见 `notes/changes.md` §3）。

---

## 1. 内存安全与生命周期

## 2. 公共 API 与代码精炼

- [ ] **C3 / B4**（P1）函数签名 <80 且不折行
  现状（2026-10-31，`int err` 之后，`znet.h` 折行 16 处）：`zn_connect` 的工厂形实测
  **105 列**（地址/端口/回调/ud 四个参数 + 返回类型），仍在折行；`zn_send`/`zn_recv`/
  `zn_sendto`/`zn_recvfrom` 的声明与定义（85–100 列）、2 个回调 typedef
  （`zn_AcceptHandler` 81、`zn_RecvFromHandler` 113）、`zn_addrinfo.h` 的 6 处、
  `zn_lua.c` 的 4 处、`bench/*.c` 的 7 处照旧。`zn_SendHandler`/`zn_RecvHandler` 已随
  `int err` 回到单行（76）；`zn_RecvFromHandler` 即使把参数全改成单字母也只能压到
  ~98 列，靠改名到不了 <80。
  公共 API：`zn_connect`/`zn_send`/`zn_recv`/`zn_sendto`/`zn_recvfrom`（声明 + 定义各
  一次）、`zn_getaddrinfo`（3 处）；内部：`znB_sendto` ×2、`znS_onudp`（85 列，v2 新增，
  与 `zn_RecvFromHandler` 同源，等回调收窄后一起解决）、`znA_makereq`/`znA_ntop`、
  4 个 `lzn_*` 回调。
  `zn_connect` 必须改 API，不能只折行；方向见 D4，**前置是操作结构体重构**（D3）：
  handler/udp/handler 收进操作体之前，这些签名压不到 <80。做完后
  `notes/changes.md` §2 记的折行遗留才归零。
  验收：脚本按“整行含返回类型和 `ZN_API`”检查，`just format-check` 通过（见 §4）。

## 3. 可选工具与绑定

- [ ] **U1**（P1）`zn_deque.h` 修还是删（§5 的决策项）
  三个独立缺陷：① `zn_popfront`/`zn_popback` 在 `znD_frontU`/`znD_backU` 定义之前就
  引用它们，头文件**不能编译**；② POSIX `zn_deldeque` 在 while 循环之后又
  `pthread_mutex_unlock` 一次（此时未持锁，UB）；③ `pthread_cond_timedwait` 收到的是
  相对 `timespec`，正的 `waitms` 会立即超时。当前无人使用。
  建议：删掉（保留 `notes/utils.md` 的记录）比修便宜；若要留，三处都要补测试。

- [ ] **U11**（P2）`zn_deltaskpool` 遇到永久阻塞的任务会永久阻塞
  位置：`zn_task.h`。
  强杀已否决（`TerminateThread`/`pthread_cancel` 跳过线程清理，还会留下被持有的锁，
  见 `notes/changes.md` §1.14）。要么文档化“任务必须能自行返回”，要么做协作式取消
  （任务自检标志）。不要引入第三种半强杀的方案。

## 4. 文档、验证与 CI

- [ ] **M5**（P3）文档与示例收尾
  更新 `README.md` / `README.zh.md` 的约束矩阵；把本机的 `notes/gate.sh`（gitignored）落成仓库里的
  `scripts/check.sh` 或等价入口，写明本机验证范围。
  `test.c` 已删除、`just smoke` 只跑 `examples/example.c`（见 `notes/changes.md` §1.2），不用再做。
  另有格式收尾：`bench/zn_bufferpool.h` 在 HEAD 上就不过 `clang-format --dry-run --Werror`
  （空行、`# define … user_data; ` 行尾空格、指针写法），格式化提交要单独做、`git diff -w`
  为空。还可以顺手评估删掉 `tests/state_test.c` 里已冗余的 `state_anchor`（它只为 select 的
  nfds bug 存在，删它要动若干 `assertok(state_anchor(S))`）。

- [ ] **CI 矩阵**（P2，独立工作流）
  GitHub Actions 的 ubuntu / macos / windows 三矩阵。Windows job 需要 `cl /W4 /WX` 的
  C 与 C++ 编译、IOCP 测试、以及 `misc/gen_entries.lua` 依赖的 lua。它不阻塞上面的
  顺序，但越早越好：现在全靠本机 + `orb` + wx-miku 手工跑，换机器/工具集就会漏。

- [ ] **签名/门限检查脚本**（P3）接入 `just format-check`
  把“函数签名单行 <80”“`clang-format` 幂等”“没有 `// clang-format off` 滥用”做成脚本；
  格式化提交另加 `.git-blame-ignore-revs`。C3 收尾后这个脚本才是回归门。

- [ ] **覆盖率门槛决策**（P3）
  当前报告解释了剩余行为什么不可覆盖。若要把 100% 行覆盖当硬门槛：结构性死代码用
  `LCOV_EXCL_LINE`，资源错误做 fault injection 构建，**不要**用 fd 耗尽/信号关闭等脆弱
  手段（见 `tests/COVERAGE.md`）。

- [ ] **Q1**（P2）测试不该直接调内部函数：改成公开 API 断言，或删掉
  原则（2026-10-31 用户提出）：单测调用/测试内部函数是不合理的，**更不能因为单测可调
  而保留一个本该内联的 helper**（`znS_clampms` 已因此删掉，见 `changes.md` §1.31）。
  现状还剩两处直接调内部函数的地方，都要落成"公开 API 可达的行为"或删（v2 引入的
  白盒用例是另一批，见 Q2）：
  ① `tests/addr_test.c` 整个文件调 `znP_parseaddr`/`znP_setinfo`/`znP_ntop`/`znP_error`/
  `znP_family`/`znP_isv6`/`znP_size`。解析失败/IPv6/peer 文本可以用
  `zn_listen`/`zn_connect`/`zn_sendto`/`zn_connectudp` + `zn_lasterror` +
  `zn_getpeerinfo` + recvfrom 回调的 `addr/port` 复现；`znP_family`/`znP_isv6`/
  `znP_size` 的返回值不是 API 可观测的，只能删断言或整个删文件。
  ② `tests/timer_test.c` 的 `znT_gettimeout` 断言（`timer_root_and_timeout` 的 T3、
  `timer_cancel_updates_root`、`timer_64bit_delay`）。T3 的修法：空堆的 idle state 调
  `zn_run(LOOP)` 必须立即返回 0（`state_run_check_empty` 已覆盖）；精确 delta 只剩
  "timer 不早于 deadline 触发"这一条可观测行为。
  代价：②会削掉 T3/T4 的精确回归点，所以先记着，等排到 timer/测试批次时一起改。
  判据：测试文件里不出现 `znT_`/`znS_`/`znB_`/`znR_`/`znP_`/`znF_` 调用（读 `S->` 状态
  的断言不在此列，它们是泄漏/记账的代理）。

- [ ] **Q2**（P2）v2 白盒用例与三处"测试自己会放宽"的地方
  2026-10-31 审计登记（用户决定"先记录、后面再改"），等测试批次一起处理：
  ① v2 新增/改写的用例大量直接调内部函数，与 Q1 的判据冲突：`tcp_test.c` 的
  `znS_newtcpfd` / `znB_retryrecv` / `znB_retrysend` / `znB_onsend` / `znB_onrecv` /
  `znR_process` / `znP_parseaddr` / `znP_setnonblock`，`resultqueue_test.c` 整套靠
  `znR_add`/`znR_process`，`dispatch_test.c` 与 `ztest.h` 的 `zt_dispatch_*` 直接调
  `znB_onaccept`/`znB_onrecvfrom`，`lifecycle_test.c` 用 `znS_valid` 与 `S->active_*`；
  `tcp_close_both_parked` 更是手工造对象。其中"就绪不是完成""批次失效""排队结果被压制"
  确实只能靠注入就绪/手工入队构造，所以要么改成公开 API 可观测的行为，要么把它们写进
  Q1 的豁免（现在 Q1 只列了 addr/timer 两处，看上去像漏了）。
  ② 两处核心回归藏在 `test_skip` 后面（runner 把 SKIP 算通过，`just check` 不会红）：
  `tcp_close_blocked_send` 的"填不满 socket buffer"、`resultqueue_test` 四个批量用例
  （`rq_batch_limit`/`rq_process_all`/`rq_close_with_many_queued`/
  `rq_poll_returns_one_when_backlog`）的"120 条 loopback 连接建不起来"。要么改成硬失败
  并把环境要求写进 `tests/README.md`，要么让用例不依赖环境。
  ③ `asserteq(zt_run(S), 0)` 恒真（`zn_run(ZN_RUN_LOOP)` 只可能返回 0），新加的
  `zt_pair_open` / `close_phase` 里也有；真正起作用的是 watchdog 的终止性。
  另外 `resultqueue_test` 从 dummy socket 改成 120 条真实 loopback 连接，依赖 fd 与
  临时端口，每个对象还要 pump 一次 loop；`znS_newtcpfd(socketpair())` 是 L2 用例里
  现成的省法（而它本身就是 ① 要处理的内部调用）。

## 5. 待定的设计取舍（先拍板，再动手）

> D2（公共 API 的列对齐）已定：交给 `.clang-format` 的 `AlignConsecutiveDeclarations`，
> 不手工在 `(` 前补空格；`AGENTS.md` 的“约定”一节与 `notes/changes.md` §3 记了结论。

- [ ] **D3** 签名 <80 的计量口径：整行含返回类型和 `ZN_API`（现状，严格执行），
  还是只约束“函数名 + 参数列表”？后者能让部分函数少改，但 `zn_connect` 仍要重构。
  这条决定 C3 的工作量，先定再开工。
  **2026-10-29 方向（暂不裁决）**：先不做口径选择。后续把操作收进 `struct TcpOp`
  之类的操作体（handler + userdata + 方向/长度/状态），签名的参数个数自然减少，口径
  等操作体落地后再定；在那之前 C3 只做非 API 的部分（`bench/`、`examples/example.c`、内部
  `znB_sendto`/`znP_ntop`/`znA_*`/`lzn_*`），公共 API 的 5 个入口留给操作体重构。

- [ ] **D4** `zn_connect` 的重构方向：参数结构体 / 把回调收进操作体 / 新增
  `zn_connectex`。要评估源码/ABI 兼容性（这是公共 API，1.7 的 `zn_Time` 已经做过
  一次刻意的 ABI 变更，不宜再随手加）。
  **2026-10-29 结论（否决 `zn_connectex`）**：新增薄封装治标不治本——`zn_connectex`
  自己也会超宽，且多一个入口就多一份 ABI。方向是操作收进 `TcpOp` 后减少
  `zn_connect` 的参数，不保留旧入口的薄封装；这一项和 D3 是同一个重构，等它排期时
  一起做，ABI 只变一次。
  **2026-10-30 更新**：`type`/`packet` 已随 U17 删除，`zn_connect` 只剩
  addr/port/cb/ud；lifecycle v2 把它改成工厂（第一个参数从 `zn_Tcp *` 变成 `zn_State *`），
  实测 105 列，仍折行。C3 的公共 API 部分继续等操作体。

- [ ] **D5** `zn_deque.h`：修好并纳入测试，还是标记为不支持/删除？
  当前没有使用者。删掉最便宜（`notes/utils.md` 保留记录）；要留则三处缺陷都要修并补测试
  （见 U1）。这条决定 U1 的走向。

## 6. 明确不做（记录，避免重复提案）

| 提案 | 结论 | 依据 |
| --- | --- | --- |
| timer 四叉堆（M4） | 已实现并实测，`fire` 全面退化超过 5% 门槛 → 回退，保留二叉堆 | `notes/changes.md` §1.16、`notes/m4-4ary-heap.md` §5 |
| M6：reactive 改 per-arm 注册 | **前提不成立**，代码 2016 年起就是每连接注册一次；加回去 macOS 慢 11%、Linux 慢 32% | `notes/et-vs-lt.md` §3 |
| 只把 UDP/listener 的注册改成 ET（或靠 re-arm 的 MOD/ADD “踢一脚”） | 不采用“单独 ET”：会丢“数据已在缓冲、之后才 arm”的唤醒。已实现的是 ET + arm 时 syscall + `can_*` latch + 结果入队（§1.38）；“踢一脚”仍然不采用（未文档化的内核行为） | `notes/et-vs-lt.md` §2、`notes/changes.md` §1.38 |
| ET → 电平触发（LT） | 吞吐基本等价、CPU 仅 -3%，burst 上 ET 反而更省；LT 也省不掉 latch | `notes/et-vs-lt.md` §4 |
| 32 位平台 timer 回绕 | 文档化限制（49.7 天）；64 位用 `size_t`，不引入 `ZN_USE_64BIT_TIMER` | `notes/changes.md` §1.7 |
| 删掉 IOCP 每条完成的 `GetOverlappedResult` | 77k/s 下是白白的函数调用，但替代品是未文档化的 `Internal` 字段，不值当 | `notes/et-vs-lt.md` §2 |
| 用 `TerminateThread`/`pthread_cancel` 收尾线程池/DNS worker | 跳过线程自己的清理、留下被持有的锁；只能“置标志 → 唤醒 → join” | `notes/changes.md` §1.14 |
| 用 fd 耗尽/信号关闭凑覆盖率 | 脆弱且不可归因 | `tests/COVERAGE.md` |
| 让 `zn_close` 兜底“忘了调用 `zn_closeaddrinfo`”（排空 `waitings` / close hook） | 都要改 close 契约：前者让 close 阻塞一次解析，后者加公共 API；而“`zn_close` 之后再用 `S`”本来就是用户 UAF | `notes/changes.md` §1.24、`notes/utils.md` |
| T5：timer 堆 shrink / 数量上限 | 本库的内存模型就是“分配不返还”（mempool、缓冲、堆都只增），timer 堆不单独破例 | 2026-10-30 用户决定；`notes/timers.md` |
| B8：reactive `znB_listen` 失败路径没 `znF_unregister` | **不是 bug**：`S->infos[]` 是 select 的表（select 的 listen 从不注册），close 会移除注册（有 man page 保证），批内失效路径不可达。顺手把注册挪到 bind/listen 之后，与 IOCP/select 同构——零代价的一致性同步，不是修 bug | `notes/changes.md` §1.26、`notes/backends.md` |
| 先跑全量 `clang-format` 再修 bug | 已完成（B5）；此后格式化提交必须单独、`git diff -w` 为空 | `notes/changes.md` §2 |

## 7. 优先级评估与执行顺序

排序依据：① 是否崩溃/内存安全；② 是否违反硬约束（AGENTS 的 <80 与 Clean C）；
③ 是否让后续工作建立在错误信息上（过时文档/测试）；④ 成本与前置依赖。

已完成的批次记在 `notes/changes.md`（最近一次是 §1.38）；这里只剩未做项。按 2026-10-31
的决定，**折行（C3/B4、D3/D4）与 deque（U1、D5）暂不排期**，下面的顺序只排剩下的：

- **Q2（P2）**：测试里的白盒调用、`test_skip` 盖住的回归、恒真断言；它决定后面看到的绿灯
  是不是真的。前置：无。
- **Q1（P2）**：addr/timer 的用例直调内部函数；和 Q2 一起按测试批次做。前置：无。
- **U11（P2）**：工具的可用性；只需文档化最小改动。前置：无。
- **CI（P2，独立）**：现在全靠三台机器手工跑，可与上面的项并行。前置：无。
- **M5 + format-check（P3）**：收尾；格式化幂等与现有门限现在就能做，签名项等 C3。
  前置：无（签名项等 C3）。
- **覆盖率门槛（P3）**：依赖 CI 或 fault-injection 构建。前置：CI。

如果只做三件事：Q2 → U11 → Q1。C3（折行）与 U1/D5（deque）按 2026-10-31 的决定
暂缓，等折行/工具头的批次再开；CI 作为并行轨道，别等全部修完才铺。
