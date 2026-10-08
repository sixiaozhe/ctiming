# ctiming 使用说明

本文档描述运行时库 `libctiming` 的使用方式、`CTIMING_*` 环境变量、公共 API 与 `.ctrace` 文件格式。分析器 `ctiming-analyze` 已实现（计划 2），详见 [analyzer.md](analyzer.md)；**HTML 查看器属计划 3，尚未提供**。

## 编译与链接

对需要观测的目标文件加 `-finstrument-functions`，并链接 `libctiming`：

```bash
gcc -finstrument-functions -g -O0 app.c -Iinclude -Lbuild -lctiming -o app
```

如果系统已安装该库，可简化为：

```bash
gcc -finstrument-functions -g app.c -L. -lctiming -o app
```

运行时库自身所有函数都标注了 `no_instrument_function`，因此**给库加不加该标志都不影响**，且不会产生递归记录。支持 C 与 C++（`ctiming.h` 带 `extern "C"`）。

## 环境变量

| 变量 | 取值 | 默认 | 含义 |
|------|------|------|------|
| `CTIMING_ENABLE` | `off` 或 `0` 关闭；其它值、未设置均视为开启 | 开启 | 全局记录开关 |
| `CTIMING_MAX_DEPTH` | 非负整数（`strtoul` 解析），`0` = 不限 | `0` | 最大调用深度；`depth >= MAX_DEPTH` 的事件被丢弃 |
| `CTIMING_INCLUDE` | 逗号分隔的 glob 列表，如 `foo*,std::*` | 未设置 | 非空时，函数名必须命中其中之一才记录 |
| `CTIMING_EXCLUDE` | 逗号分隔的 glob 列表 | 未设置 | 命中任一项的函数一律丢弃；**优先级高于 INCLUDE** |
| `CTIMING_OUT` | 输出文件路径 | `./<程序名>.ctrace` | 退出时导出路径；`<程序名>` 取自 `/proc/self/exe` 的 basename |
| `CTIMING_BUF_KB` | 非负整数（KB） | `1024` | 每线程缓冲初始容量；换算为可容纳的事件数 |
| `CTIMING_BUF_MAX_KB` | 非负整数（KB） | `65536` | 每线程缓冲扩容上限；达到上限后标记截断并停止该线程记录 |
| `CTIMING_DROP_UNKNOWN` | `1` 丢弃；未设置或其它值保留 | 保留 | 无法符号化地址的过滤开关 |

glob 语法支持 `*`（任意长度，含空）与 `?`（单个字符），逐项匹配，项两侧空白会被去除，大小写敏感。逗号列表中的空项不匹配任何名字。

### 过滤语义

判定顺序如下（实现见 `ct_filter_match`）：

1. 若函数名命中 `CTIMING_EXCLUDE`（或 API 设置的 `exclude`）→ **丢弃**。
2. 否则，若 `INCLUDE` 非空 → 必须命中 `INCLUDE` 才保留，否则丢弃。
3. 否则（`INCLUDE` 为空或未设置）→ 保留。

即 **EXCLUDE 优先级最高**。示例：

```bash
CTIMING_INCLUDE='main,mid*' CTIMING_EXCLUDE='mid_internal' ./app
```

无法符号化的地址（`0x...`）默认保留，**即使设置了 `INCLUDE` 也不受影响**；设置 `CTIMING_DROP_UNKNOWN=1` 后一律丢弃。

## 公共 API

头文件：`include/ctiming.h`。版本字符串当前为 `0.1.0`（`ctiming_version()`）。

| 函数 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `void ctiming_start(void)` | 无 | 无 | 开启记录（默认即开启），并触发运行时初始化 |
| `void ctiming_stop(void)` | 无 | 无 | 停止记录，之后的事件不再写入缓冲 |
| `int ctiming_dump(const char *path)` | `path`：输出路径，可为 `NULL` | `0` 成功，非 `0` 失败 | 立即把当前所有线程缓冲导出到文件；`NULL` 时用配置路径（`CTIMING_OUT` 或默认路径） |
| `void ctiming_set_filter(const char *include, const char *exclude)` | 两个逗号分隔 glob，均可为 `NULL` | 无 | 运行期更新过滤规则，语义同环境变量 |
| `void ctiming_set_max_depth(unsigned depth)` | `depth`：最大深度，`0` = 不限 | 无 | 运行期更新最大调用深度 |
| `const char *ctiming_version(void)` | 无 | 指向版本字符串的常量指针 | 返回如 `"0.1.0"` |

除 `ctiming_version` 外，调用公共 API 会触发一次惰性初始化（内部 `pthread_once`）；`ctiming_version` 仅返回常量字符串，不做初始化。`ctiming_stop` 会先完成初始化再把记录开关置零，因此**在任何插桩事件之前调用也不会被后续的惰性初始化重新开启**。同样地，若程序未提前调用，首次进入插桩钩子时也会自动初始化。进程退出时由 `atexit` 自动导出一次。

## `.ctrace` 文件格式

分段式二进制文件，**全部小端**。地址类字段（`base`、`offset`、`call_site`）一律写成定长 `u64`；Header 中虽记录 `ptr_size`，但当前实现仅支持 64 位，读回时并不使用该字段。字符串编码为 `u32 长度 + 原始字节`（不含结尾 `\0`）。整数 `u8/u16/u32/u64` 为定长小端；`varint` 为无符号 LEB128 变长编码（每字节低 7 位有效，最高位为续位）。

写出顺序如下：

1. **Header**
   - magic `CTMG`（4 字节）
   - version `u16`（当前为 `1`）
   - endian `u8`（`1` = 小端）
   - ptr_size `u8`（`sizeof(void*)`）
   - pid `u32`
   - start_ns `u64`（`CLOCK_MONOTONIC` 启动时刻）
   - flags `u32`（bit0 为截断标志：任一线程缓冲达到上限时为 `1`，否则 `0`）
   - exe `string`（可执行文件绝对路径）
2. **MODULES 段**：`u32 n_modules`，随后每条 `{base: u64, path: string}`，用于把符号偏移换算回运行地址。
3. **SYMBOLS 段**：`u32 n_symbols`，随后每条 `{module: u32, offset: u64, name: string}`。`offset` 为相对所属模块 `base` 的偏移；`name` 已是 demangle 后的函数名（可能为空串）。
4. **EVENTS 段**：`u32 n_threads`（写出的线程块数），随后每块：
   - 块头 `{tid: u32, count: u32}`；
   - `count` 条事件，逐条：
     - `fn_id`：`varint`，指向符号表下标（含后续 EXTRA 段追加的条目）；
     - `ts`：`varint`，同线程内的时间戳；本块第一条为绝对值，其余为相对上一事件的差值；
     - `flags`：`u8`，bit0 为 `kind`（`0` = ENTER，`1` = EXIT），bit1 表示是否带 `call_site`；
     - `call_site`：`u64`，仅当 `flags` 的 bit1 置位时出现。
5. **EXTRA 符号段**：`u32 n_extra`，随后每条 `{module: u32, offset: u64, name: string}`。运行期未能符号化的地址写入此处，`module` 为 `0xFFFFFFFF`（`CT_UNKNOWN_MODULE`），`offset` 存的是绝对地址；读回时这些条目被合并到符号表末尾。
6. **Footer**
   - total_events `u32`（事件总数）
   - dropped `u32`（截断/溢出计数：某线程缓冲达到 `CTIMING_BUF_MAX_KB` 上限后会标记截断并停止该线程记录，因此通常每线程至多计一次，**并非**被丢弃的逐事件总数）
   - magic `CTME`（4 字节）

读回时通过 `ct_trace_open`/`ct_trace_close`（`src/trace.h`）解析，签名校验失败、版本/端序不匹配或结构越界都会返回非 `0` 错误码，而不会崩溃。`ctiming-info` 即基于该接口打印摘要与每函数调用次数。

## 分析器与查看器

- **计划 2（已实现）**：`ctiming-analyze` 分析器（C++17），配对 ENTER/EXIT、重建调用树、聚合统计与调用图，支持文本摘要过滤与 `--json` 全量导出。用法见 [analyzer.md](analyzer.md)。
- **计划 3（尚未提供）**：自包含 HTML 查看器（火焰图、瀑布时间线、调用关系图），读取 `analysis.json`。
