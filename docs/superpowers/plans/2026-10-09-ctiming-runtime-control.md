# ctiming 运行期控制与子树追踪 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 给 `libctiming` 增加运行期控制通道（FIFO）与“指定符号子树追踪”。

**Architecture:** 在现有运行时上新增：控制 FIFO 后台线程；追踪目标地址快照（由符号名解析）；钩子内按线程的捕获开关（`tls_capture`）。设计见 `docs/superpowers/specs/2026-10-09-ctiming-runtime-control-design.md`。

**Tech Stack:** C11、pthread、Linux FIFO。零第三方依赖。

**规则：** 代码不写注释；运行时函数 `CT_NOINSTR`，对外 API `CTIMING_HIDDEN`。

---

## Task 1: 追踪目标快照 + 子树捕获 + API/环境变量

**Files:**
- Modify: `include/ctiming.h`
- Modify: `src/config.h` / `src/config.c`
- Modify: `src/runtime.c`
- Modify: `tests/test_config.c`
- Create: `examples/example_control.c`
- Create: `tests/integration_trace_env.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: `include/ctiming.h` 增加 API**

在 `ctiming_set_max_depth` 之后加：

```c
int ctiming_set_trace_symbol(const char *pattern);
```

- [ ] **Step 2: `src/config.h` 增加字段**

在 `int exclude_lib;` 后加：

```c
  char *ctl_path;
  char *trace_pattern;
```

- [ ] **Step 3: `src/config.c` 解析/清理**

在 `c->exclude = dup_env("CTIMING_EXCLUDE");` 之后加：

```c
  c->ctl_path = dup_env("CTIMING_CTL");
  c->trace_pattern = dup_env("CTIMING_TRACE");
```

`ct_config_clear` 增加：

```c
  free(c->ctl_path);
  free(c->trace_pattern);
  c->ctl_path = NULL;
  c->trace_pattern = NULL;
```

- [ ] **Step 4: `tests/test_config.c`**

在开头 `unsetenv` 行加 `CTIMING_CTL`、`CTIMING_TRACE`；默认用例断言 `c.ctl_path == NULL && c.trace_pattern == NULL`；在 setenv 用例里加 `setenv("CTIMING_CTL","/tmp/x.fifo",1); setenv("CTIMING_TRACE","demo::hot",1);` 并断言二者相等。

- [ ] **Step 5: `src/runtime.c` 核心改动**

includes 增加 `<fcntl.h>`、`<sys/stat.h>`、`<errno.h>`。

类型与全局（放在 `ct_filter_snapshot` 之后）：

```c
typedef struct ct_trace_targets {
  uintptr_t *addrs;
  size_t n;
  struct ct_trace_targets *next;
} ct_trace_targets;

static _Atomic(ct_trace_targets *) g_trace = NULL;
static ct_trace_targets *g_trace_retired = NULL;
```

TLS 增加：

```c
static __thread int tls_capture = 0;
static __thread unsigned tls_capture_depth = 0;
```

新增函数（在 `pass_filter` 之前）：

```c
CT_NOINSTR static int cmp_uptr(const void *a, const void *b) {
  uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

CT_NOINSTR static ct_trace_targets *build_trace_targets(const char *pattern) {
  ct_trace_targets *t = (ct_trace_targets *)calloc(1, sizeof(*t));
  if (!t) return NULL;
  if (pattern == NULL || pattern[0] == '\0') return t;
  size_t cap = 16;
  t->addrs = (uintptr_t *)malloc(cap * sizeof(uintptr_t));
  if (!t->addrs) { free(t); return NULL; }
  for (size_t i = 0; i < g_syms.n_symbols; i++) {
    if (!ct_filter_match(pattern, NULL, g_syms.syms[i].name)) continue;
    if (t->n == cap) {
      size_t nc = cap * 2;
      uintptr_t *na = (uintptr_t *)realloc(t->addrs, nc * sizeof(uintptr_t));
      if (!na) break;
      t->addrs = na;
      cap = nc;
    }
    t->addrs[t->n++] = (uintptr_t)g_syms.syms[i].addr;
  }
  qsort(t->addrs, t->n, sizeof(uintptr_t), cmp_uptr);
  return t;
}

CT_NOINSTR static ct_trace_targets *trace_snapshot(void) {
  return atomic_load(&g_trace);
}

CT_NOINSTR static int trace_configured(void) {
  ct_trace_targets *t = trace_snapshot();
  return t && t->n > 0;
}

CT_NOINSTR static int trigger_match(uintptr_t fn) {
  ct_trace_targets *t = trace_snapshot();
  if (!t) return 0;
  size_t lo = 0, hi = t->n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uintptr_t a = t->addrs[mid];
    if (a == fn) return 1;
    if (a < fn) lo = mid + 1;
    else hi = mid;
  }
  return 0;
}

CT_NOINSTR static int pass_exclusion(uintptr_t fn) {
  const char *name = ct_symbols_lookup(&g_syms, fn, NULL, NULL);
  if (!name) return g_cfg.drop_unknown ? 0 : 1;
  if (g_cfg.exclude_lib && ct_lib_name_match(name)) return 0;
  ct_filter_snapshot *s = atomic_load(&g_filter);
  return ct_filter_match(NULL, s ? s->exclude : NULL, name);
}
```

把 `record` 改为接受 `forced` 并在 forced 时用 `pass_exclusion`：

```c
CT_NOINSTR static void record(uintptr_t fn, uintptr_t cs, ct_event_kind kind, unsigned depth, int forced) {
  if (tls_in_hook) return;
  tls_in_hook = 1;
  if (!atomic_load(&g_started)) pthread_once(&g_once, init_once);
  if (!atomic_load(&g_started) || !atomic_load(&g_enabled) || tls_truncated) { tls_in_hook = 0; return; }
  unsigned maxd = atomic_load(&g_max_depth);
  int ok = (maxd == 0 || depth < maxd) && (forced ? pass_exclusion(fn) : pass_filter(fn));
  if (ok) {
    ct_buffer *b = current_buffer();
    if (b) {
      ct_event e;
      e.tid = tls_tid; e.kind = (uint8_t)kind; e.ts = now_ns(); e.fn = fn; e.call_site = cs;
      ct_buffer_push(b, e);
      if (b->truncated) tls_truncated = 1;
    }
  }
  tls_in_hook = 0;
}
```

钩子改为：

```c
CT_NOINSTR void __cyg_profile_func_enter(void *fnp, void *cs) {
  uintptr_t fn = (uintptr_t)fnp;
  unsigned d = tls_depth;
  int forced = 0;
  if (trace_configured()) {
    if (!tls_capture && trigger_match(fn)) { tls_capture = 1; tls_capture_depth = d; }
    forced = tls_capture;
  }
  record(fn, (uintptr_t)cs, CT_EV_ENTER, d, forced);
  tls_depth = d + 1;
}

CT_NOINSTR void __cyg_profile_func_exit(void *fnp, void *cs) {
  uintptr_t fn = (uintptr_t)fnp;
  if (tls_depth) tls_depth--;
  unsigned d = tls_depth;
  int forced = (trace_configured() && tls_capture);
  record(fn, (uintptr_t)cs, CT_EV_EXIT, d, forced);
  if (tls_capture && d == tls_capture_depth) tls_capture = 0;
}
```

`init_once` 里 `atomic_store(&g_filter, ...)` 之后加：

```c
  atomic_store(&g_trace, build_trace_targets(g_cfg.trace_pattern));
```

`ct_atexit` 释放 retire 的追踪快照（在过滤快照释放之后）：

```c
  ct_trace_targets *tt = g_trace_retired;
  while (tt) { ct_trace_targets *nx = tt->next; free(tt->addrs); free(tt); tt = nx; }
  g_trace_retired = NULL;
```

`ctiming_set_filter` 同步 `g_cfg.include/exclude`（用于 FIFO 的 include/exclude 命令）：

```c
  pthread_mutex_lock(&g_retired_mu);
  old->next = g_filter_retired;
  g_filter_retired = old;
  if (g_cfg.include) free(g_cfg.include);
  if (g_cfg.exclude) free(g_cfg.exclude);
  g_cfg.include = include ? strdup(include) : NULL;
  g_cfg.exclude = exclude ? strdup(exclude) : NULL;
  pthread_mutex_unlock(&g_retired_mu);
```

新增公共 API（文件末尾）：

```c
CT_NOINSTR CTIMING_HIDDEN int ctiming_set_trace_symbol(const char *pattern) {
  pthread_once(&g_once, init_once);
  ct_trace_targets *t = build_trace_targets(pattern);
  if (!t) return -1;
  ct_trace_targets *old = atomic_exchange(&g_trace, t);
  if (old) {
    pthread_mutex_lock(&g_retired_mu);
    old->next = g_trace_retired;
    g_trace_retired = old;
    pthread_mutex_unlock(&g_retired_mu);
  }
  return (int)t->n;
}
```

- [ ] **Step 6: `examples/example_control.c`**

```c
#include <stdio.h>
#include <unistd.h>

static void leaf(int x) { volatile int s = 0; for (int i = 0; i < x; i++) s += i; (void)s; }
static int hot(int n) { int s = 0; for (int i = 0; i < n; i++) leaf(1000); return s; }
static void unrelated(void) { volatile int s = 0; for (int i = 0; i < 1000000; i++) s += i; (void)s; }

int main(int argc, char **argv) {
  int once = (argc > 1 && argv[1][0] == 'o');
  if (once) { hot(3); unrelated(); printf("done\n"); return 0; }
  for (int i = 0; i < 60; i++) usleep(5000);
  for (int i = 0; i < 200; i++) { if (i % 5 == 0) unrelated(); else hot(2); usleep(2000); }
  printf("done\n");
  return 0;
}
```

- [ ] **Step 7: `tests/integration_trace_env.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_TRACE='hot' CTIMING_OUT="$WORK/out.ctrace" "$EXE" once >/dev/null
test -s "$WORK/out.ctrace"
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/a.json" >/dev/null
python3 - "$WORK/a.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = [f["name"] for f in d["functions"] if f["calls"] > 0]
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert any("leaf" in n for n in names), names
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY
```

- [ ] **Step 8: `CMakeLists.txt`**

```cmake
add_executable(example_control examples/example_control.c)
target_compile_options(example_control PRIVATE -finstrument-functions -g -O0 -pthread)
target_link_libraries(example_control PRIVATE ctiming)

add_test(
  NAME integration_trace_env
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_trace_env.sh
          $<TARGET_FILE:example_control> $<TARGET_FILE:ctiming-analyze>)
```

- [ ] **Step 9: build + test**：`cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`（全绿）。

- [ ] **Step 10: commit**：`feat(runtime): 子树追踪目标快照与 CTIMING_TRACE`

---

## Task 2: 控制 FIFO 与命令处理

**Files:**
- Modify: `src/runtime.c`
- Create: `tests/integration_control.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: `src/runtime.c` 增加控制线程与命令处理**

新增全局：

```c
static _Atomic int g_ctl_started = 0;
```

命令处理（放在 `ctiming_set_trace_symbol` 之后；使用 `g_cfg`、`ctiming_*`）：

```c
CT_NOINSTR static void ctl_reply(const char *msg) {
  fprintf(stderr, "ctiming: %s\n", msg);
}

CT_NOINSTR static void ctl_command(char *line) {
  char *save = NULL;
  char *cmd = strtok_r(line, " \t\r\n", &save);
  if (!cmd || !*cmd) return;
  if (strcmp(cmd, "start") == 0) { ctiming_start(); ctl_reply("recording on"); }
  else if (strcmp(cmd, "stop") == 0) { ctiming_stop(); ctl_reply("recording off"); }
  else if (strcmp(cmd, "toggle") == 0) {
    if (atomic_load(&g_enabled)) { ctiming_stop(); ctl_reply("recording off"); }
    else { ctiming_start(); ctl_reply("recording on"); }
  }
  else if (strcmp(cmd, "dump") == 0) {
    char *path = strtok_r(NULL, " \t\r\n", &save);
    int rc = ctiming_dump(path);
    ctl_reply(rc == 0 ? "dumped" : "dump failed");
  }
  else if (strcmp(cmd, "trace") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    if (!arg || !*arg || strcmp(arg, "off") == 0) {
      ctiming_set_trace_symbol(NULL);
      ctl_reply("trace cleared");
    } else {
      int n = ctiming_set_trace_symbol(arg);
      char buf[128];
      snprintf(buf, sizeof(buf), "trace '%s' -> %d address(es)", arg, n);
      ctl_reply(buf);
    }
  }
  else if (strcmp(cmd, "untrace") == 0) { ctiming_set_trace_symbol(NULL); ctl_reply("trace cleared"); }
  else if (strcmp(cmd, "include") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    ctiming_set_filter((arg && *arg) ? arg : NULL, g_cfg.exclude);
    ctl_reply("include set");
  }
  else if (strcmp(cmd, "exclude") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    ctiming_set_filter(g_cfg.include, (arg && *arg) ? arg : NULL);
    ctl_reply("exclude set");
  }
  else if (strcmp(cmd, "status") == 0) {
    char buf[256];
    snprintf(buf, sizeof(buf), "enabled=%d trace=%s include=%s exclude=%s",
             atomic_load(&g_enabled), trace_configured() ? "on" : "off",
             g_cfg.include ? g_cfg.include : "-", g_cfg.exclude ? g_cfg.exclude : "-");
    ctl_reply(buf);
  }
  else ctl_reply("unknown command");
}

CT_NOINSTR static void *ctl_thread_main(void *arg) {
  (void)arg;
  const char *path = g_cfg.ctl_path;
  if (mkfifo(path, 0600) != 0 && errno != EEXIST) { ctl_reply("mkfifo failed"); return NULL; }
  int fd = open(path, O_RDWR);
  if (fd < 0) { ctl_reply("open ctl failed"); return NULL; }
  FILE *f = fdopen(fd, "r");
  if (!f) { close(fd); return NULL; }
  char line[512];
  while (fgets(line, sizeof(line), f)) ctl_command(line);
  fclose(f);
  return NULL;
}

CT_NOINSTR static void start_control(void) {
  if (g_cfg.ctl_path == NULL || g_cfg.ctl_path[0] == '\0') return;
  if (atomic_exchange(&g_ctl_started, 1)) return;
  pthread_t th;
  if (pthread_create(&th, NULL, ctl_thread_main, NULL) == 0) pthread_detach(th);
}
```

在 `init_once` 末尾（`atomic_store(&g_started, 1);` 之前）调用 `start_control();`。

- [ ] **Step 2: `tests/integration_control.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
FIFO="$WORK/ctl"
CTIMING_CTL="$FIFO" CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null 2>"$WORK/err.log" &
PID=$!
for _ in $(seq 1 100); do [ -p "$FIFO" ] && break; sleep 0.02; done
test -p "$FIFO"
echo 'trace hot' > "$FIFO"
sleep 0.05
echo 'status' > "$FIFO"
wait "$PID" || true
test -s "$WORK/out.ctrace"
"$ANALYZE" "$WORK/out.ctrace" --json "$WORK/a.json" >/dev/null
python3 - "$WORK/a.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
names = [f["name"] for f in d["functions"] if f["calls"] > 0]
assert any(n == "hot" or n.endswith("hot") for n in names), names
assert any("leaf" in n for n in names), names
assert not any("unrelated" in n for n in names), "unrelated leaked: %r" % names
PY
grep -q "trace 'hot'" "$WORK/err.log"
```

- [ ] **Step 3: `CMakeLists.txt`**

```cmake
add_test(
  NAME integration_control
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_control.sh
          $<TARGET_FILE:example_control> $<TARGET_FILE:ctiming-analyze>)
```

- [ ] **Step 4: build + test**：`cmake --build build -j && ctest --test-dir build --output-on-failure`（全绿）。

- [ ] **Step 5: commit**：`feat(runtime): 控制 FIFO（start/stop/dump/trace/include/exclude/status）`

---

## Task 3: 文档

**Files:**
- Modify: `docs/usage.md`
- Modify: `README.md`

- [ ] **Step 1: `docs/usage.md`** 增加：`CTIMING_CTL`、`CTIMING_TRACE` 环境变量行；新增“运行期控制与子树追踪”一节，列出 FIFO 命令、子树语义、与过滤的关系、`ctiming_set_trace_symbol` API。
- [ ] **Step 2: `README.md`** 环境变量表补 `CTIMING_CTL`/`CTIMING_TRACE`；快速开始加一段 FIFO 用法示例。
- [ ] **Step 3: 全量测试** `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`（全绿）。
- [ ] **Step 4: commit**：`docs: 运行期控制与子树追踪说明`

---

## 自审记录

- **规格覆盖：** FIFO 通道（Task 2）、子树语义与地址快照（Task 1）、启动期+运行期设置（Task 1/2）、API（Task 1）、过滤交互（Task 1 `pass_exclusion`）、文档（Task 3）。
- **类型/接口一致性：** `ct_trace_targets{addrs,n,next}`、`g_trace`、`tls_capture/tls_capture_depth`、`record(...,forced)`、`ctiming_set_trace_symbol` 在各步骤一致。
- **取舍：** `dump` 的线程覆盖为既有行为；FIFO 启动在 `init_once` 内；追踪目标地址在设置时刻解析。
