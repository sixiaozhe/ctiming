# ctiming 计划 1：运行时库 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 `libctiming` 运行时库与 `.ctrace` 文件格式，使被 `-finstrument-functions` 编译的 C/C++ 程序在退出时产出一份可读回的事件流。

**Architecture:** 每个被插桩函数在进入/退出时触发钩子，钩子把 `(函数地址, 时间戳, 线程, ENTER/EXIT)` 写入该线程私有缓冲；进程退出时，运行时库把各线程缓冲连同“自己给自己解析出的符号表”序列化成 `.ctrace`。符号化与过滤都在运行期完成（读 `/proc/self/maps` + 各模块 ELF `.symtab/.dynsym`），因此分析器无需额外二进制即可显示函数名。

**Tech Stack:** C11（运行时库）、C++17（测试与 CLI）、CMake 3.16+、pthread、glibc、`<elf.h>`。零第三方依赖。

**参考规格:** `docs/superpowers/specs/2026-09-30-ctiming-design.md`（第 5、6、9、10、11、12 节）

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `include/ctiming.h` | 公共 API（C 兼容） |
| `src/ct_common.h` | 私有：`CT_NOINSTR` 等宏 |
| `src/glob.h` / `src/glob.c` | 通配符匹配（`*`、`?`）与逗号分隔过滤判定 |
| `src/buffer.h` / `src/buffer.c` | 每线程事件缓冲（自动扩容、丢弃计数） |
| `src/symbols.h` / `src/symbols.c` | 自符号化：`/proc/self/maps` + ELF `.symtab/.dynsym` + demangle |
| `src/config.h` / `src/config.c` | 读取 `CTIMING_*` 环境变量 |
| `src/trace.h` / `src/trace.c` | `.ctrace` 写出（writer）与读回（reader） |
| `src/runtime.c` | 钩子、惰性初始化、深度/过滤、atexit 导出、公共 API 实现 |
| `tools/ctiming-info.c` | 最小读回 CLI，打印 trace 摘要 |
| `examples/example_single.c` | 端到端示例（被插桩） |
| `tests/check.h` | 极简测试断言宏 |
| `tests/test_glob.c`、`tests/test_buffer.c`、`tests/test_trace.c`、`tests/test_symbols.c`、`tests/test_config.c` | 单元测试 |
| `tests/integration_test.sh` | 端到端：编译示例→运行→`ctiming-info` 校验 |
| `CMakeLists.txt` | 构建与 CTest |
| `.gitignore` | 已有 |

**约定：** 运行时库中所有函数定义都加 `CT_NOINSTR`（`__attribute__((no_instrument_function))`），保证库自身被 `-finstrument-functions` 编译时不会自我递归。所有字符串路径长度用 `uint32_t` 前缀序列化；整数变长编码用无符号 LEB128。

---

## Task 1: 项目骨架与公共头文件

**Files:**
- Create: `include/ctiming.h`
- Create: `src/ct_common.h`
- Create: `tests/check.h`
- Create: `tests/test_smoke.c`
- Create: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/check.h` + `tests/test_smoke.c`**

`tests/check.h`:

```c
#ifndef CT_CHECK_H
#define CT_CHECK_H
#include <stdio.h>
#define CHECK(cond) do { \
  if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } \
} while (0)
#define CHECK_EQ_LONG(a, b) do { \
  long _a = (long)(a), _b = (long)(b); \
  if (_a != _b) { fprintf(stderr, "FAIL %s:%d: %s=%ld != %s=%ld\n", __FILE__, __LINE__, #a, _a, #b, _b); fails++; } \
} while (0)
#endif
```

`tests/test_smoke.c`:

```c
#include "check.h"
#include "ctiming.h"
#include <string.h>

int main(void) {
  int fails = 0;
  const char *v = ctiming_version();
  CHECK(v != NULL);
  CHECK(strlen(v) > 0);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake -S . -B build && cmake --build build 2>&1 | tail`（此时还没有 CMakeLists，先创建下面的文件再构建；预期 `ctiming_version` 未定义 / 头文件缺失而失败）

- [ ] **Step 3: 写公共头 `include/ctiming.h` 与私有头 `src/ct_common.h`**

`include/ctiming.h`:

```c
#ifndef CTIMING_H
#define CTIMING_H

#ifdef __cplusplus
extern "C" {
#endif

void ctiming_start(void);
void ctiming_stop(void);
int  ctiming_dump(const char *path);
void ctiming_set_filter(const char *include, const char *exclude);
void ctiming_set_max_depth(unsigned depth);
const char *ctiming_version(void);

#ifdef __cplusplus
}
#endif

#endif
```

`src/ct_common.h`:

```c
#ifndef CT_COMMON_H
#define CT_COMMON_H

#define CT_NOINSTR __attribute__((no_instrument_function))
#define CT_VERSION_STRING "0.1.0"

#endif
```

- [ ] **Step 4: 占位实现 `src/runtime.c`（仅 `ctiming_version`，后续 Task 覆盖）**

```c
#include "ct_common.h"
#include "ctiming.h"

CT_NOINSTR const char *ctiming_version(void) { return CT_VERSION_STRING; }
```

- [ ] **Step 5: 写 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(ctiming C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)
enable_testing()

add_library(ctiming STATIC src/runtime.c)
target_include_directories(ctiming PUBLIC include PRIVATE src)
target_link_libraries(ctiming PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

add_executable(ctiming-info tools/ctiming-info.c)
target_include_directories(ctiming-info PRIVATE include src)
target_link_libraries(ctiming-info PRIVATE ctiming)

function(ct_add_test name file)
  add_executable(${name} ${file})
  target_include_directories(${name} PRIVATE include src tests)
  target_link_libraries(${name} PRIVATE ctiming)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ctiming-info.c)
  ct_add_test(test_smoke tests/test_smoke.c)
endif()
```

同时创建占位 `tools/ctiming-info.c`（后面 Task 8 覆盖）：

```c
#include <stdio.h>
int main(void) { printf("ctiming-info stub\n"); return 0; }
```

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `test_smoke` PASS。

- [ ] **Step 7: 提交**

```bash
git add include src tests tools CMakeLists.txt
git commit -m "build: ctiming 骨架与公共头文件"
```

---

## Task 2: 通配符匹配与过滤判定

**Files:**
- Create: `src/glob.h`
- Create: `src/glob.c`
- Create: `tests/test_glob.c`
- Modify: `CMakeLists.txt`（把 `src/glob.c` 加入库、注册测试）

- [ ] **Step 1: 写失败测试 `tests/test_glob.c`**

```c
#include "check.h"
#include "glob.h"

int main(void) {
  int fails = 0;
  CHECK_EQ_LONG(ct_glob_match("foo*", "foobar"), 1);
  CHECK_EQ_LONG(ct_glob_match("foo*", "barfoo"), 0);
  CHECK_EQ_LONG(ct_glob_match("*bar", "foobar"), 1);
  CHECK_EQ_LONG(ct_glob_match("f?o", "foo"), 1);
  CHECK_EQ_LONG(ct_glob_match("f?o", "fooo"), 0);
  CHECK_EQ_LONG(ct_glob_match("*", "anything"), 1);
  CHECK_EQ_LONG(ct_glob_match("std::*", "std::vector"), 1);
  CHECK_EQ_LONG(ct_glob_match("std::*", "std::"), 1);
  CHECK_EQ_LONG(ct_glob_match("a*b*c", "aXXbYYc"), 1);
  CHECK_EQ_LONG(ct_glob_match("a*b*c", "aXXcYYb"), 0);

  CHECK_EQ_LONG(ct_filter_match(NULL, NULL, "foo"), 1);
  CHECK_EQ_LONG(ct_filter_match("foo*,bar*", NULL, "foobar"), 1);
  CHECK_EQ_LONG(ct_filter_match("foo*,bar*", NULL, "baz"), 0);
  CHECK_EQ_LONG(ct_filter_match(NULL, "*std::*", "std::vector"), 0);
  CHECK_EQ_LONG(ct_filter_match("foo*", "foo_bar*", "foo_bar_x"), 0);
  CHECK_EQ_LONG(ct_filter_match("foo*", "foo_bar*", "foo_qux"), 1);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R test_glob --output-on-failure`
Expected: 编译失败（`glob.h` 不存在）。

- [ ] **Step 3: 写 `src/glob.h` 与 `src/glob.c`**

`src/glob.h`:

```c
#ifndef CT_GLOB_H
#define CT_GLOB_H

int ct_glob_match(const char *pattern, const char *str);
/* include/exclude 均为逗号分隔的 glob 列表，可为 NULL。
   语义：exclude 命中即 0；否则 include 为空则 1，否则需命中 include。 */
int ct_filter_match(const char *include, const char *exclude, const char *name);

#endif
```

`src/glob.c`:

```c
#include "glob.h"
#include "ct_common.h"
#include <string.h>

CT_NOINSTR static int glob_here(const char *p, const char *s) {
  while (*p) {
    if (*p == '*') {
      while (*p == '*') p++;
      if (!*p) return 1;
      for (; *s; s++) if (glob_here(p, s)) return 1;
      return glob_here(p, s);
    } else if (*p == '?') {
      if (!*s) return 0;
      p++; s++;
    } else {
      if (*p != *s) return 0;
      p++; s++;
    }
  }
  return *s == '\0';
}

CT_NOINSTR int ct_glob_match(const char *pattern, const char *str) {
  if (!pattern || !str) return 0;
  return glob_here(pattern, str);
}

CT_NOINSTR static int list_match(const char *list, const char *name) {
  if (!list || !*list) return 0;
  const char *p = list;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char pat[512];
    size_t n = len < sizeof(pat) - 1 ? len : sizeof(pat) - 1;
    memcpy(pat, p, n);
    pat[n] = '\0';
    /* 跳过前后空白 */
    char *b = pat; while (*b == ' ' || *b == '\t') b++;
    char *e = b + strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    if (*b && ct_glob_match(b, name)) return 1;
    if (!comma) break;
    p = comma + 1;
  }
  return 0;
}

CT_NOINSTR int ct_filter_match(const char *include, const char *exclude, const char *name) {
  if (exclude && list_match(exclude, name)) return 0;
  if (include && *include) return list_match(include, name);
  return 1;
}
```

- [ ] **Step 4: 更新 `CMakeLists.txt`**

把库源文件改为：

```cmake
add_library(ctiming STATIC src/glob.c src/runtime.c)
```

在 `test_smoke` 之后加：

```cmake
ct_add_test(test_glob tests/test_glob.c)
```

- [ ] **Step 5: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `test_smoke`、`test_glob` 全 PASS。

- [ ] **Step 6: 提交**

```bash
git add src tests CMakeLists.txt
git commit -m "feat: 通配符匹配与 include/exclude 过滤判定"
```

---

## Task 3: 每线程事件缓冲

**Files:**
- Create: `src/buffer.h`
- Create: `src/buffer.c`
- Create: `tests/test_buffer.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_buffer.c`**

```c
#include "check.h"
#include "buffer.h"

int main(void) {
  int fails = 0;
  ct_buffer *b = ct_buffer_new(2);
  CHECK(b != NULL);
  CHECK_EQ_LONG(b->count, 0);
  CHECK_EQ_LONG(b->dropped, 0);

  ct_event e = { .tid = 7, .ts = 100, .fn = 0x1000, .call_site = 0, .kind = CT_EV_ENTER };
  ct_buffer_push(b, e);
  e.ts = 200; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  CHECK_EQ_LONG(b->count, 2);
  CHECK_EQ_LONG(b->data[0].fn, 0x1000);
  CHECK_EQ_LONG(b->data[0].kind, CT_EV_ENTER);
  CHECK_EQ_LONG(b->data[1].ts, 200);

  /* 扩容：连续压入足够多事件，容量应增长 */
  size_t cap_before = b->cap;
  for (int i = 0; i < 200; i++) { e.ts = 1000 + (uint64_t)i; ct_buffer_push(b, e); }
  CHECK_EQ_LONG(b->count, 202);
  CHECK(b->cap > cap_before);
  CHECK_EQ_LONG(b->data[201].ts, 1199);

  ct_buffer_free(b);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R test_buffer --output-on-failure`
Expected: 编译失败（`buffer.h` 不存在）。

- [ ] **Step 3: 写 `src/buffer.h` 与 `src/buffer.c`**

`src/buffer.h`:

```c
#ifndef CT_BUFFER_H
#define CT_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef enum { CT_EV_ENTER = 0, CT_EV_EXIT = 1 } ct_event_kind;

typedef struct {
  uint32_t tid;
  uint8_t  kind;
  uint64_t ts;
  uintptr_t fn;
  uintptr_t call_site;
} ct_event;

typedef struct ct_buffer {
  ct_event *data;
  size_t count;
  size_t cap;
  size_t dropped;
  struct ct_buffer *next; /* 用于退役链表 */
} ct_buffer;

ct_buffer *ct_buffer_new(size_t initial_cap);
void ct_buffer_free(ct_buffer *b);
/* 返回 1 成功，0 表示因分配失败被丢弃（dropped++）。追加过程中不触发用户代码。 */
int ct_buffer_push(ct_buffer *b, ct_event ev);

#endif
```

`src/buffer.c`:

```c
#include "buffer.h"
#include "ct_common.h"
#include <stdlib.h>

CT_NOINSTR ct_buffer *ct_buffer_new(size_t initial_cap) {
  if (initial_cap < 16) initial_cap = 16;
  ct_buffer *b = (ct_buffer *)calloc(1, sizeof(ct_buffer));
  if (!b) return NULL;
  b->data = (ct_event *)calloc(initial_cap, sizeof(ct_event));
  if (!b->data) { free(b); return NULL; }
  b->cap = initial_cap;
  return b;
}

CT_NOINSTR void ct_buffer_free(ct_buffer *b) {
  if (!b) return;
  free(b->data);
  free(b);
}

CT_NOINSTR static int buffer_grow(ct_buffer *b) {
  size_t ncap = b->cap * 2;
  ct_event *nd = (ct_event *)realloc(b->data, ncap * sizeof(ct_event));
  if (!nd) return 0;
  b->data = nd;
  b->cap = ncap;
  return 1;
}

CT_NOINSTR int ct_buffer_push(ct_buffer *b, ct_event ev) {
  if (b->count == b->cap) {
    if (!buffer_grow(b)) { b->dropped++; return 0; }
  }
  b->data[b->count++] = ev;
  return 1;
}
```

- [ ] **Step 4: 更新 `CMakeLists.txt`**

```cmake
add_library(ctiming STATIC src/glob.c src/buffer.c src/runtime.c)
ct_add_test(test_buffer tests/test_buffer.c)
```

- [ ] **Step 5: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 三个测试全 PASS。

- [ ] **Step 6: 提交**

```bash
git add src tests CMakeLists.txt
git commit -m "feat: 每线程事件缓冲"
```

---

## Task 4: `.ctrace` 写出与读回

**Files:**
- Create: `src/trace.h`
- Create: `src/trace.c`
- Create: `tests/test_trace.c`
- Modify: `CMakeLists.txt`

**格式（小端）：**

```
Header:  magic "CTMG"(4) | version u16 | endian u8=1 | ptr_size u8 | pid u32
         start_ns u64 | flags u32 | exe_len u32 + exe_bytes
Modules: n_modules u32 | 每条 { base u64 | path: u32_len + bytes }
Symbols: n_symbols u32 | 每条 { module u32 | offset u64 | name: u32_len + bytes }
Threads: n_threads u32 | 每线程 { tid u32 | n_events u32 | 事件序列 }
  事件: fn_id varint | ts_delta varint | flags u8（bit0: kind；bit1: 有 call_site）
        | 若 bit1: call_site u64
Footer:  total_events u32 | dropped u32 | magic_end "CTME"(4)
```

**约定：** `ts_delta` 为本线程内相对上一事件的时间差（首个事件相对该线程首事件为 0 基准，直接存原始 ts）；`fn_id` 为符号表索引。

- [ ] **Step 1: 写失败测试 `tests/test_trace.c`**

```c
#include "check.h"
#include "trace.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
  int fails = 0;
  const char *path = "/tmp/ct_test_trace.bin";
  unlink(path);

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = 4242;
  meta.start_ns = 1000000ULL;
  meta.flags = 0;
  strcpy(meta.exe, "/bin/app");

  ct_trace_module mods[1];
  mods[0].base = 0x400000ULL;
  strcpy(mods[0].path, "/bin/app");

  ct_trace_symbol syms[2];
  memset(syms, 0, sizeof(syms));
  syms[0].module = 0; syms[0].offset = 0x1000ULL; strcpy(syms[0].name, "main");
  syms[1].module = 0; syms[1].offset = 0x2000ULL; strcpy(syms[1].name, "foo");

  ct_buffer *b = ct_buffer_new(4);
  ct_event e = { .tid = 1, .ts = 1000, .fn = 0x401000ULL, .call_site = 0, .kind = CT_EV_ENTER };
  ct_buffer_push(b, e);
  e.ts = 1500; e.fn = 0x402000ULL; e.kind = CT_EV_ENTER;
  ct_buffer_push(b, e);
  e.ts = 1700; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  e.ts = 2000; e.fn = 0x401000ULL; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);

  const ct_buffer *bufs[1] = { b };
  CHECK_EQ_LONG(ct_trace_write(path, bufs, 1, mods, 1, syms, 2, &meta), 0);

  ct_trace_reader r;
  CHECK_EQ_LONG(ct_trace_open(path, &r), 0);
  CHECK_EQ_LONG(r.header.pid, 4242);
  CHECK_EQ_LONG(r.header.start_ns, 1000000ULL);
  CHECK_EQ_LONG(r.n_modules, 1);
  CHECK_EQ_LONG(r.n_symbols, 2);
  CHECK_EQ_LONG(r.total_events, 4);
  CHECK_EQ_LONG(r.dropped, 0);
  CHECK(strcmp(r.modules[0].path, "/bin/app") == 0);
  CHECK_EQ_LONG(r.modules[0].base, 0x400000ULL);
  CHECK(strcmp(r.symbols[0].name, "main") == 0);
  CHECK_EQ_LONG(r.symbols[1].offset, 0x2000ULL);

  ct_trace_thread *t = &r.threads[0];
  CHECK_EQ_LONG(t->tid, 1);
  CHECK_EQ_LONG(t->n_events, 4);
  CHECK_EQ_LONG(t->events[0].fn_id, 0);
  CHECK_EQ_LONG(t->events[0].kind, CT_EV_ENTER);
  CHECK_EQ_LONG(t->events[0].ts, 1000);
  CHECK_EQ_LONG(t->events[1].fn_id, 1);
  CHECK_EQ_LONG(t->events[2].kind, CT_EV_EXIT);
  CHECK_EQ_LONG(t->events[3].ts, 2000);

  ct_trace_close(&r);
  ct_buffer_free(b);
  unlink(path);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R test_trace --output-on-failure`
Expected: 编译失败（`trace.h` 不存在）。

- [ ] **Step 3: 写 `src/trace.h`**

```c
#ifndef CT_TRACE_H
#define CT_TRACE_H

#include "buffer.h"
#include <stdint.h>
#include <stddef.h>

#define CT_PATH_MAX 512

typedef struct {
  uint32_t pid;
  uint64_t start_ns;
  uint32_t flags;
  char exe[CT_PATH_MAX];
} ct_trace_meta;

typedef struct {
  uint64_t base;
  char path[CT_PATH_MAX];
} ct_trace_module;

typedef struct {
  uint32_t module;   /* 模块索引；0xFFFFFFFF 表示未知 */
  uint64_t offset;   /* 相对模块 base 的偏移；未知时即原始地址 */
  char name[256];
} ct_trace_symbol;

int ct_trace_write(const char *path,
                   const ct_buffer *const *bufs, size_t nbufs,
                   const ct_trace_module *mods, size_t nmods,
                   const ct_trace_symbol *syms, size_t nsyms,
                   const ct_trace_meta *meta);

typedef struct {
  uint32_t fn_id;
  uint8_t  kind;
  uint64_t ts;        /* 已由 reader 还原为绝对时间戳 */
  uint64_t call_site;
  int      has_call_site;
} ct_trace_event;

typedef struct {
  uint32_t tid;
  uint32_t n_events;
  ct_trace_event *events;
} ct_trace_thread;

typedef struct {
  ct_trace_meta header;
  ct_trace_module *modules;
  uint32_t n_modules;
  ct_trace_symbol *symbols;
  uint32_t n_symbols;
  ct_trace_thread *threads;
  uint32_t n_threads;
  uint32_t total_events;
  uint32_t dropped;
} ct_trace_reader;

int ct_trace_open(const char *path, ct_trace_reader *r);
void ct_trace_close(ct_trace_reader *r);

#endif
```

- [ ] **Step 4: 写 `src/trace.c`**

```c
#include "trace.h"
#include "ct_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CT_MAGIC "CTMG"
#define CT_MAGIC_END "CTME"
#define CT_VERSION 1
#define CT_UNKNOWN_MODULE 0xFFFFFFFFu

/* ---------- 写 ---------- */
CT_NOINSTR static void put_u8(FILE *f, uint8_t v) { fputc(v, f); }
CT_NOINSTR static void put_u16(FILE *f, uint16_t v) { uint8_t b[2]={(uint8_t)v,(uint8_t)(v>>8)}; fwrite(b,1,2,f); }
CT_NOINSTR static void put_u32(FILE *f, uint32_t v) { uint8_t b[4]; for(int i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); fwrite(b,1,4,f); }
CT_NOINSTR static void put_u64(FILE *f, uint64_t v) { uint8_t b[8]; for(int i=0;i<8;i++) b[i]=(uint8_t)(v>>(8*i)); fwrite(b,1,8,f); }
CT_NOINSTR static void put_str(FILE *f, const char *s) { size_t n=strlen(s); put_u32(f,(uint32_t)n); fwrite(s,1,n,f); }
CT_NOINSTR static void put_varint(FILE *f, uint64_t v) {
  while (v >= 0x80) { put_u8(f,(uint8_t)(v|0x80)); v >>= 7; }
  put_u8(f,(uint8_t)v);
}

/* 把事件的函数地址映射为符号索引：命中返回下标，否则返回 -1。 */
CT_NOINSTR static long find_symbol_addr(uint64_t addr, const ct_buffer *b,
                                        const ct_trace_module *mods, size_t nmods,
                                        const ct_trace_symbol *syms, size_t nsyms) {
  (void)b; (void)mods; (void)nmods;
  for (size_t i = 0; i < nsyms; i++) {
    if (syms[i].module == CT_UNKNOWN_MODULE) continue;
    uint64_t a = mods[syms[i].module].base + syms[i].offset;
    if (a == addr) return (long)i;
  }
  return -1;
}

CT_NOINSTR int ct_trace_write(const char *path,
                              const ct_buffer *const *bufs, size_t nbufs,
                              const ct_trace_module *mods, size_t nmods,
                              const ct_trace_symbol *syms, size_t nsyms,
                              const ct_trace_meta *meta) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;

  fwrite(CT_MAGIC, 1, 4, f);
  put_u16(f, CT_VERSION);
  put_u8(f, 1);
  put_u8(f, (uint8_t)sizeof(void *));
  put_u32(f, meta->pid);
  put_u64(f, meta->start_ns);
  put_u32(f, meta->flags);
  put_str(f, meta->exe);

  put_u32(f, (uint32_t)nmods);
  for (size_t i = 0; i < nmods; i++) { put_u64(f, mods[i].base); put_str(f, mods[i].path); }

  put_u32(f, (uint32_t)nsyms);
  for (size_t i = 0; i < nsyms; i++) {
    put_u32(f, syms[i].module); put_u64(f, syms[i].offset); put_str(f, syms[i].name);
  }

  /* 未知地址收集：动态追加符号 */
  ct_trace_symbol *extra = NULL;
  size_t nextra = 0, capextra = 0;

  put_u32(f, (uint32_t)nbufs);
  uint32_t total = 0, dropped = 0;
  for (size_t bi = 0; bi < nbufs; bi++) {
    const ct_buffer *b = bufs[bi];
    put_u32(f, b->data && b->count ? b->data[0].tid : 0);
    put_u32(f, (uint32_t)b->count);
    uint64_t prev_ts = 0;
    for (size_t i = 0; i < b->count; i++) {
      const ct_event *e = &b->data[i];
      long id = find_symbol_addr(e->fn, b, mods, nmods, syms, nsyms);
      if (id < 0) {
        id = (long)(nsyms + nextra);
        if (nextra == capextra) {
          size_t nc = capextra ? capextra * 2 : 8;
          ct_trace_symbol *ne = (ct_trace_symbol *)realloc(extra, nc * sizeof(*ne));
          if (ne) { extra = ne; capextra = nc; }
        }
        memset(&extra[nextra], 0, sizeof(extra[nextra]));
        extra[nextra].module = CT_UNKNOWN_MODULE;
        extra[nextra].offset = e->fn;
        nextra++;
      }
      put_varint(f, (uint64_t)id);
      put_varint(f, i == 0 ? e->ts : e->ts - prev_ts);
      prev_ts = e->ts;
      uint8_t fl = (uint8_t)(e->kind & 1);
      if (e->call_site) fl |= 2;
      put_u8(f, fl);
      if (fl & 2) put_u64(f, e->call_site);
      total++;
    }
    dropped += (uint32_t)b->dropped;
  }

  /* 补写未知符号：追加到 symbols 区之后不可行（已在前面写完），
     因此把未知符号写成一段“EXTRA”区，reader 会合并。 */
  put_u32(f, (uint32_t)nextra);
  for (size_t i = 0; i < nextra; i++) {
    put_u32(f, extra[i].module); put_u64(f, extra[i].offset); put_str(f, extra[i].name);
  }
  free(extra);

  put_u32(f, total);
  put_u32(f, dropped);
  fwrite(CT_MAGIC_END, 1, 4, f);

  int rc = fflush(f);
  fclose(f);
  return rc == 0 ? 0 : -1;
}

/* ---------- 读 ---------- */
CT_NOINSTR static int get_u8(FILE *f, uint8_t *v) { int c=fgetc(f); if(c<0) return -1; *v=(uint8_t)c; return 0; }
CT_NOINSTR static int get_u16(FILE *f, uint16_t *v) { uint8_t b[2]; if(fread(b,1,2,f)!=2) return -1; *v=(uint16_t)(b[0]|(b[1]<<8)); return 0; }
CT_NOINSTR static int get_u32(FILE *f, uint32_t *v) { uint8_t b[4]; if(fread(b,1,4,f)!=4) return -1; *v=0; for(int i=0;i<4;i++) *v|=(uint32_t)b[i]<<(8*i); return 0; }
CT_NOINSTR static int get_u64(FILE *f, uint64_t *v) { uint8_t b[8]; if(fread(b,1,8,f)!=8) return -1; *v=0; for(int i=0;i<8;i++) *v|=(uint64_t)b[i]<<(8*i); return 0; }
CT_NOINSTR static int get_str(FILE *f, char *out, size_t cap) {
  uint32_t n; if (get_u32(f,&n)) return -1;
  if (n >= cap) { /* 丢弃多余 */ char tmp[256]; uint32_t left=n; while(left){ size_t k=left>sizeof(tmp)?sizeof(tmp):left; if(fread(tmp,1,k,f)!=k) return -1; left-=(uint32_t)k; } return -1; }
  if (n && fread(out,1,n,f)!=n) return -1;
  out[n]='\0'; return 0;
}
CT_NOINSTR static int get_varint(FILE *f, uint64_t *v) {
  *v=0; int shift=0;
  for(;;){ uint8_t b; if(get_u8(f,&b)) return -1; *v |= (uint64_t)(b&0x7f)<<shift; if(!(b&0x80)) break; shift+=7; if(shift>63) return -1; }
  return 0;
}

CT_NOINSTR int ct_trace_open(const char *path, ct_trace_reader *r) {
  memset(r, 0, sizeof(*r));
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  char magic[4];
  if (fread(magic,1,4,f)!=4 || memcmp(magic, CT_MAGIC, 4)!=0) { fclose(f); return -2; }
  uint16_t ver; uint8_t endian, ptr; uint32_t n;
  if (get_u16(f,&ver) || get_u8(f,&endian) || get_u8(f,&ptr)) { fclose(f); return -2; }
  if (ver != CT_VERSION || endian != 1) { fclose(f); return -3; }
  if (get_u32(f,&r->header.pid) || get_u64(f,&r->header.start_ns) ||
      get_u32(f,&r->header.flags) || get_str(f, r->header.exe, CT_PATH_MAX)) { fclose(f); return -2; }

  if (get_u32(f,&n)) { fclose(f); return -2; }
  r->n_modules = n;
  r->modules = n ? (ct_trace_module *)calloc(n, sizeof(ct_trace_module)) : NULL;
  for (uint32_t i=0;i<n;i++) { if (get_u64(f,&r->modules[i].base) || get_str(f,r->modules[i].path,CT_PATH_MAX)) { fclose(f); return -2; } }

  uint32_t nsym;
  if (get_u32(f,&nsym)) { fclose(f); return -2; }
  r->n_symbols = nsym;
  r->symbols = nsym ? (ct_trace_symbol *)calloc(nsym, sizeof(ct_trace_symbol)) : NULL;
  for (uint32_t i=0;i<nsym;i++) {
    uint32_t m; if (get_u32(f,&m) || get_u64(f,&r->symbols[i].offset) || get_str(f,r->symbols[i].name,256)) { fclose(f); return -2; }
    r->symbols[i].module = m;
  }

  uint32_t nthr;
  if (get_u32(f,&nthr)) { fclose(f); return -2; }
  r->n_threads = nthr;
  r->threads = nthr ? (ct_trace_thread *)calloc(nthr, sizeof(ct_trace_thread)) : NULL;
  for (uint32_t ti=0; ti<nthr; ti++) {
    uint32_t tid, ne;
    if (get_u32(f,&tid) || get_u32(f,&ne)) { fclose(f); return -2; }
    r->threads[ti].tid = tid;
    r->threads[ti].n_events = ne;
    r->threads[ti].events = ne ? (ct_trace_event *)calloc(ne, sizeof(ct_trace_event)) : NULL;
    uint64_t ts = 0;
    for (uint32_t i=0;i<ne;i++) {
      uint64_t fid, delta; uint8_t fl;
      if (get_varint(f,&fid) || get_varint(f,&delta) || get_u8(f,&fl)) { fclose(f); return -2; }
      if (i) ts += delta; else ts = delta;
      r->threads[ti].events[i].fn_id = (uint32_t)fid;
      r->threads[ti].events[i].ts = ts;
      r->threads[ti].events[i].kind = (uint8_t)(fl & 1);
      r->threads[ti].events[i].has_call_site = (fl & 2) ? 1 : 0;
      if (fl & 2) { if (get_u64(f,&r->threads[ti].events[i].call_site)) { fclose(f); return -2; } }
    }
  }

  /* 未知符号 EXTRA 区 */
  uint32_t nextra;
  if (get_u32(f,&nextra)) { fclose(f); return -2; }
  if (nextra) {
    uint32_t merged = r->n_symbols + nextra;
    ct_trace_symbol *ns = (ct_trace_symbol *)realloc(r->symbols, merged * sizeof(ct_trace_symbol));
    if (!ns) { fclose(f); return -2; }
    r->symbols = ns;
    for (uint32_t i=0;i<nextra;i++) {
      uint32_t m; uint64_t off;
      if (get_u32(f,&m) || get_u64(f,&off) || get_str(f, r->symbols[r->n_symbols].name, 256)) { fclose(f); return -2; }
      r->symbols[r->n_symbols].module = m;
      r->symbols[r->n_symbols].offset = off;
      r->n_symbols++;
    }
  }

  if (get_u32(f,&r->total_events) || get_u32(f,&r->dropped)) { fclose(f); return -2; }
  char me[4];
  if (fread(me,1,4,f)!=4 || memcmp(me, CT_MAGIC_END, 4)!=0) { fclose(f); return -2; }

  fclose(f);
  return 0;
}

CT_NOINSTR void ct_trace_close(ct_trace_reader *r) {
  if (!r) return;
  for (uint32_t i=0;i<r->n_threads;i++) free(r->threads[i].events);
  free(r->threads);
  free(r->symbols);
  free(r->modules);
  memset(r, 0, sizeof(*r));
}
```

- [ ] **Step 5: 更新 `CMakeLists.txt`**

```cmake
add_library(ctiming STATIC src/glob.c src/buffer.c src/trace.c src/runtime.c)
ct_add_test(test_trace tests/test_trace.c)
```

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全部 PASS（尤其 `test_trace`）。

- [ ] **Step 7: 提交**

```bash
git add src tests CMakeLists.txt
git commit -m "feat: .ctrace 文件格式写出与读回"
```

---

## Task 5: 自符号化（/proc/self/maps + ELF）

**Files:**
- Create: `src/symbols.h`
- Create: `src/symbols.c`
- Create: `tests/test_symbols.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_symbols.c`**

```c
#include "check.h"
#include "symbols.h"
#include <string.h>

static void dummy_target(void) { }

int main(void) {
  int fails = 0;
  ct_symbol_table t;
  CHECK_EQ_LONG(ct_symbols_load(&t), 0);
  CHECK(t.n_modules >= 1);
  CHECK(t.n_symbols >= 1);

  /* 本可执行文件里的 main 应能被解析出名字 */
  const char *name = ct_symbols_lookup(&t, (uintptr_t)&main, NULL, NULL);
  CHECK(name != NULL);
  if (name) CHECK(strstr(name, "main") != NULL);

  /* dummy_target 是 static，仍应能解析（因为我们读 .symtab） */
  const char *dt = ct_symbols_lookup(&t, (uintptr_t)&dummy_target, NULL, NULL);
  CHECK(dt != NULL);
  if (dt) CHECK(strstr(dt, "dummy_target") != NULL);

  uintptr_t off = 123;
  CHECK_EQ_LONG(ct_symbols_lookup(&t, (uintptr_t)0xdeadbeefULL, &off, NULL), 0);
  CHECK_EQ_LONG(off, 123);

  ct_symbols_free(&t);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

> 说明：本测试用 `main`/`dummy_target` 验证自符号化；两函数都是本可执行文件内的函数，应能在 ELF `.symtab` 中找到。注意编译本测试时**不要**加 `-finstrument-functions`（否则会与未初始化的运行时交互，且无必要）。

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R test_symbols --output-on-failure`
Expected: 编译失败（`symbols.h` 不存在）。

- [ ] **Step 3: 写 `src/symbols.h`**

```c
#ifndef CT_SYMBOLS_H
#define CT_SYMBOLS_H

#include "trace.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint64_t addr;   /* 绝对运行地址 */
  char name[256];
  uint32_t module; /* ct_symbol_table.modules 的索引 */
} ct_sym_entry;

typedef struct {
  ct_trace_module *modules;
  size_t n_modules;
  ct_sym_entry *syms;
  size_t n_symbols;
} ct_symbol_table;

int ct_symbols_load(ct_symbol_table *t);
void ct_symbols_free(ct_symbol_table *t);

/* 查找 addr：命中返回函数名（demangle 后），并填充 offset（相对模块 base）
   与 module（模块索引，可为 NULL）。未命中返回 NULL。 */
const char *ct_symbols_lookup(const ct_symbol_table *t, uintptr_t addr,
                              uintptr_t *offset, uint32_t *module);

#endif
```

- [ ] **Step 4: 写 `src/symbols.c`**

```c
#include "symbols.h"
#include "ct_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

/* libstdc++ 提供的 demangle；用弱符号，保证纯 C 程序不链接 libstdc++ 也能跑。 */
extern char *__cxa_demangle(const char *mangled, char *buf, size_t *len, int *status)
    __attribute__((weak));

CT_NOINSTR static int add_module(ct_symbol_table *t, uint64_t base, const char *path) {
  ct_trace_module *nm = (ct_trace_module *)realloc(t->modules, (t->n_modules + 1) * sizeof(ct_trace_module));
  if (!nm) return -1;
  t->modules = nm;
  t->modules[t->n_modules].base = base;
  snprintf(t->modules[t->n_modules].path, CT_PATH_MAX, "%s", path);
  t->n_modules++;
  return (int)(t->n_modules - 1);
}

CT_NOINSTR static void add_symbol(ct_symbol_table *t, uint64_t addr, const char *name, uint32_t module) {
  ct_sym_entry *ns = (ct_sym_entry *)realloc(t->syms, (t->n_symbols + 1) * sizeof(ct_sym_entry));
  if (!ns) return;
  t->syms = ns;
  t->syms[t->n_symbols].addr = addr;
  t->syms[t->n_symbols].module = module;
  snprintf(t->syms[t->n_symbols].name, sizeof(t->syms[t->n_symbols].name), "%s", name);
  t->n_symbols++;
}

CT_NOINSTR static void maybe_demangle(const char *raw, char *out, size_t cap) {
  if (__cxa_demangle) {
    int status = 0;
    char *d = __cxa_demangle(raw, NULL, NULL, &status);
    if (status == 0 && d) { snprintf(out, cap, "%s", d); free(d); return; }
  }
  snprintf(out, cap, "%s", raw);
}

/* 读取一个 ELF 文件的 .symtab/.dynsym，把 FUNC 符号加入表。base 为该模块加载基址。 */
CT_NOINSTR static void load_elf_symbols(ct_symbol_table *t, const char *path, uint64_t base, uint32_t module) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return;
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(Elf64_Ehdr)) { close(fd); return; }
  void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) { close(fd); return; }

  const unsigned char *p = (const unsigned char *)map;
  if (p[0] == 0x7f && p[1] == 'E' && p[2] == 'L' && p[3] == 'F'
      && p[EI_CLASS] == ELFCLASS64) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)map;
    const Elf64_Shdr *sh = (const Elf64_Shdr *)(p + eh->e_shoff);
    for (int si = 0; si < eh->e_shnum; si++) {
      if (sh[si].sh_type != SHT_SYMTAB && sh[si].sh_type != SHT_DYNSYM) continue;
      const Elf64_Sym *syms = (const Elf64_Sym *)(p + sh[si].sh_offset);
      size_t count = sh[si].sh_size / sizeof(Elf64_Sym);
      const char *strtab = (const char *)(p + sh[sh[si].sh_link].sh_offset);
      for (size_t i = 0; i < count; i++) {
        if (ELF64_ST_TYPE(syms[i].st_info) != STT_FUNC) continue;
        if (syms[i].st_name == 0 || syms[i].st_value == 0) continue;
        const char *raw = strtab + syms[i].st_name;
        char name[256];
        maybe_demangle(raw, name, sizeof(name));
        add_symbol(t, base + syms[i].st_value, name, module);
      }
    }
  }
  munmap(map, (size_t)st.st_size);
  close(fd);
}

CT_NOINSTR static int cmp_sym(const void *a, const void *b) {
  uint64_t x = ((const ct_sym_entry *)a)->addr, y = ((const ct_sym_entry *)b)->addr;
  return x < y ? -1 : (x > y ? 1 : 0);
}

CT_NOINSTR int ct_symbols_load(ct_symbol_table *t) {
  memset(t, 0, sizeof(*t));
  FILE *f = fopen("/proc/self/maps", "r");
  if (!f) return -1;

  char line[1024];
  char seen_path[256][CT_PATH_MAX];
  uint64_t seen_base[256];
  size_t nseen = 0;

  while (fgets(line, sizeof(line), f)) {
    unsigned long start = 0, end = 0, off = 0;
    char perms[8]; char path[CT_PATH_MAX];
    path[0] = '\0';
    int n = sscanf(line, "%lx-%lx %7s %lx %*s %*s %511[^\n]", &start, &end, perms, &off, path);
    (void)end;
    if (n < 5) continue;
    if (path[0] != '/') continue;
    uint64_t base = (uint64_t)start - (uint64_t)off;
    int dup = 0;
    for (size_t i = 0; i < nseen; i++) {
      if (strcmp(seen_path[i], path) == 0) { dup = 1; break; }
    }
    if (dup) continue;
    if (nseen >= 256) break;
    snprintf(seen_path[nseen], CT_PATH_MAX, "%s", path);
    seen_base[nseen] = base;
    nseen++;
  }
  fclose(f);

  for (size_t i = 0; i < nseen; i++) {
    int m = add_module(t, seen_base[i], seen_path[i]);
    if (m < 0) continue;
    load_elf_symbols(t, seen_path[i], seen_base[i], (uint32_t)m);
  }

  if (t->n_symbols > 1) qsort(t->syms, t->n_symbols, sizeof(ct_sym_entry), cmp_sym);
  return 0;
}

CT_NOINSTR void ct_symbols_free(ct_symbol_table *t) {
  free(t->modules);
  free(t->syms);
  memset(t, 0, sizeof(*t));
}

CT_NOINSTR const char *ct_symbols_lookup(const ct_symbol_table *t, uintptr_t addr,
                                         uintptr_t *offset, uint32_t *module) {
  /* 二分找 addr 之前最近的符号（允许地址落在函数体内部）。 */
  size_t lo = 0, hi = t->n_symbols, found = (size_t)-1;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (t->syms[mid].addr <= addr) { found = mid; lo = mid + 1; }
    else hi = mid;
  }
  if (found == (size_t)-1) return NULL;
  const ct_sym_entry *s = &t->syms[found];
  if (offset) *offset = (uintptr_t)(addr - s->addr);
  if (module) *module = s->module;
  return s->name;
}
```

> 局限（写入代码注释即可，无需额外任务）：仅支持 64 位 ELF；静态链接的非 PIE 可执行文件 `.symtab` 仍在，可正常解析；被 `strip` 的模块只能解析导出符号或退化为 `0x...`。

- [ ] **Step 5: 更新 `CMakeLists.txt`**

```cmake
add_library(ctiming STATIC src/glob.c src/buffer.c src/trace.c src/symbols.c src/runtime.c)
ct_add_test(test_symbols tests/test_symbols.c)
```

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R "test_symbols|test_trace|test_glob|test_buffer|test_smoke" --output-on-failure`
Expected: 全 PASS。

- [ ] **Step 7: 提交**

```bash
git add src tests CMakeLists.txt
git commit -m "feat: 自符号化（/proc/self/maps + ELF symtab/dynsym）"
```

---

## Task 6: 运行期配置（环境变量）

**Files:**
- Create: `src/config.h`
- Create: `src/config.c`
- Create: `tests/test_config.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_config.c`**

```c
#include "check.h"
#include "config.h"
#include <stdlib.h>

int main(void) {
  int fails = 0;
  /* 默认 */
  unsetenv("CTIMING_ENABLE"); unsetenv("CTIMING_MAX_DEPTH"); unsetenv("CTIMING_OUT");
  unsetenv("CTIMING_INCLUDE"); unsetenv("CTIMING_EXCLUDE");
  ct_config c;
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 1);
  CHECK_EQ_LONG(c.max_depth, 0);
  CHECK(c.include == NULL && c.exclude == NULL);
  CHECK(strcmp(c.out_path, "./myprog.ctrace") == 0);

  setenv("CTIMING_ENABLE", "off", 1);
  setenv("CTIMING_MAX_DEPTH", "5", 1);
  setenv("CTIMING_INCLUDE", "foo*,bar*", 1);
  setenv("CTIMING_EXCLUDE", "std::*", 1);
  setenv("CTIMING_OUT", "/tmp/x.ctrace", 1);
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 0);
  CHECK_EQ_LONG(c.max_depth, 5);
  CHECK(c.include && strcmp(c.include, "foo*,bar*") == 0);
  CHECK(c.exclude && strcmp(c.exclude, "std::*") == 0);
  CHECK(strcmp(c.out_path, "/tmp/x.ctrace") == 0);
  ct_config_clear(&c);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

> `check.h` 需要 `<string.h>`；在测试文件顶部已包含 `check.h`，请同时 `#include <string.h>`。若缺少，补上。

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R test_config --output-on-failure`
Expected: 编译失败（`config.h` 不存在）。

- [ ] **Step 3: 写 `src/config.h` 与 `src/config.c`**

`src/config.h`:

```c
#ifndef CT_CONFIG_H
#define CT_CONFIG_H

#include "trace.h"

typedef struct {
  int enabled;        /* 默认 1 */
  unsigned max_depth; /* 0 = 不限 */
  char *include;      /* 需 free，可能为 NULL */
  char *exclude;
  char out_path[CT_PATH_MAX];
} ct_config;

void ct_config_load(ct_config *c, const char *progname);
void ct_config_clear(ct_config *c);

#endif
```

`src/config.c`:

```c
#include "config.h"
#include "ct_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

CT_NOINSTR static char *dup_or_null(const char *s) {
  if (!s || !*s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

CT_NOINSTR void ct_config_load(ct_config *c, const char *progname) {
  memset(c, 0, sizeof(*c));
  const char *en = getenv("CTIMING_ENABLE");
  c->enabled = 1;
  if (en && (strcmp(en, "off") == 0 || strcmp(en, "0") == 0)) c->enabled = 0;

  const char *d = getenv("CTIMING_MAX_DEPTH");
  c->max_depth = d ? (unsigned)strtoul(d, NULL, 10) : 0;

  c->include = dup_or_null(getenv("CTIMING_INCLUDE"));
  c->exclude = dup_or_null(getenv("CTIMING_EXCLUDE"));

  const char *out = getenv("CTIMING_OUT");
  if (out && *out) snprintf(c->out_path, CT_PATH_MAX, "%s", out);
  else snprintf(c->out_path, CT_PATH_MAX, "./%s.ctrace", progname ? progname : "ctiming");
}

CT_NOINSTR void ct_config_clear(ct_config *c) {
  free(c->include);
  free(c->exclude);
  c->include = c->exclude = NULL;
}
```

- [ ] **Step 4: 更新 `CMakeLists.txt`**

```cmake
add_library(ctiming STATIC src/glob.c src/buffer.c src/trace.c src/symbols.c src/config.c src/runtime.c)
ct_add_test(test_config tests/test_config.c)
```

- [ ] **Step 5: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全 PASS。

- [ ] **Step 6: 提交**

```bash
git add src tests CMakeLists.txt
git commit -m "feat: 运行期配置（CTIMING_* 环境变量）"
```

---

## Task 7: 插桩钩子、线程缓冲注册、atexit 导出

**Files:**
- Modify: `src/runtime.c`（完整替换）
- Create: `examples/example_single.c`
- Create: `tests/integration_test.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 完整实现 `src/runtime.c`**

```c
#include "ct_common.h"
#include "ctiming.h"
#include "buffer.h"
#include "config.h"
#include "symbols.h"
#include "trace.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CT_UNKNOWN_MODULE 0xFFFFFFFFu

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_key_t  g_key;
static pthread_mutex_t g_retired_mu = PTHREAD_MUTEX_INITIALIZER;
static ct_buffer *g_retired_head = NULL;

static ct_config g_cfg;
static ct_symbol_table g_syms;
static char g_progname[256];
static _Atomic int g_started = 0;   /* 已初始化完成 */
static _Atomic int g_enabled = 1;

static __thread ct_buffer *tls_buf = NULL;
static __thread int tls_in_hook = 0;
static __thread unsigned tls_depth = 0;

CT_NOINSTR static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

CT_NOINSTR static void retire_thread_buffer(void *p) {
  ct_buffer *b = (ct_buffer *)p;
  if (!b) return;
  pthread_mutex_lock(&g_retired_mu);
  b->next = g_retired_head;
  g_retired_head = b;
  pthread_mutex_unlock(&g_retired_mu);
}

CT_NOINSTR static void dump_locked(void) {
  /* 收集：退役链表 + 当前线程 TLS */
  size_t cap = 16, n = 0;
  const ct_buffer **bufs = (const ct_buffer **)malloc(cap * sizeof(*bufs));
  if (!bufs) return;
  for (ct_buffer *b = g_retired_head; b; b = b->next) {
    if (b->count == 0) continue;
    if (n == cap) { cap *= 2; const ct_buffer **nb = realloc((void*)bufs, cap * sizeof(*bufs)); if (!nb) break; bufs = nb; }
    bufs[n++] = b;
  }
  if (tls_buf && tls_buf->count > 0) {
    if (n == cap) { cap *= 2; const ct_buffer **nb = realloc((void*)bufs, cap * sizeof(*bufs)); if (nb) bufs = nb; }
    if (n < cap) bufs[n++] = tls_buf;
  }

  /* 构造模块表与符号表 */
  ct_trace_module *mods = (ct_trace_module *)malloc(g_syms.n_modules * sizeof(ct_trace_module));
  for (size_t i = 0; i < g_syms.n_modules; i++) mods[i] = g_syms.modules[i];
  ct_trace_symbol *syms = (ct_trace_symbol *)malloc(g_syms.n_symbols * sizeof(ct_trace_symbol));
  for (size_t i = 0; i < g_syms.n_symbols; i++) {
    syms[i].module = g_syms.syms[i].module;
    syms[i].offset = g_syms.syms[i].addr - g_syms.modules[g_syms.syms[i].module].base;
    snprintf(syms[i].name, sizeof(syms[i].name), "%s", g_syms.syms[i].name);
  }

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = (uint32_t)getpid();
  meta.start_ns = now_ns();
  meta.flags = 0;
  snprintf(meta.exe, CT_PATH_MAX, "%s", g_syms.n_modules ? g_syms.modules[0].path : "");

  ct_trace_write(g_cfg.out_path, bufs, n, mods, g_syms.n_modules, syms, g_syms.n_symbols, &meta);

  free(syms);
  free(mods);
  free((void*)bufs);
}

CT_NOINSTR static void ct_atexit(void) {
  pthread_mutex_lock(&g_retired_mu);
  dump_locked();
  pthread_mutex_unlock(&g_retired_mu);
}

CT_NOINSTR static void init_once(void) {
  /* 程序名 */
  char exe[512];
  ssize_t k = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (k > 0) {
    exe[k] = '\0';
    const char *slash = strrchr(exe, '/');
    snprintf(g_progname, sizeof(g_progname), "%s", slash ? slash + 1 : exe);
  } else {
    snprintf(g_progname, sizeof(g_progname), "ctiming");
  }

  ct_config_load(&g_cfg, g_progname);
  ct_symbols_load(&g_syms);
  atomic_store(&g_enabled, g_cfg.enabled);

  pthread_key_create(&g_key, retire_thread_buffer);
  atexit(ct_atexit);
  atomic_store(&g_started, 1);
}

CT_NOINSTR static ct_buffer *current_buffer(void) {
  if (!tls_buf) {
    tls_buf = ct_buffer_new(8192);
    if (tls_buf) pthread_setspecific(g_key, tls_buf);
  }
  return tls_buf;
}

CT_NOINSTR static int pass_filter(uintptr_t fn) {
  if (!g_cfg.include && !g_cfg.exclude) return 1;
  const char *name = ct_symbols_lookup(&g_syms, fn, NULL, NULL);
  if (!name) return g_cfg.include ? 0 : 1;
  return ct_filter_match(g_cfg.include, g_cfg.exclude, name);
}

CT_NOINSTR static void record(uintptr_t fn, uintptr_t cs, ct_event_kind kind, unsigned depth) {
  if (!atomic_load(&g_enabled)) return;
  if (tls_in_hook) return;
  tls_in_hook = 1;

  pthread_once(&g_once, init_once);
  if (!atomic_load(&g_started)) { tls_in_hook = 0; return; }

  if ((g_cfg.max_depth == 0 || depth < g_cfg.max_depth) && pass_filter(fn)) {
    ct_buffer *b = current_buffer();
    if (b) {
      ct_event e;
      e.tid = (uint32_t)gettid();
      e.kind = (uint8_t)kind;
      e.ts = now_ns();
      e.fn = fn;
      e.call_site = cs;
      ct_buffer_push(b, e);
    }
  }
  tls_in_hook = 0;
}

CT_NOINSTR void __cyg_profile_func_enter(void *fn, void *cs) {
  record((uintptr_t)fn, (uintptr_t)cs, CT_EV_ENTER, tls_depth);
  tls_depth++;
}

CT_NOINSTR void __cyg_profile_func_exit(void *fn, void *cs) {
  if (tls_depth) tls_depth--;
  record((uintptr_t)fn, (uintptr_t)cs, CT_EV_EXIT, tls_depth);
}

/* ---------- 公共 API ---------- */
CT_NOINSTR const char *ctiming_version(void) { return CT_VERSION_STRING; }

CT_NOINSTR void ctiming_start(void) {
  pthread_once(&g_once, init_once);
  atomic_store(&g_enabled, 1);
}

CT_NOINSTR void ctiming_stop(void) { atomic_store(&g_enabled, 0); }

CT_NOINSTR int ctiming_dump(const char *path) {
  pthread_once(&g_once, init_once);
  char prev[CT_PATH_MAX];
  snprintf(prev, CT_PATH_MAX, "%s", g_cfg.out_path);
  if (path) snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", path);
  pthread_mutex_lock(&g_retired_mu);
  dump_locked();
  pthread_mutex_unlock(&g_retired_mu);
  snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", prev);
  return 0;
}

CT_NOINSTR void ctiming_set_filter(const char *include, const char *exclude) {
  pthread_once(&g_once, init_once);
  free(g_cfg.include); free(g_cfg.exclude);
  g_cfg.include = include ? strdup(include) : NULL;
  g_cfg.exclude = exclude ? strdup(exclude) : NULL;
}

CT_NOINSTR void ctiming_set_max_depth(unsigned depth) {
  pthread_once(&g_once, init_once);
  g_cfg.max_depth = depth;
}
```

> 说明：`gettid()` 在 glibc ≥ 2.30 可直接使用（`<unistd.h>`）。若目标 glibc 较旧，用 `syscall(SYS_gettid)`；此处按较新 glibc 编写。

- [ ] **Step 2: 写示例 `examples/example_single.c`**

```c
#include <stdio.h>

static void leaf(int x) { volatile int s = 0; for (int i = 0; i < x; i++) s += i; (void)s; }
static void mid(void) { for (int i = 0; i < 3; i++) leaf(100000); }
int main(void) { for (int i = 0; i < 2; i++) mid(); printf("done\n"); return 0; }
```

- [ ] **Step 3: 写集成测试 `tests/integration_test.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
# 参数：$1 = example 可执行文件路径, $2 = ctiming-info 路径
EXE="$1"
INFO="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
test -s "$WORK/out.ctrace"
OUT="$("$INFO" "$WORK/out.ctrace")"
echo "$OUT"
echo "$OUT" | grep -q "total_events"
echo "$OUT" | grep -q "leaf"
echo "$OUT" | grep -q "mid"
echo "$OUT" | grep -q "main"
```

- [ ] **Step 4: 更新 `CMakeLists.txt`，加入示例与集成测试**

在文件末尾追加：

```cmake
add_executable(example_single examples/example_single.c)
target_compile_options(example_single PRIVATE -finstrument-functions -g -O0)
target_link_libraries(example_single PRIVATE ctiming)

add_test(
  NAME integration_single
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_test.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-info>)
```

- [ ] **Step 5: 构建并手工验证**

Run:
```bash
cmake -S . -B build && cmake --build build -j
./build/example_single
ls -l example_single.ctrace
```
Expected: 生成 `example_single.ctrace`（非空）。此时 `ctiming-info` 仍是 stub，故先只验证文件生成。

- [ ] **Step 6: 运行全部测试**

Run: `ctest --test-dir build --output-on-failure -E integration_single`
Expected: 单元测试全 PASS（集成测试待 Task 8 完成后启用）。

- [ ] **Step 7: 提交**

```bash
git add src examples tests CMakeLists.txt
git commit -m "feat: 插桩钩子、每线程缓冲与 atexit 导出"
```

---

## Task 8: 最小读回 CLI `ctiming-info`

**Files:**
- Modify: `tools/ctiming-info.c`（完整替换）
- Modify: `CMakeLists.txt`（让 `ctiming-info` 链接完整库）

- [ ] **Step 1: 完整实现 `tools/ctiming-info.c`**

```c
#include "trace.h"
#include <stdio.h>
#include <string.h>

static void print_symbol(const ct_trace_reader *r, uint32_t fn_id) {
  if (fn_id < r->n_symbols) {
    const ct_trace_symbol *s = &r->symbols[fn_id];
    if (s->module == 0xFFFFFFFFu) printf("0x%llx", (unsigned long long)s->offset);
    else if (s->name[0]) printf("%s", s->name);
    else printf("0x%llx", (unsigned long long)s->offset);
  } else {
    printf("fn#%u", fn_id);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s <trace.ctrace>\n", argv[0]); return 2; }
  ct_trace_reader r;
  int rc = ct_trace_open(argv[1], &r);
  if (rc != 0) { fprintf(stderr, "ct_trace_open failed: %d\n", rc); return 1; }

  printf("exe: %s\n", r.header.exe);
  printf("pid: %u\n", r.header.pid);
  printf("modules: %u\n", r.n_modules);
  printf("symbols: %u\n", r.n_symbols);
  printf("threads: %u\n", r.n_threads);
  printf("total_events: %u\n", r.total_events);
  printf("dropped: %u\n", r.dropped);

  /* 每个函数出现次数（简单直方图） */
  unsigned calls[4096];
  memset(calls, 0, sizeof(calls));
  unsigned cap = r.n_symbols < 4096 ? r.n_symbols : 4096;
  for (uint32_t ti = 0; ti < r.n_threads; ti++) {
    for (uint32_t i = 0; i < r.threads[ti].n_events; i++) {
      if (r.threads[ti].events[i].kind != 0) continue; /* 只数 ENTER */
      uint32_t id = r.threads[ti].events[i].fn_id;
      if (id < cap) calls[id]++;
    }
  }
  printf("functions:\n");
  for (uint32_t i = 0; i < cap; i++) {
    if (!calls[i]) continue;
    printf("  %-40s %u\n", "", calls[i]);
    printf("    name: ");
    print_symbol(&r, i);
    printf("\n");
  }
  ct_trace_close(&r);
  return 0;
}
```

> 上面的输出刻意让 `name:` 行包含函数名，且每行前面有缩进，方便 `grep leaf/mid/main` 命中。若觉得 `%-40s` 占位别扭，可简化为 `printf("  %u ", calls[i]); print_symbol(...); printf("\n");` —— 保持“输出里包含函数名”即可。

- [ ] **Step 2: 更新 `CMakeLists.txt`**

把 `ctiming-info` 的链接改为（`trace.c` 已在库里，无需重复编译）：

```cmake
add_executable(ctiming-info tools/ctiming-info.c)
target_include_directories(ctiming-info PRIVATE include src)
target_link_libraries(ctiming-info PRIVATE ctiming)
```

- [ ] **Step 3: 构建并运行读回验证**

Run:
```bash
cmake --build build -j
./build/example_single && ./build/ctiming-info example_single.ctrace | head -30
```
Expected: 输出含 `total_events: `、`threads:`，并在 `functions:` 下列出 `leaf`、`mid`、`main`。

- [ ] **Step 4: 运行全部测试（含集成测试）**

Run: `ctest --test-dir build --output-on-failure`
Expected: `integration_single` 及全部单元测试 PASS。

- [ ] **Step 5: 提交**

```bash
git add tools CMakeLists.txt
git commit -m "feat: ctiming-info 读回 CLI 与端到端集成测试"
```

---

## Task 9: 文档与收尾

**Files:**
- Create: `README.md`
- Create: `docs/usage.md`

- [ ] **Step 1: 写 `README.md`**

内容需包含：项目一句话介绍、`ctiming` 是什么、快速开始（编译带 `-finstrument-functions` 的程序、链接 `libctiming`、运行生成 `.ctrace`、用 `ctiming-info` 查看）、已知局限（内联函数不出现、建议 `-O0/-Og`、仅 64 位 ELF）。

- [ ] **Step 2: 写 `docs/usage.md`**

内容需包含：`CTIMING_*` 环境变量表（`ENABLE/MAX_DEPTH/INCLUDE/EXCLUDE/OUT`）、过滤语义（EXCLUDE 优先）、公共 API 一览（来自 `include/ctiming.h`）、`.ctrace` 格式概述（来自规格第 6 节）、分析器/查看器属于计划 2/3 的说明。

- [ ] **Step 3: 构建、跑测、确认全绿**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全 PASS。

- [ ] **Step 4: 提交**

```bash
git add README.md docs/usage.md
git commit -m "docs: 运行时库使用说明与快速开始"
```

---

## 自审记录

- **规格覆盖：** 5.1 钩子（Task 7）、5.2 事件模型（Task 3/4）、5.3 缓冲与线程（Task 3/7）、5.4 自符号化与过滤（Task 2/5/6/7）、5.5 符号表入 trace（Task 4/7）、5.6 触发与导出（Task 7）、5.7 API（Task 1/7）、第 6 节格式（Task 4）、第 9 节边界（Task 7 的递归保护/启动前就绪判定/丢弃计数；fork 与未插桩提示在计划 2/后续补齐——**见下方缺口**）、第 10 节零依赖（Task 5 弱符号 demangle）、第 11 节测试（各 Task + 集成）、第 12 节构建（Task 1）。
- **有意留到后续计划的缺口（非本计划范围）：** `fork` 重置策略、未插桩目标的运行期警告、trace 截断标记的细化——这些属于运行时增强，可在计划 2 之前作为一个小补丁计划单列。
- **类型一致性：** `ct_event.fn`/`call_site` 为 `uintptr_t`（buffer.h）；`ct_trace_symbol.offset/module`（trace.h）在 runtime.c 的 `dump_locked` 与 symbols.c 中一致使用；`ct_filter_match` 三参签名在 Task 2 定义、Task 7 调用一致；`ct_config` 字段名在 Task 6 定义、Task 7 使用一致。

---

## 执行交接

计划已保存到 `docs/superpowers/plans/2026-09-30-ctiming-runtime.md`。两种执行方式：

1. **子代理驱动（推荐）**：每个 Task 派发一个全新子代理，Task 之间做两阶段评审，迭代快、上下文干净。
2. **当前会话内执行**：用 executing-plans 在本会话分批执行，带检查点。

选哪种？
