# ctiming 分析器 `ctiming-analyze`

`ctiming-analyze` 读取运行时导出的 `.ctrace` 文件，在分析器内部配对 ENTER/EXIT、重建每线程调用树，再按函数聚合出耗时统计与调用图。自包含 HTML 查看器读取本工具生成的 `analysis.json`，使用说明见 [viewer.md](viewer.md)。

## 用法

```bash
ctiming-analyze <trace.ctrace> [--json FILE] [-o FILE|--html FILE] [--include GLOB] [--exclude GLOB] [--min-total NS] [--top N]
```

| 参数 | 取值 | 默认 | 含义 |
|------|------|------|------|
| `<trace.ctrace>` | 路径，必填 | 无 | 待分析的 `.ctrace` 文件 |
| `--json FILE` | 文件路径 | 未设置 | 写出全量自洽的 `analysis.json`；设置后**不再**打印文本摘要，仅打印一行 `wrote FILE` |
| `-o FILE` / `--html FILE` | 文件路径 | 未设置 | 写出自包含 `report.html`；写出成功打印一行 `wrote FILE`，设置后**不再**打印文本摘要 |
| `--include GLOB` | 逗号分隔的 glob 列表，如 `leaf*,mid*` | 未设置 | 文本摘要只保留函数名命中列表中任一项的函数 |
| `--exclude GLOB` | 逗号分隔的 glob 列表 | 未设置 | 文本摘要丢弃命中列表中任一项的函数，优先级高于 `--include` |
| `--min-total NS` | 非负整数（纳秒） | `0` | 文本摘要丢弃 `total_ns` 小于该值的函数；`0` 表示不过滤 |
| `--top N` | 非负整数 | `0` | 文本摘要按 `total_ns` 降序只显示前 `N` 行；`0` 表示不限 |

`-o`/`--html` 与 `--json` **等价于两个独立输出**：可以只给其一，也可以同时给出而一次运行写出两种文件；给出任意一个后就不再打印文本摘要，两者都未给出时才打印文本摘要。一次只接受一个 trace 路径，多余的普通参数会报错。`--include`/`--exclude` 接受**逗号分隔的 glob 列表**，命中列表中任一项即算命中；每项两侧的空白（空格/制表符）会被裁剪，空项不匹配任何名字。glob 支持 `*`（任意长度，含空）与 `?`（单字符），大小写敏感。该匹配与运行时 `CTIMING_INCLUDE`/`CTIMING_EXCLUDE` 使用同一套逻辑。

### 退出码

| 退出码 | 场景 |
|--------|------|
| `0` | 成功 |
| `1` | 读取/解析 `.ctrace` 失败，或 `--json` / `-o`（`--html`）目标文件无法写入 |
| `2` | 用法错误：缺少 trace 参数、未知选项、缺少选项值、参数过多，或 `--min-total`/`--top` 取值非法 |

## 过滤语义

`--include` / `--exclude` / `--min-total` / `--top` 决定文本摘要显示哪些函数，并写入 JSON 每个函数的 `kept` 标记；它们**不会删减 JSON 的其他数据**。判定顺序为：先按 `exclude` 丢弃，再要求命中 `include`（若设置了），最后按 `min_total_ns` 过滤；`top` 在按 `total_ns` 排序后截断，只影响文本摘要的行数，**不影响** `kept`。缺少 EXIT 导致 `calls` 为 0 的函数本来就不出现在摘要中。

`--json` 则**始终输出全量数据**，不受上述参数影响；每个函数条目携带布尔字段 `kept`，表示该函数在文本摘要中是否被保留，供查看器按需过滤。即使命令同时给出 `--json` 与过滤参数，JSON 仍然自洽、完整。

## 文本摘要

不带 `--json` 时输出按 `total_ns` 降序排列的函数表。示例：

```
exe: /path/to/example_single
threads: 1  total_events: 18  dropped: 0
functions (by total time):
  calls        total       self     function
  1  209.618us  2.084us  main
  2  207.534us  357ns  mid
  6  207.177us  207.177us  leaf
```

时间按量级自动格式化为 `ns` / `us` / `ms` / `s`。

## JSON 模式（`analysis.json`）

`analysis.json` 是一个自包含数据集，顶层包含 `trace`、`functions`、`call_graph`、`aggregated`、`threads`、`instances` 六个字段。

### `trace`

采集元信息：

| 字段 | 含义 |
|------|------|
| `exe` | 被追踪程序的绝对路径 |
| `pid` | 进程号 |
| `flags` | 文件头标志；bit0 为截断标志，任一线程缓冲达到上限时为 `1` |
| `modules` | 模块数量 |
| `symbols` | 符号表条目数（含运行期追加的 EXTRA 符号） |
| `threads` | 事件块数量 |
| `total_events` | 事件总数 |
| `dropped` | 截断/溢出计数（每线程至多计一次，并非逐事件丢弃数） |

`trace` 对象中还含两个**派生（分析器计算）**字段，它们不是 `.ctrace` 原始内容，而是分析器配对 ENTER/EXIT 重建调用树时统计得到：`unbalanced_enter` 为线程结束时仍留在调用栈上的未配对 ENTER 数量，`orphan_exit` 为调用栈为空时出现的 EXIT 数量。

### `functions`

按符号表下标排列的**全量**函数统计（包含 `calls == 0` 的条目）：

| 字段 | 含义 |
|------|------|
| `id` | 符号表下标，即调用图与实例中的 `fn` 取值 |
| `name` | 函数名 |
| `module` | 所属模块下标；无法符号化的地址为 `0xFFFFFFFF` |
| `offset` | 相对模块基址的偏移；未知符号时存绝对地址 |
| `calls` | 被观测到的调用次数 |
| `total_ns` | 各次调用耗时之和（**含子调用**） |
| `self_ns` | 各次调用自身耗时之和（**不含子调用**） |
| `min_ns` / `max_ns` | 单次调用耗时（含子调用）的最小/最大值；`calls == 0` 时为 `0` |
| `kept` | 该函数是否通过文本摘要过滤；JSON 始终包含全部函数，仅用此字段标记 |

### `call_graph`

聚合后的调用边数组，每条边：

| 字段 | 含义 |
|------|------|
| `caller` / `callee` | 调用方/被调用方的符号表下标 |
| `count` | 该边出现的次数（父实例→子实例的对数） |
| `total_ns` | 被调用实例耗时之和（含其子调用） |
| `recursive` | 是否为递归边：自环，或沿调用图可回到 `caller` 的环上边 |

在 `aggregate` 阶段构建边时，只有 `caller` 与 `callee` 均落在符号表范围内（`fn_id < symbols`）才计入，因此 `call_graph` 中**不会出现越界 `fn_id`**。

### `aggregated`

以每个线程的根调用为起点重建的调用树，聚合后的嵌套结构。每个节点：

| 字段 | 含义 |
|------|------|
| `fn` | 符号表下标 |
| `calls` | 合并后该节点的调用次数 |
| `total_ns` | 合并后含子调用的总耗时 |
| `self_ns` | 合并后自身耗时 |
| `children` | 子节点数组，结构同上（同名子节点会递归合并） |

`aggregated` 节点只携带 `fn`，不再重复写入 `name`；查看器可经由全量 `functions` 数组按 `fn` 取回函数名。

### `threads`

每线程及其根实例：

| 字段 | 含义 |
|------|------|
| `tid` | 线程号 |
| `roots` | 该线程的根实例 `id` 数组（可对应 `instances` 中 `parent == -1` 的条目） |

### `instances`

调用树中的每个实例（一次具体的进入/退出配对）：

| 字段 | 含义 |
|------|------|
| `id` | 实例下标 |
| `fn` | 函数对应的符号表下标 |
| `tid` | 线程号 |
| `depth` | 调用深度，根为 `0` |
| `parent` | 父实例 `id`；根为 `-1` |
| `start_ns` / `end_ns` | 进入/退出时间戳（同线程内单调） |
| `self_ns` | 自身耗时（总时长减去直接子实例时长之和，不足则记 `0`） |
| `children` | 子实例 `id` 数组 |

`instances` 条目同样只携带 `fn`，函数名统一从 `functions`（每个 `fn` 都有对应条目）取得。

## 时间语义

- `total_ns`：**含子调用**的累计耗时，等于各次实例 `end_ns - start_ns` 之和。
- `self_ns`：**不含子调用**的累计耗时，由每个实例的总时长减去其直接子实例时长之和得到。
- `min_ns` / `max_ns`：单次实例（含子调用）耗时的最小/最大值。
- `count`（调用图边）：父实例到子实例的配对次数，与 `calls` 的口径一致但按边拆分。

时间戳来自运行时 `CLOCK_MONOTONIC`，同线程内单调递增；跨线程不做统一排序，`start_ns` 仅在同一线程内有比较意义。

## 递归标记

`recursive` 表示该调用边位于递归环上：`caller == callee` 的自环直接标记；否则先对调用图跑一次强连通分量（SCC）分析，当 `caller` 与 `callee` 落在同一个大小大于 1 的 SCC 中时标记为递归。该标记只描述边是否成环，不改变 `calls`/`total_ns` 的统计口径。

## 与 HTML 查看器的关系

- 自包含 HTML 查看器（概览、火焰图、调用关系图、单次追踪、调用者/被调用者）读取本工具 `--json` 生成的 `analysis.json`，不依赖文本摘要；使用说明见 [viewer.md](viewer.md)。
- 需要稳定的机器可读输入时，请始终使用 `--json`；文本摘要仅用于终端快速查看，且会被过滤参数裁剪。
