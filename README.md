# ctiming

`ctiming` 是一个面向 C/C++ 程序的**函数级耗时统计与调用追踪工具**。它在编译期通过 GCC/Clang 的 `-finstrument-functions` 为每个函数插入进入/退出钩子，运行时库 `libctiming` 把每次调用记录到每线程的无锁缓冲，进程退出时导出为二进制 `.ctrace` 文件；再用 `ctiming-info` 读回统计结果，或用分析器 `ctiming-analyze` 重建调用树、聚合统计并输出 JSON，或生成自包含的 HTML 报告。

## 快速开始

以下命令均在仓库根目录执行。

```bash
# 1) 构建运行时库与工具
cmake -S . -B build
cmake --build build -j

# 2) 用插桩标志编译你的程序，并链接 libctiming
gcc -finstrument-functions -g -O0 examples/example_single.c \
    -Iinclude -Lbuild -lctiming -o app

# 3) 运行程序：默认记录，退出时生成 ./app.ctrace
./app

# 4) 读回统计
./build/ctiming-info app.ctrace
```

`ctiming-info` 输出示例：

```
exe: /path/to/app
pid: 12345
modules: 3
symbols: 2962
threads: 1
total_events: 18
dropped: 0
functions:
  6  leaf
  2  mid
  1  main
```

> 只有用 `-finstrument-functions` 编译的目标文件才会产生事件；运行时库本身无需该标志。输出路径默认是 `./<程序名>.ctrace`，可用 `CTIMING_OUT` 覆盖。

## 构建与测试

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

依赖 CMake ≥ 3.16、C11/C++17 编译器与 Linux glibc。构建产物：静态库 `ctiming`、CLI `ctiming-info`、分析器 `ctiming-analyze`、示例 `example_single`，以及各 `test_*` 测试。

## 环境变量

| 变量 | 取值 | 默认 | 含义 |
|------|------|------|------|
| `CTIMING_ENABLE` | `off` / `0` 关闭，其它值或无 | 开启 | 是否记录 |
| `CTIMING_MAX_DEPTH` | 非负整数，`0` = 不限 | `0` | 最大调用深度，超过则不记录 |
| `CTIMING_INCLUDE` | 逗号分隔 glob | 未设（不限） | 非空时，函数名须命中才记录 |
| `CTIMING_EXCLUDE` | 逗号分隔 glob | 未设 | 命中的函数一律丢弃，优先级最高 |
| `CTIMING_EXCLUDE_LIB` | `off` / `0` 关闭，其它值或无 | 开启 | 默认排除 C/C++ 标准库符号（`std::`、`__gnu_cxx::` 等） |
| `CTIMING_OUT` | 文件路径 | `./<程序名>.ctrace` | 导出路径 |
| `CTIMING_BUF_KB` | 非负整数（KB） | `1024` | 每线程缓冲初始容量 |
| `CTIMING_BUF_MAX_KB` | 非负整数（KB），`0` = 不限 | `65536` | 每线程缓冲扩容上限，达到后标记截断并停止该线程记录 |
| `CTIMING_DROP_UNKNOWN` | `1` 丢弃；其它值或未设置 = 保留 | `0`（保留） | 是否丢弃无法符号化的地址 |

过滤语义：默认先排除 C/C++ 标准库符号（`CTIMING_EXCLUDE_LIB=off` 可关闭）；随后 **`EXCLUDE` 优先**——命中 `EXCLUDE` 即丢弃；否则若 `INCLUDE` 非空则必须命中才保留，`INCLUDE` 为空则保留。判定基于函数自身的限定名，参数/返回值里含 `std::` 的用户函数仍会保留。无法符号化的地址（`0x...`）默认**保留**，设置 `CTIMING_DROP_UNKNOWN=1` 后一律丢弃。示例：

```bash
CTIMING_EXCLUDE='*detail*,*internal*' ./app
```

详见 [docs/usage.md](docs/usage.md)。

## 分析器（计划 2）

`ctiming-analyze` 读取 `.ctrace`，配对 ENTER/EXIT、重建调用树并聚合出函数耗时与调用图。

```bash
./build/example_single                       # 生成 example_single.ctrace
./build/ctiming-analyze example_single.ctrace          # 文本摘要
./build/ctiming-analyze example_single.ctrace --json analysis.json   # 全量 JSON
```

文本摘要支持 `--include GLOB`、`--exclude GLOB`、`--min-total NS`、`--top N` 四个过滤参数：它们决定文本摘要显示哪些函数，并写入 JSON 每个函数的 `kept` 布尔标记（`--top` 只影响文本行数，不改变 `kept`）；`--json` 仍始终写出全量自洽的 `analysis.json`，不会因过滤而删减数据。退出码：`0` 成功，`1` 读取/写文件失败，`2` 用法错误。字段含义与过滤语义详见 [docs/analyzer.md](docs/analyzer.md)。

## 生成 HTML 报告（计划 3）

`ctiming-analyze` 还能把分析结果生成为**自包含、离线可用**的单文件 HTML 报告：数据与查看器（CSS/JS）全部内联，不引用 CDN，无需网络。

```bash
./build/example_single
./build/ctiming-analyze example_single.ctrace -o report.html
```

`-o FILE` 与 `--html FILE` 等价；两者可与 `--json FILE` **同时使用**，一次运行同时写出 HTML 与 JSON 两份文件。给出 `-o`/`--html` 后不再打印文本摘要，成功时打印 `wrote FILE`。报告包含**概览、火焰图、调用关系图、单次追踪、调用者/被调用者**五个 Tab，读取 `analysis.json` 数据，详见 [docs/viewer.md](docs/viewer.md)。

## 已知局限

- **被内联的函数不会出现**：`-finstrument-functions` 按函数体插桩，内联后原函数消失。
- **建议使用 `-O0` 或 `-Og`**：高优化等级（`-O2`/`-O3`）下大量函数被内联，结果会明显失真。
- **仅支持 64 位 ELF / Linux**：自符号化依赖 `/proc/self/maps` 与 ELF `.symtab`/`.dynsym`。
- 头部/尾部极早构造期的调用可能被丢弃；未插桩目标不会有任何事件。

## 路线图

- **计划 1（已完成）**：运行时库 `libctiming` + `ctiming.h`、插桩钩子、每线程缓冲、自符号化、`CTIMING_*` 过滤、`.ctrace` 导出、`ctiming-info`。
- **计划 2（已完成）**：分析器 `ctiming-analyze`，重建调用树、聚合统计与调用图，支持文本摘要过滤与 `--json` 全量导出。
- **计划 3（已完成）**：自包含 HTML 查看器（概览、火焰图、调用关系图、单次追踪、调用者/被调用者），由 `ctiming-analyze -o report.html` 生成，读取 `analysis.json`。
