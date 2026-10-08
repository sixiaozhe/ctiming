# ctiming

`ctiming` 是一个面向 C/C++ 程序的**函数级耗时统计与调用追踪工具**。它在编译期通过 GCC/Clang 的 `-finstrument-functions` 为每个函数插入进入/退出钩子，运行时库 `libctiming` 把每次调用记录到每线程的无锁缓冲，进程退出时导出为二进制 `.ctrace` 文件；再用 `ctiming-info` 读回统计结果。完整的分析器与自包含 HTML 查看器为后续计划。

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

依赖 CMake ≥ 3.16、C11/C++17 编译器与 Linux glibc。构建产物：静态库 `ctiming`、CLI `ctiming-info`、示例 `example_single`，以及各 `test_*` 测试。

## 环境变量

| 变量 | 取值 | 默认 | 含义 |
|------|------|------|------|
| `CTIMING_ENABLE` | `off` / `0` 关闭，其它值或无 | 开启 | 是否记录 |
| `CTIMING_MAX_DEPTH` | 非负整数，`0` = 不限 | `0` | 最大调用深度，超过则不记录 |
| `CTIMING_INCLUDE` | 逗号分隔 glob | 未设（不限） | 非空时，函数名须命中才记录 |
| `CTIMING_EXCLUDE` | 逗号分隔 glob | 未设 | 命中的函数一律丢弃，优先级最高 |
| `CTIMING_OUT` | 文件路径 | `./<程序名>.ctrace` | 导出路径 |

过滤语义：**`EXCLUDE` 优先**——命中 `EXCLUDE` 即丢弃；否则若 `INCLUDE` 非空则必须命中才保留，`INCLUDE` 为空则保留。示例：

```bash
CTIMING_EXCLUDE='std::*,__gnu*' ./app
```

详见 [docs/usage.md](docs/usage.md)。

## 已知局限

- **被内联的函数不会出现**：`-finstrument-functions` 按函数体插桩，内联后原函数消失。
- **建议使用 `-O0` 或 `-Og`**：高优化等级（`-O2`/`-O3`）下大量函数被内联，结果会明显失真。
- **仅支持 64 位 ELF / Linux**：自符号化依赖 `/proc/self/maps` 与 ELF `.symtab`/`.dynsym`。
- 头部/尾部极早构造期的调用可能被丢弃；未插桩目标不会有任何事件。

## 路线图

- **计划 1（已完成）**：运行时库 `libctiming` + `ctiming.h`、插桩钩子、每线程缓冲、自符号化、`CTIMING_*` 过滤、`.ctrace` 导出、`ctiming-info`。
- **计划 2（后续）**：分析器 `ctiming-analyze`，重建调用树、聚合统计与调用图。
- **计划 3（后续）**：自包含 HTML 查看器（火焰图、时间线、调用关系图）。
