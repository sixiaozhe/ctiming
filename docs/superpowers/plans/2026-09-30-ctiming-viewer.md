# ctiming 计划 3：HTML 查看器 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `ctiming-analyze -o report.html` 生成一个**自包含、离线可用**的 HTML 报告：内嵌分析数据与查看器（Vanilla JS + SVG，零外部依赖），提供概览、火焰图、调用关系图、单次追踪、调用者/被调用者五个视图。

**Architecture:** 分析器已有的 `analysis.json` 数据结构不变。新增 HTML 生成器 `to_html(json)`（C++），把内嵌的查看器资源（`viewer/*.css`、`viewer/*.js`，编译期内嵌进二进制）+ 数据拼成单文件 HTML。查看器在浏览器端解析内嵌 JSON，按 Tab 渲染各视图。

**Tech Stack:** C++17（生成器、CLI）、Vanilla JS（ES2017，无框架）、SVG、内联 CSS、CMake 资源内嵌；零外部依赖，无 CDN，离线可用。

**参考规格:** `docs/superpowers/specs/2026-09-30-ctiming-design.md` 第 8 节（查看器 UX）。

---

## 数据契约（来自计划 2 的 `analysis.json`）

- `trace`: `{exe,pid,flags,modules,symbols,threads,total_events,dropped,unbalanced_enter,orphan_exit}`
- `functions`: `[{id,name,module,offset,calls,total_ns,self_ns,min_ns,max_ns,kept}]`
- `call_graph`: `[{caller,callee,count,total_ns,recursive}]`
- `aggregated`: `[{fn,calls,total_ns,self_ns,children:[...递归...]}]`（无 `name`，用 `fn` 回查 `functions`）
- `threads`: `[{tid,roots:[instance_id,...]}]`
- `instances`: `[{id,fn,tid,depth,parent,start_ns,end_ns,self_ns,children:[id...]}]`（无 `name`）

查看器一律用 `functions` 建 `id→{name,...}` 映射回查名称。

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `viewer/viewer.css` | 全部样式（主题、Tab、表格、火焰图/瀑布条、图形） |
| `viewer/core.js` | 数据索引、格式化、DOM 辅助、Tab 注册与初始化骨架 |
| `viewer/overview.js` | 概览 Tab：KPI + 热点表 |
| `viewer/flame.js` | 火焰图/icicle Tab（聚合树，下钻/反向） |
| `viewer/graph.js` | 调用关系图 Tab（节点-连线） |
| `viewer/trace.js` | 单次追踪 Tab（调用实例列表 + 瀑布图） |
| `viewer/callers.js` | 调用者/被调用者 Tab（搜索 + 上下游表） |
| `viewer/main.js` | 启动：解析数据、建 Tab 栏、全局搜索、渲染 |
| `cmake/embed_assets.cmake` | 把 `viewer/*` 转成 C++ 头（raw string） |
| `src/analyze/html.hpp` / `.cpp` | `std::string to_html(const std::string &json);` |
| `tests/test_html.cpp` | 生成器单测 |
| `tests/integration_report.sh` | 端到端：生成 `report.html` 并校验结构/数据 |
| `docs/viewer.md` | 查看器说明（简体中文） |

**约定：** 生成的 HTML 中，数据放在 `<script id="ct-data" type="application/json">…</script>`，JS 用 `JSON.parse(el.textContent)` 读取；嵌入前把 JSON 文本中的 `</` 替换为 `<\/`，防止 `</script>` 提前闭合。查看器资源以 `<style>`/`<script>` 内联。**代码不写注释**（HTML/CSS/JS/C++ 均不写解释性注释）。

---

## Task 1: 资源内嵌 + HTML 生成器 + core.js

**Files:**
- Create: `cmake/embed_assets.cmake`
- Create: `viewer/viewer.css`
- Create: `viewer/core.js`
- Create: `src/analyze/html.hpp`
- Create: `src/analyze/html.cpp`
- Create: `tests/test_html.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 写资源内嵌脚本 `cmake/embed_assets.cmake`**

```cmake
file(WRITE "${OUT}" "#pragma once\nnamespace ct { namespace assets {\n")
foreach(f IN LISTS FILES)
  get_filename_component(_name "${f}" NAME_WE)
  get_filename_component(_ext "${f}" EXT)
  file(READ "${f}" _content)
  if(_ext STREQUAL ".css")
    set(_var "css_${_name}")
  else()
    set(_var "js_${_name}")
  endif()
  file(APPEND "${OUT}" "static const char ${_var}[] = R\"CTASSET(${_content})CTASSET\";\n")
endforeach()
file(APPEND "${OUT}" "}}\n")
```

- [ ] **Step 2: 写 `viewer/viewer.css`**

```css
:root{--bg:#0f1420;--panel:#161d2c;--fg:#dbe3f0;--muted:#8b97ad;--accent:#5b9dff;--line:#26314a;--hot:#ff7b72;--warm:#f5c86b;--cool:#7ee0b8;--cold:#6ea8fe}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:13px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans SC",sans-serif}
header{display:flex;align-items:center;gap:16px;padding:10px 16px;background:var(--panel);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:5}
header h1{font-size:15px;margin:0;font-weight:600}
header .meta{color:var(--muted);font-size:12px}
nav{display:flex;gap:4px;padding:8px 16px;background:var(--panel);border-bottom:1px solid var(--line);position:sticky;top:49px;z-index:4}
nav button{background:transparent;color:var(--fg);border:1px solid transparent;border-radius:6px;padding:5px 12px;cursor:pointer;font-size:13px}
nav button.active{background:var(--accent);color:#08101f;font-weight:600}
nav button:hover{border-color:var(--line)}
main{padding:16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:16px}
.kpi{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px}
.kpi .v{font-size:20px;font-weight:700}
.kpi .k{color:var(--muted);font-size:12px}
table{border-collapse:collapse;width:100%;background:var(--panel);border-radius:8px;overflow:hidden}
th,td{padding:6px 10px;text-align:left;border-bottom:1px solid var(--line);white-space:nowrap}
th{color:var(--muted);cursor:pointer;position:sticky;top:0;background:var(--panel)}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
tbody tr:hover{background:#1d2740}
.bar{height:12px;border-radius:3px;background:var(--cold)}
.row{display:flex;align-items:center;gap:8px;height:18px}
.row .lbl{width:230px;flex:none;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--fg)}
.row .track{position:relative;flex:1;height:14px;background:#0b101b;border-radius:3px}
.row .fill{position:absolute;top:0;height:14px;border-radius:3px}
svg{display:block;width:100%}
.tooltip{position:fixed;pointer-events:none;background:#0b101b;border:1px solid var(--line);border-radius:6px;padding:6px 8px;font-size:12px;z-index:20;display:none}
input[type=search]{background:#0b101b;border:1px solid var(--line);color:var(--fg);border-radius:6px;padding:5px 10px;font-size:13px;min-width:280px}
.pill{display:inline-block;padding:1px 7px;border-radius:10px;background:#22304d;color:#9fb6e0;font-size:11px}
.muted{color:var(--muted)}
.crumb{color:var(--muted);margin:0 0 10px}
.crumb a{color:var(--accent);cursor:pointer;text-decoration:none}
.split{display:grid;grid-template-columns:340px 1fr;gap:16px;align-items:start}
.list{max-height:70vh;overflow:auto;background:var(--panel);border:1px solid var(--line);border-radius:8px}
.list .item{padding:6px 10px;border-bottom:1px solid var(--line);cursor:pointer;display:flex;justify-content:space-between;gap:8px}
.list .item:hover{background:#1d2740}
.list .item.sel{background:#22304d}
</style>
```
(Note: the trailing `</style>` must NOT be in the CSS file — the CSS file contains only CSS rules; the generator wraps it in `<style>`. Remove that stray tag when writing the file.)

- [ ] **Step 3: 写 `viewer/core.js`**（基础索引/格式化/DOM/Tab 注册）

```js
(function () {
  const CT = (window.CT = window.CT || {});
  CT.tabs = [];
  CT.state = { active: 0 };

  CT.init = function (data) {
    CT.data = data;
    CT.fn = new Map(data.functions.map((f) => [f.id, f]));
    CT.inst = new Map(data.instances.map((i) => [i.id, i]));
    CT.outEdges = new Map();
    CT.inEdges = new Map();
    for (const e of data.call_graph) {
      if (!CT.outEdges.has(e.caller)) CT.outEdges.set(e.caller, []);
      if (!CT.inEdges.has(e.callee)) CT.inEdges.set(e.callee, []);
      CT.outEdges.get(e.caller).push(e);
      CT.inEdges.get(e.callee).push(e);
    }
  };

  CT.name = function (fnId) {
    const f = CT.fn.get(fnId);
    return f ? f.name : "fn#" + fnId;
  };

  CT.fmtNs = function (ns) {
    if (ns >= 1e9) return (ns / 1e9).toFixed(3) + " s";
    if (ns >= 1e6) return (ns / 1e6).toFixed(3) + " ms";
    if (ns >= 1e3) return (ns / 1e3).toFixed(3) + " µs";
    return ns + " ns";
  };

  CT.fmtCount = function (n) {
    return n.toLocaleString("en-US");
  };

  CT.pct = function (a, b) {
    return b > 0 ? ((a / b) * 100).toFixed(1) + "%" : "0%";
  };

  CT.el = function (tag, attrs, kids) {
    const n = document.createElement(tag);
    if (attrs) {
      for (const k in attrs) {
        if (k === "class") n.className = attrs[k];
        else if (k === "text") n.textContent = attrs[k];
        else if (k === "html") n.innerHTML = attrs[k];
        else n.setAttribute(k, attrs[k]);
      }
    }
    if (kids != null) {
      for (const c of [].concat(kids)) {
        if (c == null) continue;
        n.appendChild(typeof c === "string" ? document.createTextNode(c) : c);
      }
    }
    return n;
  };

  CT.clear = function (node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  };

  CT.registerTab = function (id, label, render) {
    CT.tabs.push({ id: id, label: label, render: render });
  };

  CT.showTab = function (i) {
    CT.state.active = i;
    const root = document.getElementById("view");
    CT.clear(root);
    document.querySelectorAll("nav button").forEach((b, j) => {
      b.classList.toggle("active", j === i);
    });
    CT.tabs[i].render(root);
  };

  CT.tooltip = function (text, x, y) {
    let t = document.getElementById("tooltip");
    if (!t) {
      t = CT.el("div", { class: "tooltip", id: "tooltip" });
      document.body.appendChild(t);
    }
    t.textContent = text;
    t.style.display = "block";
    t.style.left = x + 12 + "px";
    t.style.top = y + 12 + "px";
  };
  CT.hideTooltip = function () {
    const t = document.getElementById("tooltip");
    if (t) t.style.display = "none";
  };
})();
```

- [ ] **Step 4: 写 `src/analyze/html.hpp`**

```cpp
#pragma once
#include <string>

namespace ct {
std::string to_html(const std::string &json);
}
```

- [ ] **Step 5: 写 `src/analyze/html.cpp`**

```cpp
#include "html.hpp"
#include "viewer_assets.hpp"
#include <string>

namespace ct {

static std::string escape_script(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '<' && i + 1 < s.size() && s[i + 1] == '/') {
      out += "<\\/";
      i++;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string to_html(const std::string &json) {
  std::string h;
  h += "<!DOCTYPE html>\n<html lang=\"zh\"><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  h += "<title>ctiming report</title>\n<style>";
  h += assets::css_viewer;
  h += "</style></head>\n<body>\n<header><h1>ctiming report</h1><span class=\"meta\" id=\"meta\"></span></header>\n";
  h += "<nav id=\"nav\"></nav>\n<main id=\"view\"></main>\n";
  h += "<script id=\"ct-data\" type=\"application/json\">";
  h += escape_script(json);
  h += "</script>\n";
  h += "<script>";
  h += assets::js_core;
  h += "</script>\n<script>";
  h += assets::js_overview;
  h += "</script>\n<script>";
  h += assets::js_flame;
  h += "</script>\n<script>";
  h += assets::js_graph;
  h += "</script>\n<script>";
  h += assets::js_trace;
  h += "</script>\n<script>";
  h += assets::js_callers;
  h += "</script>\n<script>";
  h += assets::js_main;
  h += "</script>\n</body></html>\n";
  return h;
}

} // namespace ct
```

- [ ] **Step 6: 写 `tests/test_html.cpp`**

```cpp
#include "check.h"
#include "html.hpp"
#include <string>

int main() {
  int fails = 0;
  std::string js = "{\"trace\":{\"pid\":1},\"functions\":[],\"call_graph\":[],\"aggregated\":[],\"threads\":[],\"instances\":[]}";
  std::string h = ct::to_html(js);
  CHECK(h.rfind("<!DOCTYPE html>", 0) == 0);
  CHECK(h.find("id=\"ct-data\"") != std::string::npos);
  CHECK(h.find("\"pid\":1") != std::string::npos);
  CHECK(h.find("CT.init") != std::string::npos);
  CHECK(h.find("function CT.init") != std::string::npos || h.find("CT.init =") != std::string::npos);
  CHECK(h.find("</html>") != std::string::npos);
  // </script> 防闭合
  std::string evil = "{\"functions\":[{\"name\":\"</script><x>\"}]}";
  std::string h2 = ct::to_html(evil);
  CHECK(h2.find("</script><x>") == std::string::npos);
  CHECK(h2.find("<\\/script>") != std::string::npos);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
```

- [ ] **Step 7: 创建占位查看器脚本**（本 Task 先占位，后续 Task 覆盖）

创建空但含命名空间的文件，保证 `viewer_assets.hpp` 变量齐全：
- `viewer/overview.js`: `(function(){ const CT=window.CT; CT.registerTab("overview","概览",function(root){ root.textContent="overview"; }); })();`
- `viewer/flame.js`: 同上，`id="flame"`,`label="火焰图"`。
- `viewer/graph.js`: `id="graph"`,`label="调用关系图"`。
- `viewer/trace.js`: `id="trace"`,`label="单次追踪"`。
- `viewer/callers.js`: `id="callers"`,`label="调用者/被调用者"`。
- `viewer/main.js`: 解析数据并启动：

```js
(function () {
  const CT = window.CT;
  const data = JSON.parse(document.getElementById("ct-data").textContent);
  CT.init(data);
  const meta = document.getElementById("meta");
  if (meta) {
    meta.textContent =
      data.trace.exe + " · threads " + data.trace.threads +
      " · events " + data.trace.total_events +
      (data.trace.dropped ? " · dropped " + data.trace.dropped : "");
  }
  const nav = document.getElementById("nav");
  CT.tabs.forEach(function (t, i) {
    nav.appendChild(
      CT.el("button", { type: "button", text: t.label, onclick: function () { CT.showTab(i); } })
    );
  });
  if (CT.tabs.length) CT.showTab(0);
})();
```

- [ ] **Step 8: 更新 `CMakeLists.txt`**

在 `ctiming_analysis` 定义之后、生成器之前加入资源内嵌与 html.cpp：

```cmake
set(CT_VIEWER_ASSETS
  viewer/viewer.css
  viewer/core.js
  viewer/overview.js
  viewer/flame.js
  viewer/graph.js
  viewer/trace.js
  viewer/callers.js
  viewer/main.js)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/viewer_assets.hpp
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${CMAKE_BINARY_DIR}/viewer_assets.hpp
          "-DFILES=${CT_VIEWER_ASSETS}"
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_assets.cmake
  DEPENDS ${CT_VIEWER_ASSETS} ${CMAKE_SOURCE_DIR}/cmake/embed_assets.cmake
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_custom_target(ct_viewer_assets DEPENDS ${CMAKE_BINARY_DIR}/viewer_assets.hpp)

target_sources(ctiming_analysis PRIVATE src/analyze/html.cpp)
target_include_directories(ctiming_analysis PRIVATE ${CMAKE_BINARY_DIR})
add_dependencies(ctiming_analysis ct_viewer_assets)

ct_add_analysis_test(test_html tests/test_html.cpp)
```

- [ ] **Step 9: 构建并运行测试**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: `test_html` PASS，全部测试通过，无警告。

- [ ] **Step 10: 提交**

```bash
git add cmake viewer src/analyze/html.hpp src/analyze/html.cpp tests/test_html.cpp CMakeLists.txt
git commit -m "feat(viewer): 资源内嵌与 HTML 生成器骨架"
```

---

## Task 2: 概览 Tab（KPI + 热点表）

**Files:**
- Modify: `viewer/overview.js`
- Modify: `tests/test_html.cpp`（断言含关键标记）

- [ ] **Step 1: 实现 `viewer/overview.js`**

```js
(function () {
  const CT = window.CT;

  const SORTS = [
    { key: "total_ns", label: "总耗时" },
    { key: "self_ns", label: "自身耗时" },
    { key: "calls", label: "调用次数" },
  ];
  let sortKey = "total_ns";

  function kpi(v, k) {
    return CT.el("div", { class: "kpi" }, [
      CT.el("div", { class: "v", text: v }),
      CT.el("div", { class: "k", text: k }),
    ]);
  }

  function render(root) {
    const t = CT.data.trace;
    const funcs = CT.data.functions.filter(function (f) { return f.calls > 0; });
    let grandTotal = 0;
    for (const f of CT.data.functions) if (f.total_ns > grandTotal) grandTotal = f.total_ns;

    CT.clear(root);
    root.appendChild(
      CT.el("div", { class: "grid" }, [
        kpi(CT.fmtNs(t.total_events ? grandTotal : 0), "最重函数总耗时"),
        kpi(CT.fmtCount(t.total_events), "事件数"),
        kpi(CT.fmtCount(t.threads), "线程数"),
        kpi(CT.fmtCount(funcs.length), "有调用的函数数"),
        kpi(t.dropped ? CT.fmtCount(t.dropped) : "0", "截断/丢弃"),
        kpi(t.unbalanced_enter ? CT.fmtCount(t.unbalanced_enter) : "0", "未配对 ENTER"),
      ])
    );

    const header = CT.el("tr");
    for (const col of [
      { k: "name", label: "函数" },
      { k: "calls", label: "调用次数", num: true },
      { k: "self_ns", label: "自身耗时", num: true },
      { k: "total_ns", label: "总耗时", num: true },
      { k: "max_ns", label: "最慢一次", num: true },
      { k: "pct", label: "占比", num: true },
    ]) {
      const th = CT.el("th", { text: col.label });
      if (col.num) th.classList.add("num");
      if (col.k === "self_ns" || col.k === "total_ns" || col.k === "calls") {
        th.appendChild(CT.el("span", { class: "muted", text: sortKey === col.k ? " ▾" : "" }));
        th.onclick = function () { sortKey = col.k; render(root); };
      }
      header.appendChild(th);
    }
    const table = CT.el("table", null, [CT.el("thead", null, header)]);
    const tbody = CT.el("tbody");

    funcs.sort(function (a, b) { return b[sortKey] - a[sortKey]; });
    const maxTotal = funcs.length ? funcs[0].total_ns : 1;
    const top = funcs.slice(0, 200);
    for (const f of top) {
      const tr = CT.el("tr");
      tr.appendChild(CT.el("td", { title: f.name }, [CT.el("span", { text: f.name })]));
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtCount(f.calls) }));
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtNs(f.self_ns) }));
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtNs(f.total_ns) }));
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtNs(f.max_ns) }));
      tr.appendChild(CT.el("td", { class: "num", text: CT.pct(f.total_ns, maxTotal) }));
      tr.style.cursor = "pointer";
      tr.onclick = function () { CT.openCallers(f.id); };
      tbody.appendChild(tr);
    }
    table.appendChild(tbody);
    root.appendChild(table);
    if (funcs.length > top.length) {
      root.appendChild(CT.el("p", { class: "muted", text: "仅显示前 200 个函数，请用顶部搜索定位具体函数。" }));
    }
  }

  CT.registerTab("overview", "概览", render);
})();
```

- [ ] **Step 2: 在 `viewer/core.js` 增加 `CT.openCallers`（跨 Tab 跳转占位）**

在 core.js 的 `CT.registerTab` 之后加入：

```js
  CT.openCallers = function (fnId) {
    CT.pendingFn = fnId;
    for (let i = 0; i < CT.tabs.length; i++)
      if (CT.tabs[i].id === "callers") { CT.showTab(i); return; }
  };
```

- [ ] **Step 3: 更新 `tests/test_html.cpp`**

在 `return fails ? 1 : 0;` 之前加：

```cpp
  CHECK(h.find("CT.registerTab(\"overview\"") != std::string::npos);
  CHECK(h.find("CT.openCallers") != std::string::npos);
```

- [ ] **Step 4: 构建并测试**

Run: `cmake --build build -j && ctest --test-dir build -R test_html --output-on-failure` → PASS。

- [ ] **Step 5: 提交**

```bash
git add viewer/overview.js viewer/core.js tests/test_html.cpp
git commit -m "feat(viewer): 概览 Tab 与 KPI/热点表"
```

---

## Task 3: 火焰图 / icicle Tab

**Files:**
- Modify: `viewer/flame.js`
- Modify: `tests/test_html.cpp`

- [ ] **Step 1: 实现 `viewer/flame.js`**（聚合树，宽度=total_ns，点击下钻，反向自底向上）

```js
(function () {
  const CT = window.CT;
  let rootNode = null;
  let reversed = false;
  const stack = [];

  function frameHeight() { return 20; }

  function subtreeTotal(node) { return node.total_ns || 1; }

  function render(root) {
    CT.clear(root);
    const bars = [];
    const whole = rootNode ? subtreeTotal(rootNode) : 1;
    const nodes = rootNode ? [rootNode] : CT.data.aggregated;
    const total = rootNode ? whole : nodes.reduce(function (s, n) { return s + n.total_ns; }, 0) || 1;

    const crumb = CT.el("p", { class: "crumb" });
    crumb.appendChild(CT.el("a", { text: "全部", onclick: function () { rootNode = null; render(root); } }));
    for (let i = reversed ? stack.length - 1 : 0; reversed ? i >= 0 : i < stack.length; reversed ? i-- : i++) {
      (function (k) {
        crumb.appendChild(document.createTextNode(" / "));
        crumb.appendChild(CT.el("a", {
          text: CT.name(stack[k].fn),
          onclick: function () { stack.splice(k + 1); rootNode = stack[k]; render(root); },
        }));
      })(i);
    }
    crumb.appendChild(document.createTextNode("   "));
    const rev = CT.el("button", { type: "button", text: reversed ? "自底向上 ✓" : "自底向上", onclick: function () { reversed = !reversed; render(root); } });
    crumb.appendChild(rev);
    root.appendChild(crumb);

    const H = Math.max(120, 320);
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 1000 " + H);
    svg.setAttribute("height", String(H));

    const W = 1000;
    const rows = [];
    function place(node, depth, x0, x1) {
      rows.push({ node: node, depth: depth, x0: x0, x1: x1 });
      const kids = reversed ? null : node.children;
      if (!kids || !kids.length) return;
      const t = subtreeTotal(node);
      let x = x0;
      for (const c of kids) {
        const w = (c.total_ns / t) * (x1 - x0);
        place(c, depth + 1, x, x + w);
        x += w;
      }
    }
    for (const n of nodes) {
      const w = (n.total_ns / total) * W;
      const baseX = (() => { let acc = 0; for (const m of nodes) { if (m === n) return acc; acc += (m.total_ns / total) * W; } return acc; })();
      place(n, 0, baseX, baseX + w);
    }

    const rowH = 22;
    for (const r of rows) {
      const g = document.createElementNS("http://www.w3.org/2000/svg", "g");
      const rect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      const x = (r.x0 / W) * W;
      const w = Math.max(1, r.x1 - r.x0);
      rect.setAttribute("x", String(x));
      rect.setAttribute("y", String(r.depth * rowH));
      rect.setAttribute("width", String(w));
      rect.setAttribute("height", String(rowH - 2));
      rect.setAttribute("rx", "3");
      const frac = r.node.total_ns / total;
      rect.setAttribute("fill", frac > 0.5 ? "var(--hot)" : frac > 0.2 ? "var(--warm)" : frac > 0.05 ? "var(--cool)" : "var(--cold)");
      rect.setAttribute("opacity", "0.9");
      const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
      label.setAttribute("x", String(x + 4));
      label.setAttribute("y", String(r.depth * rowH + 15));
      label.setAttribute("font-size", "11");
      label.setAttribute("fill", "#0b101b");
      label.textContent = w > 60 ? CT.name(r.node.fn) : "";
      const tip = CT.name(r.node.fn) + " · 总 " + CT.fmtNs(r.node.total_ns) + " · 自身 " + CT.fmtNs(r.node.self_ns) + " · 调用 " + CT.fmtCount(r.node.calls);
      rect.addEventListener("mousemove", function (e) { CT.tooltip(tip, e.clientX, e.clientY); });
      rect.addEventListener("mouseleave", CT.hideTooltip);
      if (!reversed) {
        rect.addEventListener("click", function () {
          if (r.node.children && r.node.children.length) {
            stack.push(r.node);
            rootNode = r.node;
            render(root);
          } else {
            CT.openCallers(r.node.fn);
          }
        });
      }
      g.appendChild(rect);
      g.appendChild(label);
      svg.appendChild(g);
    }
    root.appendChild(svg);
    if (reversed) {
      root.appendChild(CT.el("p", { class: "muted", text: "自底向上视图暂显示聚合树（叶子到根）；下钻以顶部面包屑为准。" }));
    }
  }

  CT.registerTab("flame", "火焰图", render);
})();
```

- [ ] **Step 2: `tests/test_html.cpp` 加断言**

```cpp
  CHECK(h.find("CT.registerTab(\"flame\"") != std::string::npos);
```

- [ ] **Step 3: 构建并测试**

Run: `cmake --build build -j && ctest --test-dir build -R test_html --output-on-failure` → PASS。

- [ ] **Step 4: 提交**

```bash
git add viewer/flame.js tests/test_html.cpp
git commit -m "feat(viewer): 火焰图/icicle 视图（下钻与反向）"
```

---

## Task 4: 调用关系图 Tab

**Files:**
- Modify: `viewer/graph.js`
- Modify: `tests/test_html.cpp`

- [ ] **Step 1: 实现 `viewer/graph.js`**（按总耗时取前 N，环形布局，边宽=count）

```js
(function () {
  const CT = window.CT;
  const N = 40;

  function render(root) {
    CT.clear(root);
    const byTotal = CT.data.functions.slice().sort(function (a, b) { return b.total_ns - a.total_ns; });
    const chosen = byTotal.filter(function (f) { return f.calls > 0; }).slice(0, N);
    const idset = new Set(chosen.map(function (f) { return f.id; }));
    const W = 1000, H = 760, cx = W / 2, cy = H / 2, R = 300;
    const pos = new Map();
    chosen.forEach(function (f, i) {
      const a = (2 * Math.PI * i) / chosen.length - Math.PI / 2;
      pos.set(f.id, [cx + R * Math.cos(a), cy + R * Math.sin(a)]);
    });

    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    svg.setAttribute("height", String(H));

    const edges = CT.data.call_graph.filter(function (e) { return idset.has(e.caller) && idset.has(e.callee); });
    const maxCount = edges.reduce(function (m, e) { return Math.max(m, e.count); }, 1);
    for (const e of edges) {
      const a = pos.get(e.caller), b = pos.get(e.callee);
      if (!a || !b) continue;
      const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
      line.setAttribute("x1", String(a[0]));
      line.setAttribute("y1", String(a[1]));
      line.setAttribute("x2", String(b[0]));
      line.setAttribute("y2", String(b[1]));
      line.setAttribute("stroke", e.recursive ? "var(--hot)" : "var(--line)");
      line.setAttribute("stroke-width", String(0.5 + 4 * (e.count / maxCount)));
      line.setAttribute("opacity", "0.7");
      const tip = CT.name(e.caller) + " → " + CT.name(e.callee) + " · " + CT.fmtCount(e.count) + " 次 · " + CT.fmtNs(e.total_ns) + (e.recursive ? " · 递归" : "");
      line.addEventListener("mousemove", function (ev) { CT.tooltip(tip, ev.clientX, ev.clientY); });
      line.addEventListener("mouseleave", CT.hideTooltip);
      svg.appendChild(line);
    }
    for (const f of chosen) {
      const p = pos.get(f.id);
      const c = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      c.setAttribute("cx", String(p[0]));
      c.setAttribute("cy", String(p[1]));
      c.setAttribute("r", "6");
      c.setAttribute("fill", f.kept ? "var(--accent)" : "var(--muted)");
      c.style.cursor = "pointer";
      c.addEventListener("click", function () { CT.openCallers(f.id); });
      c.addEventListener("mousemove", function (ev) { CT.tooltip(CT.name(f.id) + " · 总 " + CT.fmtNs(f.total_ns), ev.clientX, ev.clientY); });
      c.addEventListener("mouseleave", CT.hideTooltip);
      svg.appendChild(c);
      const t = document.createElementNS("http://www.w3.org/2000/svg", "text");
      t.setAttribute("x", String(p[0]));
      t.setAttribute("y", String(p[1] + 18));
      t.setAttribute("font-size", "11");
      t.setAttribute("fill", "var(--fg)");
      t.setAttribute("text-anchor", "middle");
      t.textContent = CT.name(f.id);
      svg.appendChild(t);
    }
    root.appendChild(CT.el("p", { class: "muted", text: "显示总耗时前 " + chosen.length + " 个函数；红边为递归调用。点击节点查看上下游。" }));
    root.appendChild(svg);
  }

  CT.registerTab("graph", "调用关系图", render);
})();
```

- [ ] **Step 2: 加断言并测试**

```cpp
  CHECK(h.find("CT.registerTab(\"graph\"") != std::string::npos);
```
Run: `cmake --build build -j && ctest --test-dir build -R test_html --output-on-failure` → PASS。

- [ ] **Step 3: 提交**

```bash
git add viewer/graph.js tests/test_html.cpp
git commit -m "feat(viewer): 调用关系图视图"
```

---

## Task 5: 单次追踪 Tab（实例列表 + 瀑布图）

**Files:**
- Modify: `viewer/trace.js`
- Modify: `viewer/core.js`（暴露 `CT.instanceTotal` 等）
- Modify: `tests/test_html.cpp`

- [ ] **Step 1: 实现 `viewer/trace.js`**

```js
(function () {
  const CT = window.CT;
  let selected = null;

  function rootsByThread() {
    const rows = [];
    for (const th of CT.data.threads) {
      for (const id of th.roots) {
        const i = CT.inst.get(id);
        if (i) rows.push(i);
      }
    }
    return rows;
  }

  function renderList(root, list, onPick) {
    CT.clear(list);
    for (const i of list) {
      const dur = i.end_ns - i.start_ns;
      const item = CT.el("div", { class: "item" + (selected && selected.id === i.id ? " sel" : "") }, [
        CT.el("span", { text: CT.name(i.fn) }),
        CT.el("span", { class: "muted", text: CT.fmtNs(dur) }),
      ]);
      item.onclick = function () { onPick(i); };
      list.appendChild(item);
    }
  }

  function renderWaterfall(host, inst) {
    CT.clear(host);
    const frames = [];
    (function walk(id, depth) {
      const n = CT.inst.get(id);
      if (!n) return;
      frames.push({ n: n, depth: depth });
      for (const c of n.children) walk(c, depth + 1);
    })(inst.id, 0);
    const base = inst.start_ns;
    const span = Math.max(1, inst.end_ns - inst.start_ns);
    const maxDepth = frames.reduce(function (m, f) { return Math.max(m, f.depth); }, 0);
    host.appendChild(CT.el("p", { class: "muted", text: CT.name(inst.fn) + " · 深度 " + maxDepth + " · 时长 " + CT.fmtNs(span) + " · tid " + inst.tid }));
    const rowH = 16;
    const track = Math.max(200, host.clientWidth - 250);
    for (const f of frames) {
      const left = ((f.n.start_ns - base) / span) * track;
      const width = Math.max(1, ((f.n.end_ns - f.n.start_ns) / span) * track);
      const row = CT.el("div", { class: "row" });
      const lbl = CT.el("div", { class: "lbl", title: CT.name(f.n.fn) });
      for (let d = 0; d < f.depth; d++) lbl.appendChild(CT.el("span", { text: "  " }));
      lbl.appendChild(document.createTextNode(CT.name(f.n.fn)));
      const tr = CT.el("div", { class: "track", style: "width:" + track + "px" });
      const fill = CT.el("div", { class: "fill" });
      fill.style.left = left + "px";
      fill.style.width = width + "px";
      fill.style.background = f.depth === 0 ? "var(--hot)" : f.depth === 1 ? "var(--warm)" : f.depth === 2 ? "var(--cool)" : "var(--cold)";
      fill.title = CT.name(f.n.fn) + " · " + CT.fmtNs(f.n.end_ns - f.n.start_ns) + " · 自身 " + CT.fmtNs(f.n.self_ns);
      tr.appendChild(fill);
      row.appendChild(lbl);
      row.appendChild(tr);
      host.appendChild(row);
    }
  }

  function render(root) {
    CT.clear(root);
    const split = CT.el("div", { class: "split" });
    const left = CT.el("div");
    const search = CT.el("input", { type: "search", placeholder: "按函数名过滤调用实例…" });
    left.appendChild(search);
    const list = CT.el("div", { class: "list" });
    left.appendChild(list);
    const right = CT.el("div");
    split.appendChild(left);
    split.appendChild(right);
    root.appendChild(split);

    const all = rootsByThread().sort(function (a, b) { return (b.end_ns - b.start_ns) - (a.end_ns - a.start_ns); });
    function refresh() {
      const q = search.value.trim().toLowerCase();
      const list2 = q ? all.filter(function (i) { return CT.name(i.fn).toLowerCase().indexOf(q) >= 0; }) : all;
      renderList(root, list2, function (i) { selected = i; refresh(); renderWaterfall(right, i); });
    }
    search.oninput = refresh;
    refresh();
    if (all.length) { selected = all[0]; renderList(root, all, function (i) { selected = i; refresh(); renderWaterfall(right, i); }); renderWaterfall(right, all[0]); }
  }

  CT.registerTab("trace", "单次追踪", render);
})();
```

- [ ] **Step 2: 加断言并测试**

```cpp
  CHECK(h.find("CT.registerTab(\"trace\"") != std::string::npos);
```
Run: `cmake --build build -j && ctest --test-dir build -R test_html --output-on-failure` → PASS。

- [ ] **Step 3: 提交**

```bash
git add viewer/trace.js tests/test_html.cpp
git commit -m "feat(viewer): 单次追踪视图（实例列表+瀑布图）"
```

---

## Task 6: 调用者/被调用者 Tab

**Files:**
- Modify: `viewer/callers.js`
- Modify: `tests/test_html.cpp`

- [ ] **Step 1: 实现 `viewer/callers.js`**

```js
(function () {
  const CT = window.CT;
  let current = null;

  function edgeTable(edges, otherKey, title, root) {
    const table = CT.el("table");
    const head = CT.el("tr", null, [
      CT.el("th", { text: title }),
      CT.el("th", { class: "num", text: "次数" }),
      CT.el("th", { class: "num", text: "总耗时" }),
      CT.el("th", { text: "递归" }),
    ]);
    const tbody = CT.el("tbody");
    edges.sort(function (a, b) { return b.total_ns - a.total_ns; });
    for (const e of edges) {
      const other = e[otherKey];
      const tr = CT.el("tr");
      const nameCell = CT.el("td");
      const link = CT.el("a", { href: "#", text: CT.name(other) });
      link.onclick = function (ev) { ev.preventDefault(); select(other, root); };
      nameCell.appendChild(link);
      tr.appendChild(nameCell);
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtCount(e.count) }));
      tr.appendChild(CT.el("td", { class: "num", text: CT.fmtNs(e.total_ns) }));
      tr.appendChild(CT.el("td", { text: e.recursive ? "是" : "" }));
      tbody.appendChild(tr);
    }
    table.appendChild(CT.el("thead", null, head));
    table.appendChild(tbody);
    return table;
  }

  function select(fnId, root) {
    current = fnId;
    const input = document.getElementById("caller-search");
    if (input) input.value = CT.name(fnId);
    const host = document.getElementById("callers-body");
    CT.clear(host);
    const f = CT.fn.get(fnId);
    if (f) {
      host.appendChild(CT.el("div", { class: "grid" }, [
        CT.el("div", { class: "kpi" }, [CT.el("div", { class: "v", text: CT.fmtCount(f.calls) }), CT.el("div", { class: "k", text: "调用次数" })]),
        CT.el("div", { class: "kpi" }, [CT.el("div", { class: "v", text: CT.fmtNs(f.self_ns) }), CT.el("div", { class: "k", text: "自身耗时" })]),
        CT.el("div", { class: "kpi" }, [CT.el("div", { class: "v", text: CT.fmtNs(f.total_ns) }), CT.el("div", { class: "k", text: "总耗时" })]),
        CT.el("div", { class: "kpi" }, [CT.el("div", { class: "v", text: CT.fmtNs(f.max_ns) }), CT.el("div", { class: "k", text: "最慢一次" })]),
      ]));
    }
    const inc = CT.inEdges.get(fnId) || [];
    const out = CT.outEdges.get(fnId) || [];
    host.appendChild(CT.el("h3", { text: "调用者（谁调用了它）" }));
    host.appendChild(inc.length ? edgeTable(inc.slice(), "caller", "调用者", root) : CT.el("p", { class: "muted", text: "无（可能是线程入口）" }));
    host.appendChild(CT.el("h3", { text: "被调用者（它调用了谁）" }));
    host.appendChild(out.length ? edgeTable(out.slice(), "callee", "被调用者", root) : CT.el("p", { class: "muted", text: "无（叶子函数）" }));
  }

  function render(root) {
    CT.clear(root);
    const box = CT.el("div");
    const input = CT.el("input", { type: "search", id: "caller-search", placeholder: "搜索函数名…" });
    const sugg = CT.el("div", { class: "list", style: "max-height:240px;margin-top:8px" });
    const body = CT.el("div", { id: "callers-body", style: "margin-top:12px" });
    box.appendChild(input);
    box.appendChild(sugg);
    box.appendChild(body);
    root.appendChild(box);

    function update(q) {
      CT.clear(sugg);
      const s = q.trim().toLowerCase();
      if (!s) return;
      const hits = CT.data.functions.filter(function (f) { return f.calls > 0 && f.name.toLowerCase().indexOf(s) >= 0; }).slice(0, 30);
      for (const f of hits) {
        const item = CT.el("div", { class: "item" }, [CT.el("span", { text: f.name }), CT.el("span", { class: "muted", text: CT.fmtNs(f.total_ns) })]);
        item.onclick = function () { CT.clear(sugg); select(f.id, root); };
        sugg.appendChild(item);
      }
    }
    input.oninput = function () { update(input.value); };

    if (CT.pendingFn != null) { select(CT.pendingFn, root); CT.pendingFn = null; }
    else { select(0, root); input.value = CT.name(0); }
  }

  CT.registerTab("callers", "调用者/被调用者", render);
})();
```

- [ ] **Step 2: 加断言并测试**

```cpp
  CHECK(h.find("CT.registerTab(\"callers\"") != std::string::npos);
```
Run: `cmake --build build -j && ctest --test-dir build -R test_html --output-on-failure` → PASS。

- [ ] **Step 3: 提交**

```bash
git add viewer/callers.js tests/test_html.cpp
git commit -m "feat(viewer): 调用者/被调用者视图"
```

---

## Task 7: CLI `-o report.html` 与端到端集成

**Files:**
- Modify: `tools/ctiming-analyze.cpp`
- Create: `tests/integration_report.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: CLI 增加 `-o FILE` / `--html FILE`**

要求：新增变量 `html_path`；解析 `-o`/`--html`（带值，值不能以 `-` 开头，否则 missing value + usage，返回 2）。行为：
- 若给了 `--html`：写 `to_html(to_json(r, trace))` 到该文件（写入用与 `--json` 相同的字节数/fclose 校验，失败返回 1，成功打印 `wrote FILE`）。
- 若同时给了 `--json` 与 `--html`：两者都写。
- 若都没给：打印 `render_text()` 到 stdout。
- 更新 usage 行包含 `[-o FILE|--html FILE]`。
- 在 `-o`/`--html` 写文件后，不再打印文本摘要。

（在现有 `--json` 分支旁对称实现；`to_html` 来自 `#include "html.hpp"`。）

- [ ] **Step 2: 写 `tests/integration_report.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
EXE="$1"
ANALYZE="$2"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
CTIMING_OUT="$WORK/out.ctrace" "$EXE" >/dev/null
"$ANALYZE" "$WORK/out.ctrace" -o "$WORK/report.html" >/dev/null
test -s "$WORK/report.html"
grep -q '<!DOCTYPE html>' "$WORK/report.html"
grep -q 'id="ct-data"' "$WORK/report.html"
grep -q 'CT.registerTab' "$WORK/report.html"
if command -v python3 >/dev/null 2>&1; then
  python3 - "$WORK/report.html" <<'PY'
import json, re, sys
html = open(sys.argv[1], encoding="utf-8").read()
m = re.search(r'<script id="ct-data" type="application/json">(.*?)</script>', html, re.S)
assert m, "data script not found"
data = json.loads(m.group(1))
assert "functions" in data and "instances" in data and "aggregated" in data
assert any(f.get("name") == "leaf" for f in data["functions"])
PY
fi
```

Make executable. (Note: because the JSON is embedded with `</` escaped to `<\/`, `json.loads` still parses it — `\/` is a valid JSON escape.)

- [ ] **Step 3: `CMakeLists.txt` 注册集成测试**

```cmake
add_test(
  NAME integration_report
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/integration_report.sh
          $<TARGET_FILE:example_single> $<TARGET_FILE:ctiming-analyze>)
```

- [ ] **Step 4: 构建并运行全部测试**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` → all tests pass (15), no warnings.

- [ ] **Step 5: 手工生成并查看**

Run: `./build/example_single && ./build/ctiming-analyze example_single.ctrace -o report.html && ls -l report.html`
Expected: 生成自包含 `report.html`；用浏览器打开应显示五个 Tab 与数据（人工确认）。

- [ ] **Step 6: 提交**

```bash
git add tools/ctiming-analyze.cpp tests/integration_report.sh CMakeLists.txt
git commit -m "feat(viewer): ctiming-analyze -o report.html 与端到端集成"
```

---

## Task 8: 文档

**Files:**
- Create: `docs/viewer.md`
- Modify: `README.md`

- [ ] **Step 1: 写 `docs/viewer.md`（简体中文）**

内容：如何生成（`ctiming-analyze app.ctrace -o report.html`）、自包含/离线说明、五个 Tab（概览/火焰图/调用关系图/单次追踪/调用者被调用者）各自能看什么与操作（下钻、搜索、点击跳转）；数据来源 `analysis.json` 的说明与链接到 `docs/analyzer.md`；已知限制（实例全量内嵌，超大 trace 的 HTML 体积会较大）。

- [ ] **Step 2: 更新 `README.md`**

在分析器小节后补“生成 HTML 报告”：`./build/ctiming-analyze app.ctrace -o report.html`；路线图把计划 3 标为已完成。

- [ ] **Step 3: 全量测试**

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` → 全 PASS。

- [ ] **Step 4: 提交**

```bash
git add docs/viewer.md README.md
git commit -m "docs(viewer): HTML 报告使用说明"
```

---

## 自审记录

- **规格覆盖（§8）：** 概览/Top 热点（Task 2）、火焰图+反向（Task 3）、调用关系图（Task 4）、单次追踪瀑布图（Task 5）、调用者/被调用者（Task 6）、全局搜索（Task 6 搜索 + 概览过滤）、时间单位自适应（`CT.fmtNs`，Task 1）、自包含离线（Task 1/7）。
- **占位符扫描：** Task 1 Step 7 明确要求先放占位查看器脚本、后续 Task 覆盖——这是分步实现的必要环节，非“待办占位”；其余无 TBD。
- **类型/接口一致性：** `CT.registerTab(id,label,render)`、`CT.openCallers(fnId)`、`CT.pendingFn` 在 core/overview/callers 间一致；`viewer_assets.hpp` 变量名规则为 `css_<name>`/`js_<name>`（`viewer.css`→`css_viewer`，`core.js`→`js_core` 等），与 `html.cpp` 引用一致。
- **已知取舍：** 火焰图“自底向上”为简化实现（仍按聚合树下钻展示，附提示）；调用图仅显示总耗时前 40 个函数；实例全量内嵌使大 trace 的 HTML 偏大——均写入 `docs/viewer.md`。

---

## 执行交接

计划已保存到 `docs/superpowers/plans/2026-09-30-ctiming-viewer.md`。两种执行方式：

1. **子代理驱动（推荐）**：每个 Task 派发全新子代理，Task 间两阶段评审。
2. **当前会话内执行**：用 executing-plans 分批执行，带检查点。

选哪种？
