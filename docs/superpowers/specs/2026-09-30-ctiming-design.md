# ctiming 设计规格

- 日期：2026-09-30
- 状态：已定稿，待实现
- 代号：ctiming（C/C++ 函数耗时统计与调用追踪工具）

## 1. 背景与目标

需要一个工具，对 C/C++ 程序做**函数级耗时统计**，并且能：

1. **看出调用关系**——聚合视角下，函数之间的调用结构（谁调用谁、调用次数、累计耗时）。
2. **追踪某一次调用过程**——能挑出某一次具体的函数调用，回放它从进入、内部子调用到返回的完整时序。
3. **展示直观**——以自包含的静态 HTML 报告呈现，含火焰图、瀑布时间线、调用关系图等。

目标平台：Linux + GCC/Clang，C 与 C++ 程序均可。

## 2. 非目标（YAGNI）

- 不做跨平台（Windows/MSVC、macOS 不在本期范围）。
- 不做采样式剖析（本工具基于编译期插桩，事件完整）。
- 不做实时/在线分析服务（`--serve` 仅作为未来扩展预留）。
- 不做内存剖析、不做 I/O 统计，仅函数耗时与调用关系。
- 不内建压缩（不引入 zlib）；用户可自行 gzip。

## 3. 已知局限（须写入用户文档）

- **被内联的函数不会出现**在结果中（`-finstrument-functions` 按函数体插桩）。
- 高优化等级（`-O2`/`-O3`）下大量函数被内联，结果会显著失真；**建议用 `-O0` 或 `-Og`** 采集。
- 头部/尾部的极早构造期调用可能被丢弃或标记为“启动前”。
- 若目标未以 `-finstrument-functions` 编译，运行时库会给出明确提示。

## 4. 总体架构

采用**两段式**：运行期只记录原始事件；分析器离线重建一切。

```
┌─────────────────┐   -finstrument-functions        ┌──────────┐
│ 被观测程序      │──────(libctiming 钩子)─────────▶│ .ctrace  │
│ (C/C++)         │   每线程无锁缓冲, atexit 导出     └────┬─────┘
└─────────────────┘                                      │
                                                         ▼
                                            ┌────────────────────────┐
                                            │ ctiming-analyze (C++)  │
                                            │ 解析/符号化/重建/渲染   │
                                            └───────────┬────────────┘
                                                        ▼
                                            ┌────────────────────────┐
                                            │ report.html (自包含)    │
                                            │ 内嵌数据 + JS/SVG 查看器│
                                            └────────────────────────┘
```

组件：

1. **运行时库 `libctiming` + 头文件 `ctiming.h`**（C 兼容，可被 C/C++ 程序链接）。
2. **分析器 CLI `ctiming-analyze`**（C++17，单二进制）。
3. **查看器**：内嵌在 `report.html` 里的 Vanilla JS/SVG。

同一份 `.ctrace` 可反复分析、切换视图。

## 5. 运行时库

### 5.1 插桩机制

- 依赖 GCC/Clang 的 `-finstrument-functions`，实现：
  - `void __cyg_profile_func_enter(void *this_fn, void *call_site)`
  - `void __cyg_profile_func_exit(void *this_fn, void *call_site)`
- 运行时库自身**所有**函数标注 `__attribute__((no_instrument_function))`，并加 `CTIMING_HIDDEN`，避免递归记录。
- 钩子内部**不调用 malloc / 不加锁 / 不做符号化**，只做：读时钟、写事件、更新深度。

### 5.2 事件模型

每条原始事件：

| 字段 | 类型 | 说明 |
|------|------|------|
| `tid` | u32 | 线程 id（`gettid()`） |
| `fn_addr` | ptr | 被调用函数地址 |
| `kind` | u8 | `ENTER` / `EXIT` |
| `ts` | u64 | `CLOCK_MONOTONIC` 纳秒 |
| `call_site` | ptr | 调用点地址（可选，用于更细的调用边归属） |

- 深度由每线程栈推导，不单独存储。
- 事件按线程分块存放，便于 delta 编码（同线程内时间戳做差）。

### 5.3 缓冲与线程

- 每线程一份**预分配环形/分块缓冲**，默认 1 MB（`CTIMING_BUF_KB` 可调）。
- 惰性初始化：首次在本线程触发钩子时分配；惰性初始化期间有递归保护。
- 溢出策略：默认**继续扩容**（上限可配）；达到上限则标记截断（记录丢事件数），保证 ENTER/EXIT 尽量配对。
- 线程退出：将本线程缓冲登记到全局队列，待 `atexit`/`dump` 时统一写出。

### 5.4 自符号化与过滤（关键）

运行期拿不到函数名，因此 ctiming **给自己所加载的模块符号化**：

- 初始化时读 `/proc/self/maps` + `dl_iterate_phdr`，得到各模块加载基址。
- 通过 mmap 读取各模块文件（`/proc/self/exe` 及共享库）的 ELF `.symtab` 与 `.dynsym`，建立 `地址 → 函数名` 表（含 static 函数）；C++ 名用 `abi::__cxa_demangle` 还原。
- **惰性解析 + 缓存**：某 `fn_addr` 首次出现时解析一次，缓存其名字与过滤判定，之后 O(1)。
- 过滤配置：
  - 环境变量 `CTIMING_INCLUDE` / `CTIMING_EXCLUDE`：逗号分隔 glob（如 `foo*,std::*`）。
  - API `ctiming_set_filter(include, exclude)`。
  - 过滤语义（明确）：`EXCLUDE` 命中即丢弃；否则若 `INCLUDE` 非空则必须命中才保留；若 `INCLUDE` 为空则保留。即 **EXCLUDE 优先级最高**。
- 无法符号化的地址：默认保留，显示为 `0x...`；`CTIMING_DROP_UNKNOWN=1` 可丢弃。
- 其他运行期过滤：录制开关（默认开）、最大调用深度（`CTIMING_MAX_DEPTH`）。

### 5.5 符号化结果的去向

运行期把 `地址 → 名字` 表写入 trace 的符号表段。因此**分析器不加 `--binary` 也能显示函数名**。`--binary` 仅用于补全被 strip 或运行期未能解析的符号。

### 5.6 触发与导出

- 默认开启记录。
- `atexit`（并辅以 `__attribute__((destructor))`）在进程退出时把各线程缓冲写为 `./<prog>.ctrace`；路径可用 `CTIMING_OUT` 覆盖。
- 手动导出 API：`ctiming_dump(const char *path)`、`ctiming_start()`、`ctiming_stop()`。
- `fork` 后：子进程默认**重置**运行时状态（不继承父缓冲），可通过 `CTIMING_FORK=inherit` 改为继承。

### 5.7 公共 API（`ctiming.h`）

```c
void ctiming_start(void);
void ctiming_stop(void);
int  ctiming_dump(const char *path);          /* 0 成功 */
void ctiming_set_filter(const char *include,  /* 逗号分隔 glob，可 NULL */
                        const char *exclude);
void ctiming_set_max_depth(unsigned depth);   /* 0 = 不限 */
const char *ctiming_version(void);
```

## 6. trace 文件格式（`.ctrace`）

分段式（chunked）二进制，小端，指针宽度跟随采集机（u64/u32）：

- **Header**：magic `CTMG`、version(u16)、endian(u8)、ptr_size(u8)、pid(u32)、启动时间(u64)、标志位（开关/限深）、exe 路径。
- **MODULES 段**：每条 `{base(u64), path(string)}`，用于地址换算。
- **EVENTS 段**：按线程分块；块头 `{tid, count}`，记录为变长编码：
  - `fn_id`：varint，指向符号表索引；
  - `ts`：varint delta（同线程上一事件的时间差）；
  - `kind`：1 bit（ENTER/EXIT）；
  - `call_site`：可选，仅当 Header 标志开启。
- **SYMBOLS 段**：`fn_id → {module_index, addr_offset, name}`（名字已是 demangle 结果）。
- **Footer/索引**：事件总数、线程表 `{tid → 事件范围}`、每线程 ENTER 计数、截断/丢弃计数。

分析器同时接受未压缩 `.ctrace`；若文件名以 `.gz` 结尾，可选地调用系统 `gzip -dc` 读取（不链接 zlib）。

## 7. 分析器 `ctiming-analyze`

用法：

```
ctiming-analyze app.ctrace [--binary ./app] [-o report.html] [--top N]
                [--include GLOB] [--exclude GLOB] [--min-total NS]
```

流程：

1. 解析 trace，按线程把 ENTER/EXIT 配对成调用树（不平衡处按启发式收尾并标记）。
2. 符号化：优先用 trace 内符号表；缺失时读 `--binary` 的 ELF（自实现解析，无 libelf）。
3. 重建产物：
   - **每函数聚合**：调用次数、total（含子）、self、min/max/avg。
   - **聚合调用树**：相同调用路径合并。
   - **调用图边**：`(caller, callee, count, total_cost)`，标记递归/环。
   - **调用实例索引**：每个 ENTER 节点为一个“调用实例”，记录 `{函数, tid, 起止时间, 深度, parent, children}`，供单次追踪任意挑选。
4. 渲染 `report.html`：把上述数据以 JSON 内嵌，配合内嵌查看器 JS。

按名过滤（`--include/--exclude`）与耗时阈值（`--min-total`）在分析器侧二次过滤。

## 8. 查看器 UX（内嵌 `report.html`，单页多 Tab）

- **概览**：KPI（总耗时、调用总次数、函数数、最慢单次调用）+ Top 热点表（可按 self/total/次数排序）。
- **火焰图 / icicle**：聚合调用栈，宽度=累计耗时，支持下钻、反向（自底向上）合并、缩放。
- **调用关系图**：节点=函数，有向边=调用，线粗=次数或耗时；可过滤低权重边。
- **单次追踪**：左侧调用实例列表（可按函数/线程/耗时搜索排序），右侧**时间线瀑布图**；选中某实例显示其子树与 self/total。
- **调用者 / 被调用者**：点选任一函数，列出谁调用它、它调用谁（带次数与耗时）。
- 全局函数搜索；时间单位自适应（ns/µs/ms/s）。

查看器零外部依赖：不引用 CDN，JS 与 SVG 全部内嵌。

## 9. 错误处理与边界

- 钩子重入/递归保护；早于运行时初始化的调用：惰性初始化，就绪前丢弃或暂存，并在报告中标注“启动前”。
- 事件截断：保证尽量配对，截断处打标记并在报告中提示。
- `fork`：默认重置，可配置继承。
- 未插桩目标：运行时打印明确警告（提示加 `-finstrument-functions`）。
- 时钟：统一 `CLOCK_MONOTONIC`，多线程可比；不做跨机时钟对齐。
- 分析器对损坏/版本不匹配的 trace 给出可读错误，而非崩溃。

## 10. 依赖

- 运行时库：仅 glibc（Linux）。
- 分析器：C++17 + libstdc++（用其自带 `abi::__cxa_demangle`）；ELF 解析自实现。
- 查看器：无外部依赖，无 CDN。
- **不引入 zlib、libelf、d3 等第三方库。**

## 11. 测试策略

- **单元**：环形/分块缓冲、变长编解码、ENTER/EXIT 配对、聚合统计、ELF 符号解析、`__cxa_demangle`、PIE 地址换算、glob 过滤。
- **集成**：一组示例程序（单线程、多线程、递归、C++ 构造/析构、共享库），编译插桩 → 运行 → 分析 → 断言统计与调用关系。
- **报告**：断言 `report.html` 含关键数据（内嵌 JSON 可解析、函数出现、耗时量级正确）。
- **性能**：微基准，量化每次调用的钩子开销，写入文档。

## 12. 构建与用法

- CMake（C++17），targets：`ctiming`（静态库）、`ctiming-analyze`、`examples/`、`tests/`。

```bash
gcc -finstrument-functions -g app.c -L. -lctiming -o app
./app                                   # 默认开启，退出生成 app.ctrace
ctiming-analyze app.ctrace -o report.html   # 用 trace 内符号表
# 全量记录建议 -O0/-Og；过滤示例：
CTIMING_EXCLUDE='std::*,__gnu*' ./app
```

## 13. 未来扩展（不在本期）

- `ctiming-analyze --serve`：本地服务实时查看。
- trace 内建压缩；跨机合并多份 trace。
- 文件:行号与模块级归因（需 DWARF）。
- C++ 异常的调用栈边界处理。
