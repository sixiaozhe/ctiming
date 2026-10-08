# ctiming 计划 2：分析器 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 `ctiming-analyze` 分析器：读 `.ctrace`，配对 ENTER/EXIT 重建调用树与调用实例，产出聚合统计、调用关系图、聚合调用树，并输出结构化 `analysis.json` 与可读文本摘要。

**Architecture:** C++17 静态库 `ctiming_analysis`：`Trace` 加载（复用 C 版 `trace.h` 的 `ct_trace_open`）→ `CallTree`（每线程 ENTER/EXIT 栈式配对为 `Instant` 实例）→ `Analysis`（每函数统计、调用图边与递归标记、聚合合并树）→ `json`（自实现）→ CLI。计划 3 的 HTML 查看器读 `analysis.json`。

**Tech Stack:** C++17、CMake 3.16+、复用 `libctiming` 的 `trace.h`；零第三方依赖（JSON 自实现，通配符复用 `ct_glob_match`）。

**参考规格:** `docs/superpowers/specs/2026-09-30-ctiming-design.md` 第 7 节（分析器）、第 5/6 节（数据来源）。

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `src/analyze/event.hpp` / `.cpp` | `Trace` 数据结构 + `load_trace()`（包装 `ct_trace_open`，转为 C++ 结构，提供 `name_of(fn_id)`） |
| `src/analyze/calltree.hpp` / `.cpp` | `Instance`、`CallTree`、`build_call_tree()`（每线程栈式配对，算 `self_ns`） |
| `src/analyze/aggregate.hpp` / `.cpp` | `FuncStats`、`Edge`、`AggNode`、`aggregate()`（统计/边/递归/合并树） |
| `src/analyze/analysis.hpp` / `.cpp` | `Options`、`Analysis`、`analyze()`、`render_text()`、`to_json()` |
| `src/analyze/json.hpp` / `.cpp` | 极简 JSON writer（对象/数组/数字/字符串转义） |
| `tools/ctiming-analyze.cpp` | CLI：解析参数、调用 `analyze()`、输出文本或写 JSON |
| `tests/test_event.cpp` 等 | 单元测试 |
| `tests/integration_analyze.sh` | 端到端：示例程序 → `.ctrace` → `ctiming-analyze --json` → 校验 |

**约定：** 命名空间统一 `ct`。测试用现成 `tests/check.h`（C 宏，在 C++ 中可用，测试里声明 `int fails = 0;`）。**不写注释**（项目规则）。`kind == 0` 为 ENTER，`kind == 1` 为 EXIT。

**JSON 模式（`analysis.json`）：**

```json
{
  "trace": {"exe": "...", "pid": 1, "flags": 0, "modules": 3, "symbols": 2962,
            "threads": 1, "total_events": 18, "dropped": 0},
  "functions": [{"id":0,"name":"main","module":0,"offset":4096,"calls":1,
                 "total_ns":123,"self_ns":10,"min_ns":123,"max_ns":123}],
  "call_graph": [{"caller":0,"callee":1,"count":2,"total_ns":100,"recursive":false}],
  "aggregated": [{"fn":0,"calls":1,"total_ns":123,"self_ns":10,"children":[]}],
  "threads": [{"tid":1,"roots":[0]}],
  "instances": [{"id":0,"fn":0,"tid":1,"depth":0,"parent":-1,
                 "start_ns":0,"end_ns":123,"self_ns":10,"children":[1]}]
}
```

---

## Task 1: 分析器骨架与 Trace 加载

**Files:**
- Create: `src/analyze/event.hpp`
- Create: `src/analyze/event.cpp`
- Create: `tests/test_event.cpp`
- Create/Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_event.cpp`**

```cpp
#include "check.h"
#include "event.hpp"
#include <cstring>
#include <string>
#include <unistd.h>

using namespace ct;

int main() {
  int fails = 0;
  const char *path = "/tmp/ct_ev.bin";
  unlink(path);

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = 9;
  strcpy(meta.exe, "/bin/app");
  ct_trace_module mods[1];
  mods[0].base = 0x400000ULL;
  strcpy(mods[0].path, "/bin/app");
  ct_trace_symbol syms[1];
  memset(syms, 0, sizeof(syms));
  syms[0].module = 0; syms[0].offset = 0x1000ULL; strcpy(syms[0].name, "main");

  ct_buffer *b = ct_buffer_new(4);
  ct_event e = {.tid = 5, .ts = 10, .fn = 0x401000ULL, .call_site = 0, .kind = CT_EV_ENTER};
  ct_buffer_push(b, e);
  e.ts = 40; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  const ct_buffer *bufs[1] = {b};
  ct_trace_write(path, bufs, 1, mods, 1, syms, 1, &meta);

  Trace t;
  std::string err;
  CHECK(load_trace(path, t, err));
  CHECK_EQ_LONG(t.pid, 9);
  CHECK_EQ_LONG(t.total_events, 2);
  CHECK_EQ_LONG(t.threads.size(), 1);
  CHECK_EQ_LONG(t.threads[0].tid, 5);
  CHECK_EQ_LONG(t.threads[0].events.size(), 2);
  CHECK_EQ_LONG(t.threads[0].events[0].kind, 0);
  CHECK_EQ_LONG(t.threads[0].events[0].fn_id, 0);
  CHECK_EQ_LONG(t.threads[0].events[1].ts, 40);
  CHECK(t.name_of(0) == "main");
  CHECK(t.name_of(99).substr(0, 2) == "0x");

  Trace bad;
  CHECK(!load_trace("/tmp/does_not_exist_xyz.bin", bad, err));
  ct_buffer_free(b);
  unlink(path);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake -S . -B build && cmake --build build -j 2>&1 | tail`（`event.hpp` 不存在，失败）。

- [ ] **Step 3: 写 `src/analyze/event.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "trace.h"

namespace ct {

struct ModuleInfo { uint64_t base = 0; std::string path; };

struct SymbolInfo { uint32_t module = 0; uint64_t offset = 0; std::string name; };

struct TraceEvent {
  uint32_t tid = 0;
  uint8_t kind = 0;
  uint64_t ts = 0;
  uint32_t fn_id = 0;
  bool has_call_site = false;
  uint64_t call_site = 0;
};

struct ThreadEvents {
  uint32_t tid = 0;
  std::vector<TraceEvent> events;
};

struct Trace {
  std::string exe;
  uint32_t pid = 0;
  uint32_t flags = 0;
  std::vector<ModuleInfo> modules;
  std::vector<SymbolInfo> symbols;
  uint32_t total_events = 0;
  uint32_t dropped = 0;
  std::vector<ThreadEvents> threads;

  const SymbolInfo *symbol(uint32_t fn_id) const {
    return fn_id < symbols.size() ? &symbols[fn_id] : nullptr;
  }
  std::string name_of(uint32_t fn_id) const;
};

bool load_trace(const std::string &path, Trace &out, std::string &err);

} // namespace ct
```

- [ ] **Step 4: 写 `src/analyze/event.cpp`**

```cpp
#include "event.hpp"
#include <cstdio>

namespace ct {

std::string Trace::name_of(uint32_t fn_id) const {
  const SymbolInfo *s = symbol(fn_id);
  if (!s) return "fn#" + std::to_string(fn_id);
  if (!s->name.empty()) return s->name;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)s->offset);
  return buf;
}

bool load_trace(const std::string &path, Trace &out, std::string &err) {
  ct_trace_reader r;
  int rc = ct_trace_open(path.c_str(), &r);
  if (rc != 0) {
    err = "ct_trace_open failed: " + std::to_string(rc);
    return false;
  }
  out.exe = r.header.exe;
  out.pid = r.header.pid;
  out.flags = r.header.flags;
  out.total_events = r.total_events;
  out.dropped = r.dropped;
  for (uint32_t i = 0; i < r.n_modules; i++)
    out.modules.push_back(ModuleInfo{r.modules[i].base, r.modules[i].path});
  for (uint32_t i = 0; i < r.n_symbols; i++)
    out.symbols.push_back(SymbolInfo{r.symbols[i].module, r.symbols[i].offset, r.symbols[i].name});
  for (uint32_t ti = 0; ti < r.n_threads; ti++) {
    ThreadEvents te;
    te.tid = r.threads[ti].tid;
    for (uint32_t i = 0; i < r.threads[ti].n_events; i++) {
      const ct_trace_event &e = r.threads[ti].events[i];
      te.events.push_back(TraceEvent{e.fn_id, e.kind, e.ts, e.fn_id, e.has_call_site != 0, e.call_site});
    }
    out.threads.push_back(std::move(te));
  }
  ct_trace_close(&r);
  return true;
}

} // namespace ct
```

> Note: `TraceEvent` field order is `tid/kind/ts/fn_id/has_call_site/call_site`; the aggregate init above passes `{fn_id, kind, ts, fn_id, ...}` positionally — that is WRONG because the first field is `tid`, not `fn_id`. Set fields by name instead:
> ```cpp
> TraceEvent tev;
> tev.tid = te.tid;
> tev.kind = e.kind;
> tev.ts = e.ts;
> tev.fn_id = e.fn_id;
> tev.has_call_site = e.has_call_site != 0;
> tev.call_site = e.call_site;
> te.events.push_back(tev);
> ```
> Implement it with named assignment (the plan intentionally shows the corrected form).

- [ ] **Step 5: 更新 `CMakeLists.txt`**

在 `ct_add_test` 定义之后新增分析器库与测试辅助：

```cmake
add_library(ctiming_analysis STATIC
  src/analyze/event.cpp)
target_include_directories(ctiming_analysis PUBLIC src/analyze PRIVATE src)
target_link_libraries(ctiming_analysis PUBLIC ctiming)
add_compile_options(-Wall -Wextra)

function(ct_add_analysis_test name file)
  add_executable(${name} ${file})
  target_include_directories(${name} PRIVATE src src/analyze tests)
  target_link_libraries(${name} PRIVATE ctiming_analysis)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ct_add_analysis_test(test_event tests/test_event.cpp)
```

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 原 8 个测试 + `test_event` 全 PASS，无警告。

- [ ] **Step 7: 提交**

```bash
git add src/analyze tests/test_event.cpp CMakeLists.txt
git commit -m "feat(analyze): Trace 加载与数据结构"
```

---

## Task 2: 调用树配对

**Files:**
- Create: `src/analyze/calltree.hpp`
- Create: `src/analyze/calltree.cpp`
- Create: `tests/test_calltree.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_calltree.cpp`**

```cpp
#include "check.h"
#include "event.hpp"
#include "calltree.hpp"

using namespace ct;

static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid;
  for (auto e : evs) t.events.push_back(e);
  return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  // main(0) -> a(1) -> b(2); a twice
  // main E0 t0 / a E1 t10 / b E2 t20 / b X2 t50 / a X1 t60 / a E1 t70 / a X1 t90 / main X0 t100
  std::vector<ThreadEvents> threads;
  threads.push_back(mk(7, {
    ev(0,0,0), ev(0,1,10), ev(0,2,20), ev(1,2,50), ev(1,1,60),
    ev(0,1,70), ev(1,1,90), ev(1,0,100)
  }));

  CallTree tree = build_call_tree(threads);
  CHECK_EQ_LONG(tree.instances.size(), 5);
  // instance 0 = main
  CHECK_EQ_LONG(tree.instances[0].fn_id, 0);
  CHECK_EQ_LONG(tree.instances[0].depth, 0);
  CHECK_EQ_LONG(tree.instances[0].parent, -1);
  CHECK_EQ_LONG(tree.instances[0].start_ns, 0);
  CHECK_EQ_LONG(tree.instances[0].end_ns, 100);
  CHECK_EQ_LONG(tree.instances[0].self_ns, 100 - (50 + 20)); // 30
  CHECK_EQ_LONG(tree.instances[0].children.size(), 2); // two 'a'
  // instance 1 = a (first)
  CHECK_EQ_LONG(tree.instances[1].fn_id, 1);
  CHECK_EQ_LONG(tree.instances[1].depth, 1);
  CHECK_EQ_LONG(tree.instances[1].parent, 0);
  CHECK_EQ_LONG(tree.instances[1].end_ns, 60);
  CHECK_EQ_LONG(tree.instances[1].self_ns, 50 - 30); // duration 50, child b 30
  CHECK_EQ_LONG(tree.instances[2].fn_id, 2);
  CHECK_EQ_LONG(tree.instances[2].depth, 2);
  CHECK_EQ_LONG(tree.instances[2].self_ns, 30);
  CHECK_EQ_LONG(tree.roots.size(), 1);
  CHECK_EQ_LONG(tree.roots[0], 0);
  CHECK_EQ_LONG(tree.unbalanced_enter, 0);
  CHECK_EQ_LONG(tree.orphan_exit, 0);

  // orphan exit is tolerated
  std::vector<ThreadEvents> t2;
  t2.push_back(mk(9, { ev(1,3,5) }));
  CallTree tree2 = build_call_tree(t2);
  CHECK_EQ_LONG(tree2.instances.size(), 0);
  CHECK_EQ_LONG(tree2.orphan_exit, 1);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j 2>&1 | tail`（`calltree.hpp` 不存在）。

- [ ] **Step 3: 写 `src/analyze/calltree.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "event.hpp"

namespace ct {

struct Instance {
  uint32_t id = 0;
  uint32_t fn_id = 0;
  uint32_t tid = 0;
  uint32_t depth = 0;
  int parent = -1;
  std::vector<uint32_t> children;
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  uint64_t self_ns = 0;
};

struct CallTree {
  std::vector<Instance> instances;
  std::vector<uint32_t> roots;
  uint32_t unbalanced_enter = 0;
  uint32_t orphan_exit = 0;
};

CallTree build_call_tree(const std::vector<ThreadEvents> &threads);

} // namespace ct
```

- [ ] **Step 4: 写 `src/analyze/calltree.cpp`**

```cpp
#include "calltree.hpp"

namespace ct {

CallTree build_call_tree(const std::vector<ThreadEvents> &threads) {
  CallTree tree;
  for (const ThreadEvents &te : threads) {
    std::vector<uint32_t> stack;
    uint32_t last_ts = 0;
    for (const TraceEvent &e : te.events) {
      last_ts = e.ts;
      if (e.kind == 0) {
        Instance in;
        in.id = (uint32_t)tree.instances.size();
        in.fn_id = e.fn_id;
        in.tid = te.tid;
        in.depth = (uint32_t)stack.size();
        in.parent = stack.empty() ? -1 : (int)stack.back();
        in.start_ns = e.ts;
        tree.instances.push_back(in);
        uint32_t id = in.id;
        if (stack.empty()) tree.roots.push_back(id);
        else tree.instances[stack.back()].children.push_back(id);
        stack.push_back(id);
      } else {
        if (stack.empty()) { tree.orphan_exit++; continue; }
        uint32_t id = stack.back();
        stack.pop_back();
        Instance &in = tree.instances[id];
        in.end_ns = e.ts >= in.start_ns ? e.ts : in.start_ns;
        uint64_t total = in.end_ns - in.start_ns;
        uint64_t child_sum = 0;
        for (uint32_t c : in.children) {
          const Instance &ci = tree.instances[c];
          child_sum += ci.end_ns - ci.start_ns;
        }
        in.self_ns = total > child_sum ? total - child_sum : 0;
      }
    }
    tree.unbalanced_enter += (uint32_t)stack.size();
    for (uint32_t id : stack) {
      Instance &in = tree.instances[id];
      in.end_ns = in.start_ns;
      in.self_ns = 0;
    }
    (void)last_ts;
  }
  return tree;
}

} // namespace ct
```

- [ ] **Step 5: 更新 `CMakeLists.txt`**

库源加 `src/analyze/calltree.cpp`；加 `ct_add_analysis_test(test_calltree tests/test_calltree.cpp)`。

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全 PASS（重点 `test_calltree`）。

- [ ] **Step 7: 提交**

```bash
git add src/analyze tests/test_calltree.cpp CMakeLists.txt
git commit -m "feat(analyze): ENTER/EXIT 配对与调用实例"
```

---

## Task 3: 每函数聚合统计

**Files:**
- Create: `src/analyze/aggregate.hpp`
- Create: `src/analyze/aggregate.cpp`
- Create: `tests/test_aggregate.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_aggregate.cpp`**

```cpp
#include "check.h"
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"

using namespace ct;
static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid; for (auto e : evs) t.events.push_back(e); return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  std::vector<ThreadEvents> threads;
  threads.push_back(mk(1, {
    ev(0,0,0), ev(0,1,10), ev(1,1,30), ev(0,1,40), ev(1,1,50), ev(1,0,60)
  }));
  CallTree tree = build_call_tree(threads);
  Analysis a = aggregate(tree, /*n_symbols=*/3);
  CHECK_EQ_LONG(a.funcs.size(), 3);
  CHECK_EQ_LONG(a.funcs[0].calls, 1);
  CHECK_EQ_LONG(a.funcs[0].total_ns, 60);
  CHECK_EQ_LONG(a.funcs[0].self_ns, 60 - 30); // two a's total 30
  CHECK_EQ_LONG(a.funcs[0].min_ns, 60);
  CHECK_EQ_LONG(a.funcs[0].max_ns, 60);
  CHECK_EQ_LONG(a.funcs[1].calls, 2);
  CHECK_EQ_LONG(a.funcs[1].total_ns, 20 + 10); // 30
  CHECK_EQ_LONG(a.funcs[1].self_ns, 30);
  CHECK_EQ_LONG(a.funcs[1].min_ns, 10);
  CHECK_EQ_LONG(a.funcs[1].max_ns, 20);
  CHECK_EQ_LONG(a.funcs[2].calls, 0);

  // edge main->a count 2, recursive false
  const Edge *e = a.find_edge(0, 1);
  CHECK(e != nullptr);
  if (e) { CHECK_EQ_LONG(e->count, 2); CHECK_EQ_LONG(e->total_ns, 30); CHECK(!e->recursive); }

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j 2>&1 | tail`。

- [ ] **Step 3: 写 `src/analyze/aggregate.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "calltree.hpp"

namespace ct {

struct FuncStats {
  uint32_t fn_id = 0;
  uint64_t calls = 0;
  uint64_t total_ns = 0;
  uint64_t self_ns = 0;
  uint64_t min_ns = 0;
  uint64_t max_ns = 0;
};

struct Edge {
  uint32_t caller = 0;
  uint32_t callee = 0;
  uint64_t count = 0;
  uint64_t total_ns = 0;
  bool recursive = false;
};

struct AggNode {
  uint32_t fn_id = 0;
  uint64_t calls = 0;
  uint64_t total_ns = 0;
  uint64_t self_ns = 0;
  std::vector<AggNode> children;
};

struct Analysis {
  std::vector<FuncStats> funcs;
  std::vector<Edge> edges;
  std::vector<AggNode> aggregated;
  const Edge *find_edge(uint32_t caller, uint32_t callee) const;
};

Analysis aggregate(const CallTree &tree, uint32_t n_symbols);

} // namespace ct
```

- [ ] **Step 4: 写 `src/analyze/aggregate.cpp`**

```cpp
#include "aggregate.hpp"
#include <map>

namespace ct {

const Edge *Analysis::find_edge(uint32_t caller, uint32_t callee) const {
  for (const Edge &e : edges)
    if (e.caller == caller && e.callee == callee) return &e;
  return nullptr;
}

static void mark_recursive(std::vector<Edge> &edges) {
  // 建邻接表（caller -> list of edge index）
  std::map<uint32_t, std::vector<size_t>> adj;
  for (size_t i = 0; i < edges.size(); i++) adj[edges[i].caller].push_back(i);
  for (size_t i = 0; i < edges.size(); i++) {
    if (edges[i].recursive) continue;
    if (edges[i].caller == edges[i].callee) { edges[i].recursive = true; continue; }
    // 从 callee 出发能否回到 caller
    std::vector<uint32_t> stack{edges[i].callee};
    std::map<uint32_t, bool> seen;
    bool found = false;
    while (!stack.empty() && !found) {
      uint32_t cur = stack.back(); stack.pop_back();
      if (cur == edges[i].caller) { found = true; break; }
      if (seen[cur]) continue;
      seen[cur] = true;
      for (size_t ei : adj[cur]) stack.push_back(edges[ei].callee);
    }
    if (found) edges[i].recursive = true;
  }
}

Analysis aggregate(const CallTree &tree, uint32_t n_symbols) {
  Analysis a;
  a.funcs.resize(n_symbols);
  for (uint32_t i = 0; i < n_symbols; i++) a.funcs[i].fn_id = i;

  std::map<std::pair<uint32_t, uint32_t>, Edge> emap;
  for (const Instance &in : tree.instances) {
    if (in.fn_id < n_symbols) {
      FuncStats &f = a.funcs[in.fn_id];
      uint64_t dur = in.end_ns - in.start_ns;
      f.calls++;
      f.total_ns += dur;
      f.self_ns += in.self_ns;
      if (f.calls == 1 || dur < f.min_ns) f.min_ns = dur;
      if (dur > f.max_ns) f.max_ns = dur;
    }
    if (in.parent >= 0) {
      const Instance &p = tree.instances[in.parent];
      auto key = std::make_pair(p.fn_id, in.fn_id);
      Edge &e = emap[key];
      e.caller = p.fn_id;
      e.callee = in.fn_id;
      e.count++;
      e.total_ns += in.end_ns - in.start_ns;
    }
  }
  for (auto &kv : emap) a.edges.push_back(kv.second);
  mark_recursive(a.edges);

  // 聚合合并树：按 fn_id 逐层合并 roots
  std::function<AggNode(const Instance &)> build = [&](const Instance &in) {
    AggNode node;
    node.fn_id = in.fn_id;
    node.calls = 1;
    node.total_ns = in.end_ns - in.start_ns;
    node.self_ns = in.self_ns;
    std::map<uint32_t, size_t> idx;
    for (uint32_t cid : in.children) {
      AggNode child = build(tree.instances[cid]);
      auto it = idx.find(child.fn_id);
      if (it == idx.end()) { idx[child.fn_id] = node.children.size(); node.children.push_back(std::move(child)); }
      else {
        AggNode &dst = node.children[it->second];
        dst.calls += child.calls;
        dst.total_ns += child.total_ns;
        dst.self_ns += child.self_ns;
        for (auto &gc : child.children) merge_child(dst, gc);
      }
    }
    return node;
  };
  for (uint32_t root : tree.roots) {
    AggNode node = build(tree.instances[root]);
    bool merged = false;
    for (AggNode &r : a.aggregated) {
      if (r.fn_id == node.fn_id) {
        r.calls += node.calls; r.total_ns += node.total_ns; r.self_ns += node.self_ns;
        for (auto &c : node.children) merge_child(r, c);
        merged = true; break;
      }
    }
    if (!merged) a.aggregated.push_back(std::move(node));
  }
  return a;
}

} // namespace ct
```

> The reference above references helpers `merge_child` and `<functional>`; add a static `void merge_child(AggNode&, const AggNode&)` that merges a child subtree into a parent (by fn_id, recursively), and `#include <functional>`. `merge_child` is:
> ```cpp
> static void merge_child(AggNode &parent, const AggNode &child) {
>   for (AggNode &c : parent.children) {
>     if (c.fn_id == child.fn_id) {
>       c.calls += child.calls; c.total_ns += child.total_ns; c.self_ns += child.self_ns;
>       for (const AggNode &gc : child.children) merge_child(c, gc);
>       return;
>     }
>   }
>   parent.children.push_back(child);
> }
> ```
> Define `merge_child` **before** `aggregate`. (Note: `build`'s local `idx` map plus `merge_child` is redundant; you may rely solely on `merge_child` for the per-node child merge. Keep behavior: children with the same fn_id are merged recursively.)

- [ ] **Step 5: 更新 `CMakeLists.txt`**

库源加 `src/analyze/aggregate.cpp`；加 `ct_add_analysis_test(test_aggregate tests/test_aggregate.cpp)`。

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全 PASS（重点 `test_aggregate`）。

- [ ] **Step 7: 提交**

```bash
git add src/analyze tests/test_aggregate.cpp CMakeLists.txt
git commit -m "feat(analyze): 每函数统计、调用图边与聚合树"
```

---

## Task 4: 递归/环检测

**Files:**
- Modify: `tests/test_aggregate.cpp`（追加用例）
- Modify: `src/analyze/aggregate.cpp`（若需要）

- [ ] **Step 1: 追加失败测试（递归检测）到 `tests/test_aggregate.cpp`**

在 `return fails ? 1 : 0;` 之前插入：

```cpp
  // recursion: f(0) -> f(0)
  {
    std::vector<ThreadEvents> t3;
    t3.push_back(mk(2, { ev(0,0,0), ev(0,0,5), ev(1,0,10), ev(1,0,20) }));
    CallTree tr = build_call_tree(t3);
    Analysis a3 = aggregate(tr, 1);
    const Edge *se = a3.find_edge(0, 0);
    CHECK(se != nullptr);
    if (se) CHECK(se->recursive);
  }
  // mutual recursion: a(0) -> b(1) -> a(0)
  {
    std::vector<ThreadEvents> t4;
    t4.push_back(mk(3, { ev(0,0,0), ev(0,1,5), ev(0,0,10), ev(1,0,15), ev(1,1,20), ev(1,0,25) }));
    CallTree tr = build_call_tree(t4);
    Analysis a4 = aggregate(tr, 2);
    const Edge *e01 = a4.find_edge(0, 1);
    const Edge *e10 = a4.find_edge(1, 0);
    CHECK(e01 != nullptr && e01->recursive);
    CHECK(e10 != nullptr && e10->recursive);
  }
```

- [ ] **Step 2: 运行，确认失败或通过**

Run: `cmake --build build -j && ctest --test-dir build -R test_aggregate --output-on-failure`
Expected: 若 `mark_recursive` 已正确，直接 PASS；若失败，修 `mark_recursive`（确保自环与互递归都标记）。

- [ ] **Step 3: 修复实现（如需要）**

确认 `mark_recursive` 覆盖：`caller==callee` 自环，以及 callee 可达 caller 的环。无需额外代码则跳过。

- [ ] **Step 4: 构建并运行全部测试**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure` → 全 PASS。

- [ ] **Step 5: 提交**

```bash
git add src/analyze tests/test_aggregate.cpp
git commit -m "test(analyze): 递归与互递归调用图标记"
```

---

## Task 5: JSON writer

**Files:**
- Create: `src/analyze/json.hpp`
- Create: `src/analyze/json.cpp`
- Create: `tests/test_json.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_json.cpp`**

```cpp
#include "check.h"
#include "json.hpp"
#include <string>

using namespace ct;

int main() {
  int fails = 0;
  CHECK(json_escape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
  CHECK(json_escape("\x01") == "\\u0001");
  CHECK(json_escape("中文") == "中文"); // UTF-8 passthrough

  JsonWriter w;
  w.begin_object();
  w.key("n"); w.number(42);
  w.key("s"); w.str("hi");
  w.key("b"); w.boolean(true);
  w.key("arr"); w.begin_array(); w.number(1); w.number(2); w.end_array();
  w.end_object();
  std::string out = w.str_out();
  CHECK(out == "{\"n\":42,\"s\":\"hi\",\"b\":true,\"arr\":[1,2]}");

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j 2>&1 | tail`。

- [ ] **Step 3: 写 `src/analyze/json.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ct {

std::string json_escape(const std::string &s);

class JsonWriter {
public:
  void begin_object();
  void end_object();
  void begin_array();
  void end_array();
  void key(const std::string &k);
  void number(int64_t v);
  void number(uint64_t v);
  void number(double v);
  void boolean(bool b);
  void str(const std::string &s);
  void null_value();
  const std::string &str_out() const { return out_; }

private:
  std::vector<bool> first_;
  std::string out_;
  void comma();
};

} // namespace ct
```

- [ ] **Step 4: 写 `src/analyze/json.cpp`**

```cpp
#include "json.hpp"
#include <cstdio>

namespace ct {

std::string json_escape(const std::string &s) {
  std::string r;
  char buf[8];
  for (unsigned char c : s) {
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (c < 0x20) { std::snprintf(buf, sizeof(buf), "\\u%04x", c); r += buf; }
        else r += (char)c;
    }
  }
  return r;
}

void JsonWriter::comma() {
  if (first_.empty()) return;
  if (first_.back()) first_.back() = false;
  else out_ += ",";
}

void JsonWriter::begin_object() { comma(); out_ += "{"; first_.push_back(true); }
void JsonWriter::end_object() { out_ += "}"; if (!first_.empty()) first_.pop_back(); }
void JsonWriter::begin_array() { comma(); out_ += "["; first_.push_back(true); }
void JsonWriter::end_array() { out_ += "]"; if (!first_.empty()) first_.pop_back(); }
void JsonWriter::key(const std::string &k) { comma(); out_ += "\"" + json_escape(k) + "\":"; first_.back() = true; }

void JsonWriter::number(int64_t v) { comma(); out_ += std::to_string(v); }
void JsonWriter::number(uint64_t v) { comma(); out_ += std::to_string(v); }
void JsonWriter::number(double v) { comma(); char b[64]; std::snprintf(b, sizeof(b), "%g", v); out_ += b; }
void JsonWriter::boolean(bool b) { comma(); out_ += b ? "true" : "false"; }
void JsonWriter::str(const std::string &s) { comma(); out_ += "\"" + json_escape(s) + "\""; }
void JsonWriter::null_value() { comma(); out_ += "null"; }

} // namespace ct
```

> Note: `key()` sets `first_.back()=true` so that the value after a key does not emit a leading comma. But the value methods call `comma()` which will then see `first_.back()==true` and just clear it without emitting a comma — correct. However after a value, the next `key()` calls `comma()` which emits a comma (since first_ is false) — correct. Verify the produced string matches the test.

- [ ] **Step 5: 更新 `CMakeLists.txt`**

库源加 `src/analyze/json.cpp`；加 `ct_add_analysis_test(test_json tests/test_json.cpp)`。

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` → 全 PASS。

- [ ] **Step 7: 提交**

```bash
git add src/analyze tests/test_json.cpp CMakeLists.txt
git commit -m "feat(analyze): 极简 JSON writer"
```

---

## Task 6: 顶层 `analyze()` 与 `to_json()`

**Files:**
- Create: `src/analyze/analysis.hpp`
- Create: `src/analyze/analysis.cpp`
- Create: `tests/test_analysis.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写失败测试 `tests/test_analysis.cpp`**

```cpp
#include "check.h"
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"
#include "analysis.hpp"

using namespace ct;
static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid; for (auto e : evs) t.events.push_back(e); return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  Trace tr;
  tr.pid = 1;
  tr.total_events = 6;
  tr.symbols.push_back(SymbolInfo{0, 0x1000, "main"});
  tr.symbols.push_back(SymbolInfo{0, 0x2000, "leaf"});
  tr.threads.push_back(mk(1, { ev(0,0,0), ev(0,1,10), ev(1,1,30), ev(0,1,40), ev(1,1,50), ev(1,0,60) }));

  Options opt;
  AnalysisResult r = analyze(tr, opt);
  CHECK_EQ_LONG(r.tree.instances.size(), 3);
  CHECK_EQ_LONG(r.agg.funcs[1].calls, 2);
  std::string js = to_json(r, tr);
  CHECK(js.find("\"main\"") != std::string::npos);
  CHECK(js.find("\"leaf\"") != std::string::npos);
  CHECK(js.find("\"call_graph\"") != std::string::npos);
  CHECK(js.find("\"instances\"") != std::string::npos);
  std::string txt = render_text(r, tr);
  CHECK(txt.find("leaf") != std::string::npos);
  CHECK(txt.find("total") != std::string::npos);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试，确认失败**

Run: `cmake --build build -j 2>&1 | tail`。

- [ ] **Step 3: 写 `src/analyze/analysis.hpp`**

```cpp
#pragma once
#include <string>
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"

namespace ct {

struct Options {
  std::string include;
  std::string exclude;
  uint64_t min_total_ns = 0;
  int top = 0;
  bool has_include = false;
  bool has_exclude = false;
};

struct AnalysisResult {
  CallTree tree;
  Analysis agg;
};

AnalysisResult analyze(const Trace &trace, const Options &opt);
std::string to_json(const AnalysisResult &r, const Trace &trace);
std::string render_text(const AnalysisResult &r, const Trace &trace);

} // namespace ct
```

- [ ] **Step 4: 写 `src/analyze/analysis.cpp`**

```cpp
#include "analysis.hpp"
#include "json.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include "glob.h"

namespace ct {

AnalysisResult analyze(const Trace &trace, const Options &opt) {
  AnalysisResult r;
  r.tree = build_call_tree(trace.threads);
  r.agg = aggregate(r.tree, (uint32_t)trace.symbols.size());

  if (opt.has_include || opt.has_exclude) {
    std::vector<FuncStats> kept;
    kept.reserve(r.agg.funcs.size());
    for (const FuncStats &f : r.agg.funcs) {
      bool pass = ct_filter_match(opt.has_include ? opt.include.c_str() : nullptr,
                                  opt.has_exclude ? opt.exclude.c_str() : nullptr,
                                  trace.name_of(f.fn_id).c_str()) != 0;
      kept.push_back(f);
      if (!pass) kept.back().calls = 0;
    }
    r.agg.funcs = std::move(kept);
  }
  if (opt.min_total_ns > 0) {
    for (FuncStats &f : r.agg.funcs)
      if (f.total_ns < opt.min_total_ns) f.calls = 0;
  }
  return r;
}

static void write_funcs(JsonWriter &w, const AnalysisResult &r, const Trace &trace) {
  w.key("functions"); w.begin_array();
  for (const FuncStats &f : r.agg.funcs) {
    w.begin_object();
    w.key("id"); w.number((uint64_t)f.fn_id);
    w.key("name"); w.str(trace.name_of(f.fn_id));
    const SymbolInfo *s = trace.symbol(f.fn_id);
    w.key("module"); w.number((int64_t)(s ? s->module : 0xFFFFFFFFu));
    w.key("offset"); w.number((uint64_t)(s ? s->offset : 0));
    w.key("calls"); w.number(f.calls);
    w.key("total_ns"); w.number(f.total_ns);
    w.key("self_ns"); w.number(f.self_ns);
    w.key("min_ns"); w.number(f.min_ns);
    w.key("max_ns"); w.number(f.max_ns);
    w.end_object();
  }
  w.end_array();
}

static void write_agg(JsonWriter &w, const AggNode &n, const Trace &trace) {
  w.begin_object();
  w.key("fn"); w.number((uint64_t)n.fn_id);
  w.key("name"); w.str(trace.name_of(n.fn_id));
  w.key("calls"); w.number(n.calls);
  w.key("total_ns"); w.number(n.total_ns);
  w.key("self_ns"); w.number(n.self_ns);
  w.key("children"); w.begin_array();
  for (const AggNode &c : n.children) write_agg(w, c, trace);
  w.end_array();
  w.end_object();
}

std::string to_json(const AnalysisResult &r, const Trace &trace) {
  JsonWriter w;
  w.begin_object();
  w.key("trace"); w.begin_object();
  w.key("exe"); w.str(trace.exe);
  w.key("pid"); w.number((uint64_t)trace.pid);
  w.key("flags"); w.number((uint64_t)trace.flags);
  w.key("modules"); w.number((uint64_t)trace.modules.size());
  w.key("symbols"); w.number((uint64_t)trace.symbols.size());
  w.key("threads"); w.number((uint64_t)trace.threads.size());
  w.key("total_events"); w.number((uint64_t)trace.total_events);
  w.key("dropped"); w.number((uint64_t)trace.dropped);
  w.end_object();

  write_funcs(w, r, trace);

  w.key("call_graph"); w.begin_array();
  for (const Edge &e : r.agg.edges) {
    w.begin_object();
    w.key("caller"); w.number((uint64_t)e.caller);
    w.key("callee"); w.number((uint64_t)e.callee);
    w.key("count"); w.number(e.count);
    w.key("total_ns"); w.number(e.total_ns);
    w.key("recursive"); w.boolean(e.recursive);
    w.end_object();
  }
  w.end_array();

  w.key("aggregated"); w.begin_array();
  for (const AggNode &n : r.agg.aggregated) write_agg(w, n, trace);
  w.end_array();

  w.key("threads"); w.begin_array();
  w.begin_object(); w.key("tid"); w.number((uint64_t)0); w.key("roots"); w.begin_array();
  for (uint32_t root : r.tree.roots) w.number((uint64_t)root);
  w.end_array(); w.end_object();
  w.end_array();

  w.key("instances"); w.begin_array();
  for (const Instance &in : r.tree.instances) {
    w.begin_object();
    w.key("id"); w.number((uint64_t)in.id);
    w.key("fn"); w.number((uint64_t)in.fn_id);
    w.key("name"); w.str(trace.name_of(in.fn_id));
    w.key("tid"); w.number((uint64_t)in.tid);
    w.key("depth"); w.number((uint64_t)in.depth);
    w.key("parent"); w.number((int64_t)in.parent);
    w.key("start_ns"); w.number(in.start_ns);
    w.key("end_ns"); w.number(in.end_ns);
    w.key("self_ns"); w.number(in.self_ns);
    w.key("children"); w.begin_array();
    for (uint32_t c : in.children) w.number((uint64_t)c);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  w.end_object();
  return w.str_out();
}

std::string render_text(const AnalysisResult &r, const Trace &trace) {
  std::ostringstream os;
  os << "exe: " << trace.exe << "\n";
  os << "threads: " << trace.threads.size() << "  total_events: " << trace.total_events
     << "  dropped: " << trace.dropped << "\n";
  os << "total functions with calls: ";
  uint64_t n = 0;
  for (const FuncStats &f : r.agg.funcs) if (f.calls) n++;
  os << n << "\n";
  os << "function                         calls     total_ns      self_ns\n";
  for (const FuncStats &f : r.agg.funcs) {
    if (!f.calls) continue;
    char line[256];
    std::snprintf(line, sizeof(line), "%-32.32s %6llu %13llu %13llu\n",
                  trace.name_of(f.fn_id).c_str(),
                  (unsigned long long)f.calls, (unsigned long long)f.total_ns,
                  (unsigned long long)f.self_ns);
    os << line;
  }
  return os.str();
}

} // namespace ct
```

> Note: the `"threads"` array in `to_json` above writes a single synthetic object; improve it to emit one entry per `trace.threads` (tid + root instances that belong to that tid). For the test, a non-empty `"threads"` array suffices; implement per-thread entries properly.

- [ ] **Step 5: 更新 `CMakeLists.txt`**

库源加 `src/analyze/analysis.cpp`；加 `ct_add_analysis_test(test_analysis tests/test_analysis.cpp)`。

- [ ] **Step 6: 构建并运行测试，确认通过**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` → 全 PASS（重点 `test_analysis`）。

- [ ] **Step 7: 提交**

```bash
git add src/analyze tests/test_analysis.cpp CMakeLists.txt
git commit -m "feat(analyze): analyze()/to_json()/文本摘要"
```

---

## Task 7: CLI `ctiming-analyze`

**Files:**
- Create: `tools/ctiming-analyze.cpp`
- Create: `tests/integration_analyze.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写 CLI `tools/ctiming-analyze.cpp`**

要求：
- 用法：`ctiming-analyze <trace.ctrace> [--json FILE] [--include GLOB] [--exclude GLOB] [--min-total NS] [--top N]`
- 无参数 / 未知参数 → 打印用法到 stderr，返回 2。
- 读 trace 失败 → stderr 报错，返回 1。
- 默认把 `render_text()` 打到 stdout；若给了 `--json FILE` 则把 `to_json()` 写入该文件（并在 stdout 打印一行提示）。
- 返回 0 成功。

参考骨架：

```cpp
#include "event.hpp"
#include "analysis.hpp"
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char **argv) {
  std::string trace_path, json_path, include, exclude;
  bool has_include = false, has_exclude = false;
  uint64_t min_total = 0;
  int top = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--json" && i + 1 < argc) { json_path = argv[++i]; }
    else if (a == "--include" && i + 1 < argc) { include = argv[++i]; has_include = true; }
    else if (a == "--exclude" && i + 1 < argc) { exclude = argv[++i]; has_exclude = true; }
    else if (a == "--min-total" && i + 1 < argc) { min_total = strtoull(argv[++i], nullptr, 10); }
    else if (a == "--top" && i + 1 < argc) { top = atoi(argv[++i]); }
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
    else if (trace_path.empty()) { trace_path = a; }
    else { std::fprintf(stderr, "too many arguments\n"); return 2; }
  }
  if (trace_path.empty()) {
    std::fprintf(stderr, "usage: %s <trace.ctrace> [--json FILE] [--include GLOB] [--exclude GLOB] [--min-total NS] [--top N]\n", argv[0]);
    return 2;
  }
  ct::Trace trace;
  std::string err;
  if (!ct::load_trace(trace_path, trace, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  ct::Options opt;
  opt.include = include; opt.exclude = exclude;
  opt.has_include = has_include; opt.has_exclude = has_exclude;
  opt.min_total_ns = min_total; opt.top = top;
  ct::AnalysisResult r = ct::analyze(trace, opt);
  if (!json_path.empty()) {
    std::string js = ct::to_json(r, trace);
    FILE *f = std::fopen(json_path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", json_path.c_str()); return 1; }
    std::fwrite(js.data(), 1, js.size(), f);
    std::fclose(f);
    std::printf("wrote %s\n", json_path.c_str());
  } else {
    std::fputs(ct::render_text(r, trace).c_str(), stdout);
  }
  return 0;
}
```

Add `#include <cstdlib>` for `strtoull`/`atoi`.

- [ ] **Step 2: 写集成测试 `tests/integration_analyze.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
INFO="$2"
ANALYZE="$3"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/analysis.json" >/dev/null
test -s "$WORK/analysis.json"
grep -q '"main"' "$WORK/analysis.json"
grep -q '"leaf"' "$WORK/analysis.json"
grep -q '"call_graph"' "$WORK/analysis.json"
TXT="$("$ANALYZE" "$WORK/out.ctrace")"
echo "$TXT"
echo "$TXT" | grep -q "leaf"
echo "$TXT" | grep -q "total_ns"
```

Make it executable.

- [ ] **Step 3: 更新 `CMakeLists.txt`**

```cmake
add_executable(ctiming-analyze tools/ctiming-analyze.cpp)
target_include_directories(ctiming-analyze PRIVATE src src/analyze)
target_link_libraries(ctiming-analyze PRIVATE ctiming_analysis)
target_compile_options(ctiming-analyze PRIVATE -Wall -Wextra)

add_test(
  NAME integration_analyze
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_analyze.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-info> $<TARGET_FILE:ctiming-analyze>)
```

- [ ] **Step 4: 构建并运行全部测试**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 全 PASS（含 `integration_analyze`）。

- [ ] **Step 5: 手工查看文本摘要**

Run: `./build/example_single && ./build/ctiming-analyze example_single.ctrace`
Expected: 表中有 `leaf`/`mid`/`main` 及 calls/total_ns/self_ns。

- [ ] **Step 6: 提交**

```bash
git add tools/ctiming-analyze.cpp tests/integration_analyze.sh CMakeLists.txt
git commit -m "feat(analyze): ctiming-analyze CLI 与端到端集成"
```

---

## Task 8: 过滤参数集成与文档

**Files:**
- Modify: `tests/integration_analyze.sh`（追加过滤断言）
- Create: `docs/analyzer.md`
- Modify: `README.md`（补一句分析器用法）

- [ ] **Step 1: 追加过滤断言到 `tests/integration_analyze.sh`**

在末尾追加：

```bash
FILTERED="$("$ANALYZE" "$WORK/out.ctrace" --include 'leaf*')"
echo "$FILTERED" | grep -q "leaf"
if echo "$FILTERED" | grep -q "mid"; then echo "include filter failed"; exit 1; fi
```

- [ ] **Step 2: 运行，确认通过**

Run: `cmake --build build -j && ctest --test-dir build -R integration_analyze --output-on-failure`
Expected: PASS（`--include 'leaf*'` 后文本摘要只含 leaf）。

- [ ] **Step 3: 写 `docs/analyzer.md`（简体中文）**

内容：`ctiming-analyze` 用法与全部参数（`--json/--include/--exclude/--min-total/--top`）、`analysis.json` 字段说明（trace/functions/call_graph/aggregated/threads/instances）、`self_ns` 与 `total_ns` 语义、递归标记含义、与计划 3 HTML 查看器的关系。

- [ ] **Step 4: 在 `README.md` 补一小节**

加“分析器（计划 2）”小节：`./build/ctiming-analyze app.ctrace --json analysis.json`，并注明 HTML 查看器属计划 3。

- [ ] **Step 5: 全量测试**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` → 全 PASS。

- [ ] **Step 6: 提交**

```bash
git add tests/integration_analyze.sh docs/analyzer.md README.md
git commit -m "docs(analyze): 分析器用法与 JSON 模式说明"
```

---

## 自审记录

- **规格覆盖：** §7 分析器流程（读 trace Task 1、配对 Task 2、聚合 Task 3、调用图与递归 Task 3/4、调用实例索引 Task 2/6、JSON Task 5/6、CLI Task 7、过滤 Task 8）。
- **占位符扫描：** 无 TBD/TODO；每步含实际代码与命令。
- **类型一致性：** `TraceEvent{ tid,kind,ts,fn_id,has_call_site,call_site }` 在 Task 1 定义，Task 2 的测试用命名赋值；`Instance{id,fn_id,tid,depth,parent,children,start_ns,end_ns,self_ns}`、`FuncStats`、`Edge`、`AggNode`、`Options{include,exclude,min_total_ns,top,has_include,has_exclude}`、`AnalysisResult{tree,agg}` 在后续任务中一致引用。
- **已知取舍：** 聚合树的 `build` 与 `merge_child` 存在局部重复逻辑，实现者需保证同 fn_id 子节点递归合并；`to_json` 的 `threads` 段需按真实线程输出（计划已注明）。
- **接口接缝：** 分析器仅产出 `analysis.json` + 文本；HTML 渲染属计划 3。

---

## 执行交接

计划已保存到 `docs/superpowers/plans/2026-09-30-ctiming-analyzer.md`。两种执行方式：

1. **子代理驱动（推荐）**：每个 Task 派发全新子代理，Task 间两阶段评审。
2. **当前会话内执行**：用 executing-plans 分批执行，带检查点。

选哪种？
