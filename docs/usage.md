# ctiming 使用说明

本文档描述运行时库 `libctiming` 的使用方式、`CTIMING_*` 环境变量、公共 API 与 `.ctrace` 文件格式。分析器 `ctiming-analyze` 与 HTML 查看器分别见 [analyzer.md](analyzer.md) 与 [viewer.md](viewer.md)。

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

## 多模块 / 动态库工程

当被观测程序由一个可执行程序加多个共享库（`.so`）组成时，原则是：**想追踪哪些代码，就对哪些模块分别用 `-finstrument-functions` 重新编译**；而 `libctiming` 全程只保留**一份**。

### 推荐：把运行时做成共享库

```bash
gcc -shared -fPIC -O2 -D_GNU_SOURCE -pthread -o libctiming.so \
    src/glob.c src/buffer.c src/trace.c src/symbols.c src/config.c src/runtime.c \
    -Iinclude -Isrc -ldl
```

`libctiming` **不要**加 `-finstrument-functions`。

### 每个模块都加插桩标志

```bash
gcc -finstrument-functions -g -O0 -fPIC -c foo.c -o foo.o && gcc -shared -o libfoo.so foo.o
gcc -finstrument-functions -g -O0 -fPIC -c bar.c -o bar.o && gcc -shared -o libbar.so bar.o
gcc -finstrument-functions -g -O0 -c main.c -o main.o
gcc -o app main.o -L. -lctiming -Wl,-rpath,'$ORIGIN'
```

- 只需要**可执行程序**链接 `libctiming.so`；各 `.so` 里对 `__cyg_profile_func_enter/exit` 的未定义引用会在加载时由它解析（全局符号作用域）。若担心，给 `.so` 也加 `-lctiming` 也安全（共享库去重，只有一份实例）。
- 运行时找库：用 `-Wl,-rpath,'$ORIGIN'`，或设 `LD_LIBRARY_PATH`。
- **不要**给每个 `.so` 各自静态链接一份 `libctiming.a`——会产生多份全局状态与多次导出。

### 备选：静态运行时

只把 `libctiming.a` 链进**可执行程序**一次，并导出符号供 `.so` 解析：

```bash
gcc -o app main.o -L. -lctiming -Wl,--export-dynamic
```

### CMake

```cmake
add_library(ctiming SHARED src/... .c)            # 共享运行时
set_target_properties(ctiming PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ctiming PUBLIC include PRIVATE src)

add_library(foo SHARED foo.c)
target_compile_options(foo PRIVATE -finstrument-functions -g -O0 -fPIC)
target_link_libraries(foo PRIVATE ctiming)        # 可选但稳妥

add_executable(app main.c)
target_compile_options(app PRIVATE -finstrument-functions -g -O0)
target_link_libraries(app PRIVATE ctiming foo)
```

### 符号化与验证

- 运行时读取 `/proc/self/maps` 与每个已加载模块的 ELF 符号，因此**可执行与所有 `.so` 都会被符号化**；`ctiming-info` 的 `modules:` 会大于 1。
- **不要 `strip`** 这些模块（保留 `.symtab`）；被 strip 的函数只显示 `0x...`。
- 验证：`CTIMING_OUT=/tmp/app.ctrace ./app && ./build/ctiming-info /tmp/app.ctrace`，应能看到来自各 `.so` 的函数名。

### 注意事项

- 只对部分模块加标志时，未插桩库的内部调用不会成为事件（其耗时归入调用者的自身耗时）。
- **预编译/第三方 `.so` 无法插桩**：编译期插桩必须重编译源码。
- 建议 `-O0`/`-Og`；高优化下函数被内联后不再产生事件。
- 插件式 `dlopen`：确保 `libctiming.so` 已由主程序加载，插件内部钩子即可解析；插件同样要用 `-finstrument-functions` 编译。

## 环境变量

| 变量 | 取值 | 默认 | 含义 |
|------|------|------|------|
| `CTIMING_ENABLE` | `off` 或 `0` 关闭；其它值、未设置均视为开启 | 开启 | 全局记录开关 |
| `CTIMING_MAX_DEPTH` | 非负整数（`strtoul` 解析），`0` = 不限 | `0` | 最大调用深度；`depth >= MAX_DEPTH` 的事件被丢弃 |
| `CTIMING_INCLUDE` | 逗号分隔的 glob 列表，如 `foo*,std::*` | 未设置 | 非空时，函数名必须命中其中之一才记录 |
| `CTIMING_EXCLUDE` | 逗号分隔的 glob 列表 | 未设置 | 命中任一项的函数一律丢弃；**优先级高于 INCLUDE** |
| `CTIMING_EXCLUDE_LIB` | `off` 或 `0` 关闭；其它值、未设置均视为开启 | 开启 | 默认排除 C/C++ 标准库符号（`std::`、`__gnu_cxx::` 等） |
| `CTIMING_OUT` | 输出文件路径 | `./<程序名>.ctrace` | 退出时导出路径；`<程序名>` 取自 `/proc/self/exe` 的 basename |
| `CTIMING_BUF_KB` | 非负整数（KB） | `1024` | 每线程缓冲初始容量；换算为可容纳的事件数 |
| `CTIMING_BUF_MAX_KB` | 非负整数（KB） | `65536` | 每线程缓冲扩容上限；达到上限后标记截断并停止该线程记录 |
| `CTIMING_DROP_UNKNOWN` | `1` 丢弃；未设置或其它值保留 | 保留 | 无法符号化地址的过滤开关 |
| `CTIMING_CTL` | 文件路径 | 未设置 | 若设置，运行时在初始化时创建该 FIFO（`mkfifo`，权限 `0600`）并开启控制线程，接受运行期命令（见“运行期控制与子树追踪”） |
| `CTIMING_TRACE` | 逗号分隔 glob | 未设置 | 启动期设置子树追踪根：只记录命中该 glob 的函数的动态范围内（含自身）的调用 |

glob 语法支持 `*`（任意长度，含空）与 `?`（单个字符），逐项匹配，项两侧空白会被去除，大小写敏感。逗号列表中的空项不匹配任何名字。

### 过滤语义

判定顺序如下（实现见 `pass_filter` 与 `ct_filter_match`）：

1. 若 `CTIMING_EXCLUDE_LIB` 开启（默认）且函数名属于 C/C++ 标准库（`std::`、`__gnu_cxx::`、`__gnu::`、`gnu::`、`__cxxabiv1::`、`__cxx::`、`operator new`、`operator delete`）→ **丢弃**。
2. 否则，若函数名命中 `CTIMING_EXCLUDE`（或 API 设置的 `exclude`）→ **丢弃**。
3. 否则，若 `INCLUDE` 非空 → 必须命中 `INCLUDE` 才保留，否则丢弃。
4. 否则（`INCLUDE` 为空或未设置）→ 保留。

即 **标准库排除与 `EXCLUDE` 优先级最高**。注意：判定基于函数**自身的限定名**（忽略参数列表里的 `std::` 类型），因此返回 `std::vector` 或参数含 `std::` 的**用户函数仍会保留**。若要连同标准库一起记录，设 `CTIMING_EXCLUDE_LIB=off`。示例：

```bash
CTIMING_INCLUDE='main,mid*' CTIMING_EXCLUDE='mid_internal' ./app
```

无法符号化的地址（`0x...`）默认保留，**即使设置了 `INCLUDE` 也不受影响**；设置 `CTIMING_DROP_UNKNOWN=1` 后一律丢弃。

**子树追踪（设置追踪根后）**：设置追踪根 `F`（`CTIMING_TRACE` 或 `ctiming_set_trace_symbol`）后进入子树语义——只记录 `F` 动态范围内（含 `F` 自身）的调用，其余调用一律不记录。子树内**忽略 `INCLUDE`**（不再要求命中），但仍遵守标准库排除（`CTIMING_EXCLUDE_LIB` 开启时）与 `EXCLUDE`；`CTIMING_MAX_DEPTH` 依旧生效，`depth >= MAX_DEPTH` 的事件仍被丢弃。捕获状态**每线程独立**；`F` 递归调用只在其最外层进入时开启一次捕获，退出该最外层 `F` 后关闭。**未设置追踪根时，行为与上述过滤语义完全一致。**

## 公共 API

头文件：`include/ctiming.h`。版本字符串当前为 `0.1.0`（`ctiming_version()`）。

| 函数 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `void ctiming_start(void)` | 无 | 无 | 开启记录（默认即开启），并触发运行时初始化 |
| `void ctiming_stop(void)` | 无 | 无 | 停止记录，之后的事件不再写入缓冲 |
| `int ctiming_dump(const char *path)` | `path`：输出路径，可为 `NULL` | `0` 成功，非 `0` 失败 | 立即导出缓冲（已退役线程 + 调用线程自身，见“已知限制”）到文件；`NULL` 时用配置路径（`CTIMING_OUT` 或默认路径） |
| `void ctiming_set_filter(const char *include, const char *exclude)` | 两个逗号分隔 glob，均可为 `NULL` | 无 | 运行期更新过滤规则，语义同环境变量 |
| `void ctiming_set_max_depth(unsigned depth)` | `depth`：最大深度，`0` = 不限 | 无 | 运行期更新最大调用深度 |
| `int ctiming_set_trace_symbol(const char *pattern)` | `pattern`：逗号分隔 glob，可为 `NULL` | 命中的函数地址数；`NULL`/空串清除时返回 `0`，分配失败返回 `-1` | 设置子树追踪根（见下节） |
| `const char *ctiming_version(void)` | 无 | 指向版本字符串的常量指针 | 返回如 `"0.1.0"` |

除 `ctiming_version` 外，调用公共 API 会触发一次惰性初始化（内部 `pthread_once`）；`ctiming_version` 仅返回常量字符串，不做初始化。`ctiming_stop` 会先完成初始化再把记录开关置零，因此**在任何插桩事件之前调用也不会被后续的惰性初始化重新开启**。同样地，若程序未提前调用，首次进入插桩钩子时也会自动初始化。进程退出时由 `atexit` 自动导出一次。

## 运行期控制与子树追踪

### 控制 FIFO

设置 `CTIMING_CTL=<path>` 后，运行时在首次惰性初始化时创建该 FIFO 并启动后台线程逐字节读取命令。命令以整行为单位，**同时支持 `\n` 与 `\r\n` 行尾**；命令回复写入 **`stderr`**，统一前缀 `ctiming:`。FIFO 以 `O_RDWR` 打开，因此即使没有写端也不会使读线程阻塞。

```bash
CTIMING_CTL=/tmp/app.fifo CTIMING_OUT=/tmp/app.trace ./app &
echo 'trace my::func' > /tmp/app.fifo   # 设置子树追踪根
echo 'status'         > /tmp/app.fifo   # 查看当前状态
echo 'dump /tmp/app.trace' > /tmp/app.fifo
echo 'untrace'        > /tmp/app.fifo
```

命令表（命令名与参数以空格/制表符分隔）：

| 命令 | 说明 | 回复 |
|------|------|------|
| `start` | 开启记录 | `recording on` |
| `stop` | 停止记录 | `recording off` |
| `toggle` | 在开/关之间切换 | `recording on` 或 `recording off` |
| `dump [PATH]` | 立即导出缓冲，省略 `PATH` 时用配置路径 | `dumped` 或 `dump failed` |
| `trace [SPEC]` | 按逗号分隔 glob `SPEC` 设置子树追踪根；`trace off` 或省略 `SPEC` 清除 | `trace 'SPEC' -> N address(es)` 或 `trace cleared` |
| `untrace` | 清除追踪根 | `trace cleared` |
| `include [GLOB]` | 设置 `INCLUDE` 过滤（保留当前 `EXCLUDE`）；省略或空则清除 | `include set` |
| `exclude [GLOB]` | 设置 `EXCLUDE` 过滤（保留当前 `INCLUDE`）；省略或空则清除 | `exclude set` |
| `status` | 打印状态 | `enabled=<0/1> trace=<on/off> include=<...> exclude=<...>` |
| 其它 | 未知命令 | `unknown command` |

`trace`/`include`/`exclude` 取命令名之后的**整行剩余内容**并去掉前导空白，因此这些 glob 参数不要带前导空格。`include`/`exclude` 的运行期语义与环境变量一致。

### 子树追踪 API

```c
int ctiming_set_trace_symbol(const char *pattern);
```

- `pattern` 为逗号分隔 glob；返回**命中的函数地址数**。
- `NULL` 或空串清除追踪（返回 `0`）。
- 内存分配失败返回 `-1`，此时保持原追踪设置不变。
- 追踪目标在**设置时刻**依据已加载的符号表解析为地址集合；计数为 `0` 时等价于未设置追踪根。

设置后行为见上文“过滤语义”中的子树追踪说明。递归、多线程与 `MAX_DEPTH` 的交互同样适用。

### 已知限制

- `dump` 只导出**已退役线程**的缓冲与**调用 `dump` 的线程自身**的缓冲；仍在运行且未退出的其它线程缓冲不会被导出。
- 追踪目标地址在设置时刻解析，之后通过 `dlopen` 新加载模块中的函数不在范围内。
- 控制 FIFO 文件**不会自动删除**，需要自行清理。

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

- `ctiming-analyze` 分析器（C++17）：配对 ENTER/EXIT、重建调用树、聚合统计与调用图，支持文本摘要过滤与 `--json` 全量导出。用法见 [analyzer.md](analyzer.md)。
- 自包含 HTML 查看器（概览、火焰图、调用关系图、单次追踪、调用者/被调用者），读取 `analysis.json`。用法见 [viewer.md](viewer.md)。
