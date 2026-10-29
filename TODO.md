# TODO — znet 待做清单

> 基线：2026-10-29，工作树（`git status` 是当前状态的唯一依据）。
>
> 本文件**只放未完成的事**。已完成 / 已关闭（含“实测否决、不改代码”）的条目、验证矩阵、
> 回归测试索引都在 [`notes/changes.md`](notes/changes.md)；条目 ID 与之共用命名空间，
> 已关闭的 ID 不再分配新含义。研究性结论在
> [`notes/et-vs-lt.md`](notes/et-vs-lt.md)（ET/LT、per-arm 注册、系统调用与吞吐）和
> [`notes/m4-4ary-heap.md`](notes/m4-4ary-heap.md)（timer 堆结构与键拷贝）。
>
> 定位用函数名，行号只作提示（多处已漂移，见 F3）。验证以 `just check`（macOS
> kqueue + select）、`orb just check`（epoll + select）、`wx-miku just check`（IOCP，
> `cl /W4 /WX`）三条矩阵为准；当前矩阵与覆盖率见 `notes/changes.md` §4。

## 0. 优先级总览

- **P1**：C3/B4、U1、B8 —— 硬约束（签名 <80）、工具头不能编译、监听失败路径留下
  stale 表项。
- **P2**：L10、B14、L11、U15、T5、U8、U11、F3、CI —— 健壮性、资源边界、误导性
  文档/测试。
- **P3**：B3、M5、format-check、覆盖率门槛 —— 清理与收尾，不动语义。
- **决策**：见 §5。D3/D4 方向已定（操作结构体重构，不阻塞 C3 的非 API 部分先做），
  D5 决定 U1，D1/D2 不影响顺序。

---

## 1. 内存安全与生命周期

- [ ] **L11**（P2）addrinfo 的迟到 post 与失败被忽略
  位置：`zn_addrinfo.h` 的 `znA_clearreq`/`znA_callback`。
  不在 `zn_close` 前调用 `zn_closeaddrinfo` 时，迟到的 post 可能触碰已释放 state；
  `znA_clearreq` 忽略 `zn_post` 失败，请求和它的 retain 会泄漏。
  建议：post 返回值要处理（失败就本地回收并 release）；state 侧加“已关闭”判据，
  或在 close 里保证 worker 的请求已排空。

- [ ] **B8**（P1）reactive `znB_listen` 先注册 fd 再 bind/listen
  位置：`znet.h` reactive 的 `znB_listen`。
  注册成功但 bind/listen 失败时只 `close(fd)`，不 `znF_unregister`：`S->infos[fd]`
  留下指向 accept 对象的 stale 指针，fd 被复用时别名到别的对象（epoll 在 Linux 上
  `close` 会顺带删表项，但 `infos[]` 不会）。
  建议：失败路径统一走 `znB_closeaccept`（或先 bind/listen 再注册），并补一个
  “listen 失败后同一 fd 号可再次 listen”的回归。

- [ ] **B14**（P2）IOCP close 时在途 connect 的完成事件
  位置：`znet.h` IOCP 的 `znB_closetcp` / `zn_connect`。
  `zn_close` 开始时仍有一个在途 connect，它的完成事件还没被取出；这期间 `zn_connect`
  返回 `ZN_EBUSY`、`S->waitings` 仍是 1，不变量 6（close 后可以重新 connect）在 IOCP 上
  要等一次 poll 才成立。POSIX 的 close 会取消 armed 操作。
  建议：给完成事件加代际/取消（`CancelIoEx`），单独定；不要和已修的 L8 捆绑。

- [ ] **L10**（P2）mempool 的 assert 在 `NDEBUG` 下消失
  位置：`znet.h` 的 `znM_*`（`assert(((sp - 1) & sp) == 0)` 那组，约 303–305 行）。
  非法 `size` 在 release 构建里会静默破坏内存。注意这是**生产配置**，不是测试专用。
  建议：把入口校验改成显式失败（返回 NULL 并让调用者在公共 API 报 `ZN_EPARAM`），
  或加一个编译期/运行期双保险；不要只留 assert。

- [ ] **T5**（P2）timer 堆只增不缩、无数量上限
  位置：`znet.h` 的 `znT_resize`（只 ×2）与 `znT_new`。
  一次突发把堆撑大后内存常驻；也没有“最多 N 个 timer”的契约。
  建议：要么 shrink（`used` 低于 size/4 时 realloc），要么文档化“容量只增”并给上限；
  前者会动 realloc 失败路径，后者是契约改动——先定哪个。

- [ ] **U15**（P2）Lua 绑定停止接收后留下空转的注册 fd
  位置：`zn_lua.c` 的 `lzn_onrecvfrom`。
  回调返回假值时只 unref，不关闭也不注销 fd；reactive 的注册是 per-listen，剩下的
  datagram 会让 epoll/kqueue 空转（与 U10 同类，U10 已修）。
  建议：先定“停止接收”的语义——关闭 socket 会连带毁掉 `sendto`；改成 per-operation
  注册又会动 reactive 模型。定下来再改，别先写实现。

## 2. 公共 API 与代码精炼

- [ ] **C3 / B4**（P1）函数签名 <80 且不折行
  现状（2026-10-29 实测）：全树的折行/超列签名共 **36 处**——`znet.h` 17
  （10 个公共 API 的声明 + 定义、`znB_sendto` ×3、4 个回调 typedef）、`zn_addrinfo.h` 6
  （`zn_getaddrinfo` 3 + `znA_makereq` + `znA_ntop` + 1 个 typedef）、`zn_lua.c` 4、
  `bench/*.c` 7（6 个回调 + `bench_measure`）、`example.c` 2（`on_udp_recv` 的声明与
  定义）。`znP_ntop` 已修（77 列单行）；`zn_RecvFromHandler` 实测即使把参数全改成单字母
  也只能压到 ~101 列，靠改名到不了 <80。
  公共 API：`zn_connect`/`zn_send`/`zn_recv`/`zn_sendto`/`zn_recvfrom`（声明 + 定义各
  一次）、`zn_getaddrinfo`（3 处）；内部：`znB_sendto` ×3、`znA_makereq`/`znA_ntop`、
  4 个 `lzn_*` 回调。
  `zn_connect` 必须改 API，不能只折行；方向见 D4，**前置是操作结构体重构**（D3）：
  handler/udp/handler 收进操作体之前，这些签名压不到 <80。做完后
  `notes/changes.md` §2 记的折行遗留才归零。
  验收：脚本按“整行含返回类型和 `ZN_API`”检查，`just format-check` 通过（见 §4）。

- [ ] **B3**（P3）控制流精炼（`notes/refine.md` §5）
  44 处**带工作的**裸 `return;`、6 处带工作的循环 `break`/`continue`、3 处 `for(;;)`、
  7 处 bool 形参、5 个 `do {} while (0)` 宏；外加一次性 helper 内联与函数前注释压缩。
  机械但量大，按文件/后端分片提交；每片跑 `just check`，闭包/记账相关的片加 `just cov`。

## 3. 可选工具与绑定

- [ ] **U1**（P1）`zn_deque.h` 修还是删（§5 的决策项）
  三个独立缺陷：① `zn_popfront`/`zn_popback` 在 `znD_frontU`/`znD_backU` 定义之前就
  引用它们，头文件**不能编译**；② POSIX `zn_deldeque` 在 while 循环之后又
  `pthread_mutex_unlock` 一次（此时未持锁，UB）；③ `pthread_cond_timedwait` 收到的是
  相对 `timespec`，正的 `waitms` 会立即超时。当前无人使用。
  建议：删掉（保留 `notes/utils.md` 的记录）比修便宜；若要留，三处都要补测试。

- [ ] **U8**（P2）`znet.hpp` 的 `TcpAccept` 把 client 存在 accept 对象里
  连续 accept 会覆写同一个 `TcpSocket`；handler 运行一次即被消费，容易误用。
  建议：把 client 作为参数传出去（或在 wrapper 层做“每次 accept 一个 shared_ptr”），
  并补一个 C++ 侧的连续 accept 用例。

- [ ] **U11**（P2）`zn_deltaskpool` 遇到永久阻塞的任务会永久阻塞
  位置：`zn_task.h`。
  强杀已否决（`TerminateThread`/`pthread_cancel` 跳过线程清理，还会留下被持有的锁，
  见 `notes/changes.md` §1.14）。要么文档化“任务必须能自行返回”，要么做协作式取消
  （任务自检标志）。不要引入第三种半强杀的方案。

## 4. 文档、验证与 CI

- [ ] **F3**（P2）行号与过时注释
  ① 本文件以外的文档里行号已漂移：`notes/backends.md` 自己的提示写着“见 TODO F3”；
  实测例子：L10 现在 `znet.h:303–305`（旧文写 316）、B8 现在 reactive `znB_listen`
  （旧文写 2352）。`notes/changes.md` §4 的“22 处折行签名”也已过时（实际 36 处，见 C3）。
  建议：统一改成“函数名 + 可选行号”，计数只在 §4 保留一处。
  ② 已清理（2026-10-29）：`tests/` 里描述“未实现/expected failure”的注释共 **5** 处
  （原记 4 处，漏了 `post_returns_ok_epoll`/B1），连同 `state_test.c` 的文件头段落与
  小节标题一起改成回归注释，见 `notes/changes.md` §1.21。
  ③ 仍未清理：`tests/` 里 6 处引用**已关闭**的 ID（`state_test.c` T3×2、`timer_test.c`
  T3/T4/B6、`buffer_test.c` U4、`dispatch_test.c` L1）。它们不会让人误判“有 bug”，但
  照 ID 去查会落空；和 ① 一起做，改成函数名或 `changes.md` 章节号。
  另：`bench/zn_bufferpool.h` 在 HEAD 上就不过 `clang-format --dry-run --Werror`
  （空行、`# define … user_data; ` 行尾空格、指针写法），格式化提交要单独做、`git diff -w`
  为空。

- [ ] **M5**（P3）文档与示例收尾
  更新 `README.txt` 的约束矩阵；补 `scripts/check.sh` 或等价入口，写明本机验证范围。
  `test.c` 已删除、`just smoke` 只跑 `example.c`（见 `notes/changes.md` §1.2），不用再做。
  与 F3 合并做，避免同一批文件改两遍。

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

## 5. 待定的设计取舍（先拍板，再动手）

- [ ] **D1** `zn_buffer` 的默认 packet handler：默认丢弃（现状）还是默认收包？
  推荐默认收包——现在 `zn_recvfinish` 的默认行为是“静默丢数据”，太容易踩。
  改动会动默认值 + `README.txt` + 一个回归用例。

- [ ] **D2** 公共 API 的列对齐：formatter 化后是否保留 `zn_connect (` 的空格？
  二选一，写进 `AGENTS.md`，不要每改一行都手工对齐。

- [ ] **D3** 签名 <80 的计量口径：整行含返回类型和 `ZN_API`（现状，严格执行），
  还是只约束“函数名 + 参数列表”？后者能让部分函数少改，但 `zn_connect` 仍要重构。
  这条决定 C3 的工作量，先定再开工。
  **2026-10-29 方向（暂不裁决）**：先不做口径选择。后续把操作收进 `struct TcpOp`
  之类的操作体（handler + userdata + 方向/长度/状态），签名的参数个数自然减少，口径
  等操作体落地后再定；在那之前 C3 只做非 API 的部分（`bench/`、`example.c`、内部
  `znB_sendto`/`znP_ntop`/`znA_*`/`lzn_*`），公共 API 的 5 个入口留给操作体重构。

- [ ] **D4** `zn_connect` 的重构方向：参数结构体 / 把 `packet` 移到 `zn_newtcp` /
  新增 `zn_connectex`。要评估源码/ABI 兼容性（这是公共 API，1.7 的 `zn_Time` 已经做过
  一次刻意的 ABI 变更，不宜再随手加）。
  **2026-10-29 结论（否决 `zn_connectex`）**：新增薄封装治标不治本——`zn_connectex`
  自己也会超宽，且多一个入口就多一份 ABI。方向是操作收进 `TcpOp` 后直接改
  `zn_connect` 的参数（`type`/`packet` 进操作体），不保留旧入口的薄封装；这一项和 D3
  是同一个重构，等它排期时一起做，ABI 只变一次。

- [ ] **D5** `zn_deque.h`：修好并纳入测试，还是标记为不支持/删除？
  当前没有使用者。删掉最便宜（`notes/utils.md` 保留记录）；要留则三处缺陷都要修并补测试
  （见 U1）。这条决定 U1 的走向。

## 6. 明确不做（记录，避免重复提案）

| 提案 | 结论 | 依据 |
| --- | --- | --- |
| timer 四叉堆（M4） | 已实现并实测，`fire` 全面退化超过 5% 门槛 → 回退，保留二叉堆 | `notes/changes.md` §1.16、`notes/m4-4ary-heap.md` §5 |
| M6：reactive 改 per-arm 注册 | **前提不成立**，代码 2016 年起就是每连接注册一次；加回去 macOS 慢 11%、Linux 慢 32% | `notes/et-vs-lt.md` §3 |
| ET → 电平触发（LT） | 吞吐基本等价、CPU 仅 -3%，burst 上 ET 反而更省；LT 也省不掉 latch | `notes/et-vs-lt.md` §4 |
| 32 位平台 timer 回绕 | 文档化限制（49.7 天）；64 位用 `size_t`，不引入 `ZN_USE_64BIT_TIMER` | `notes/changes.md` §1.7 |
| 删掉 IOCP 每条完成的 `GetOverlappedResult` | 77k/s 下是白白的函数调用，但替代品是未文档化的 `Internal` 字段，不值当 | `notes/et-vs-lt.md` §2 |
| 用 `TerminateThread`/`pthread_cancel` 收尾线程池/DNS worker | 跳过线程自己的清理、留下被持有的锁；只能“置标志 → 唤醒 → join” | `notes/changes.md` §1.14 |
| 用 fd 耗尽/信号关闭凑覆盖率 | 脆弱且不可归因 | `tests/COVERAGE.md` |
| 先跑全量 `clang-format` 再修 bug | 已完成（B5）；此后格式化提交必须单独、`git diff -w` 为空 | `notes/changes.md` §2 |

## 7. 优先级评估与执行顺序

排序依据：① 是否崩溃/内存安全；② 是否违反硬约束（AGENTS 的 <80 与 Clean C）；
③ 是否让后续工作建立在错误信息上（过时文档/测试）；④ 成本与前置依赖。

已完成的批次记在 `notes/changes.md`（最近一次是 §1.21）；这里只剩未做项，顺序如下：

- **F3（①③，P2）**：文档里的行号与已关 ID 会把人引向已修的 bug，成本最低。前置：无。
- **C3（P1）**：硬约束，且挡住 `format-check`；非 API 部分现在就能清，公共 API 那 5 个
  入口等操作结构体重构。前置：API 部分等 D3/D4。
- **U1（P1）**：头文件编不过；选“删”几乎是分钟级，留着就得修 3 个缺陷。前置：D5。
- **B8（P1）**：stale `infos[]` + 未注销，影响所有 listen 失败路径；修法小而确定。
  前置：无。
- **U15（P2）**：reactive 空转（U10 同类已修）；先定“停止接收”语义。前置：语义先定。
- **L10（P2）**：`NDEBUG` 下内存破坏，影响发布配置；5 处 `znM_init` 都是编译期常量尺寸，
  修法小而可测。前置：无。
- **L11（P2）**：生命周期 + retain 泄漏，和 `zn_close` 的契约纠缠。前置：无。
- **B14（P2）**：IOCP 完成事件所有权，与已修的 L8 不同族；close 后立刻 reconnect 会拿到
  `ZN_EBUSY`。前置：无。
- **T5（P2）**：资源边界；先定 shrink 还是文档化上限。前置：策略先定。
- **U8 / U11（P2）**：wrapper 与工具的可用性；U11 只需文档化最小改动。前置：无。
- **CI（P2，独立）**：现在全靠三台机器手工跑，可与前四项并行。前置：无。
- **B3（P3）**：纯精炼，量大且机械；放语义修复之后，免得 diff 淹没 bug。前置：C3 的
  API 决策。
- **M5 + format-check（P3）**：收尾；与 F3 合并做文档，检查脚本在 C3 之后才有意义。
  前置：C3。
- **覆盖率门槛（P3）**：依赖 CI 或 fault-injection 构建。前置：CI。

如果只做三件事：F3（①③）→ C3 的非 API 部分 → U1。下一个迭代建议前四项（F3、C3、U1、
B8），一次只碰一个契约面；CI 作为并行轨道，别等全部修完才铺。
