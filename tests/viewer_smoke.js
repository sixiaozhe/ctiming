"use strict";

const fs = require("fs");
const vm = require("vm");

const reportPath = process.argv[2];
if (!reportPath) {
  console.error("usage: node viewer_smoke.js <report.html>");
  process.exit(2);
}

let failures = 0;
function check(cond, msg) {
  if (!cond) {
    failures++;
    console.error("FAIL: " + msg);
  }
}

function makeText(text) {
  return { nodeType: 3, textContent: String(text), parentNode: null };
}

function makeElement(tag) {
  const classSet = new Set();
  const upper = String(tag).toUpperCase();
  const el = {
    nodeType: 1,
    tagName: upper,
    _tag: upper,
    childNodes: [],
    _listeners: {},
    attrs: {},
    style: {},
    value: "",
    parentNode: null,
    _text: "",
    get firstChild() {
      return this.childNodes.length ? this.childNodes[0] : null;
    },
    get children() {
      return this.childNodes.filter(function (c) { return c && c.nodeType === 1; });
    },
    appendChild(c) {
      c.parentNode = this;
      this.childNodes.push(c);
      return c;
    },
    removeChild(c) {
      const i = this.childNodes.indexOf(c);
      if (i >= 0) this.childNodes.splice(i, 1);
      c.parentNode = null;
      return c;
    },
    setAttribute(k, v) {
      this.attrs[k] = String(v);
      if (k === "id") this.id = String(v);
    },
    getAttribute(k) {
      return Object.prototype.hasOwnProperty.call(this.attrs, k) ? this.attrs[k] : null;
    },
    addEventListener(type, fn) {
      (this._listeners[type] = this._listeners[type] || []).push(fn);
    },
    _fire(type, evt) {
      (this._listeners[type] || []).forEach(function (fn) { fn(evt || {}); });
    },
  };
  Object.defineProperty(el, "textContent", {
    get() {
      if (this.childNodes.length) {
        return this.childNodes.map(function (c) { return c.textContent; }).join("");
      }
      return this._text;
    },
    set(v) {
      this._text = String(v);
      this.childNodes = [];
    },
  });
  Object.defineProperty(el, "className", {
    get() {
      return Array.from(classSet).join(" ");
    },
    set(v) {
      classSet.clear();
      String(v).split(/\s+/).filter(Boolean).forEach(function (c) { classSet.add(c); });
    },
  });
  el.classList = {
    add() { for (const c of arguments) classSet.add(c); },
    remove() { for (const c of arguments) classSet.delete(c); },
    contains(c) { return classSet.has(c); },
    toggle(c, force) {
      const has = force === undefined ? !classSet.has(c) : !!force;
      if (has) classSet.add(c); else classSet.delete(c);
      return has;
    },
  };
  return el;
}

function walk(node, fn) {
  fn(node);
  for (const c of node.childNodes || []) walk(c, fn);
}

const body = makeElement("body");
const header = makeElement("header");
header.setAttribute("id", "header");
body.appendChild(header);
const view = makeElement("main");
view.setAttribute("id", "view");
const nav = makeElement("nav");
nav.setAttribute("id", "nav");
const meta = makeElement("span");
meta.setAttribute("id", "meta");
const dataEl = makeElement("script");
dataEl.setAttribute("id", "ct-data");
dataEl.setAttribute("type", "application/json");
body.appendChild(meta);
body.appendChild(nav);
body.appendChild(view);
body.appendChild(dataEl);

const document = {
  body: body,
  _listeners: {},
  addEventListener: function (type, fn) {
    (this._listeners[type] = this._listeners[type] || []).push(fn);
  },
  _fire: function (type, evt) {
    (this._listeners[type] || []).forEach(function (fn) { fn(evt || {}); });
  },
  createElement: function (tag) { return makeElement(tag); },
  createElementNS: function (ns, tag) { return makeElement(tag); },
  createTextNode: function (text) { return makeText(text); },
  getElementById: function (id) {
    let found = null;
    walk(body, function (n) {
      if (!found && n.nodeType === 1 && n.attrs && n.attrs.id === id) found = n;
    });
    return found;
  },
  querySelectorAll: function (sel) {
    const tag = String(sel).trim().split(/\s+/).pop().toUpperCase();
    const out = [];
    walk(body, function (n) {
      if (n.nodeType === 1 && n.tagName === tag) out.push(n);
    });
    return out;
  },
  querySelector: function (sel) {
    const all = document.querySelectorAll(sel);
    return all.length ? all[0] : null;
  },
};

function collect(tag) {
  return collectIn(view, tag);
}

function collectIn(node, tag) {
  const out = [];
  walk(node, function (n) {
    if (n.nodeType === 1 && n.tagName === String(tag).toUpperCase()) out.push(n);
  });
  return out;
}

function collectByClass(cls) {
  return collectClassIn(view, cls);
}

function collectClassIn(node, cls) {
  const out = [];
  walk(node, function (n) {
    if (n.nodeType === 1 && n.classList && n.classList.contains(cls)) out.push(n);
  });
  return out;
}

let html;
try {
  html = fs.readFileSync(reportPath, "utf8");
} catch (e) {
  console.error("cannot read " + reportPath + ": " + e.message);
  process.exit(1);
}

const scriptRe = /<script([^>]*)>([\s\S]*?)<\/script>/g;
const blocks = [];
let rawData = null;
let m;
while ((m = scriptRe.exec(html)) !== null) {
  const attrs = m[1] || "";
  if (/id="ct-data"/.test(attrs) || /application\/json/.test(attrs)) {
    rawData = m[2];
    continue;
  }
  blocks.push(m[2]);
}

check(rawData != null, "data script not found in report");
check(blocks.length >= 5, "expected viewer script blocks, got " + blocks.length);
if (rawData == null) {
  console.error("cannot continue without embedded data");
  process.exit(1);
}

let data = null;
try {
  data = JSON.parse(rawData);
} catch (e) {
  check(false, "embedded data is not valid JSON: " + e.message);
}

dataEl.textContent = rawData;

const rafQueue = [];
let rafTime = 0;
const sandbox = {
  console: console,
  document: document,
  window: {},
  performance: { now: function () { return rafTime; } },
  requestAnimationFrame: function (cb) { rafQueue.push(cb); return rafQueue.length; },
};
function flushAnim() {
  let guard = 0;
  rafTime += 100;
  while (rafQueue.length && guard++ < 200) {
    const cbs = rafQueue.splice(0);
    rafTime += 100;
    for (const cb of cbs) cb(rafTime);
  }
}
vm.createContext(sandbox);
for (let i = 0; i < blocks.length; i++) {
  try {
    vm.runInContext(blocks[i], sandbox, { filename: "inline-" + i + ".js" });
  } catch (e) {
    failures++;
    console.error("FAIL: inline script " + i + " threw: " + (e && e.stack ? e.stack : e));
  }
}

const CT = sandbox.window.CT;
check(CT && typeof CT.init === "function", "CT.init is defined by report scripts");
if (!CT || typeof CT.init !== "function") {
  console.error("viewer did not initialize from " + reportPath);
  process.exit(1);
}

const selfInit = !!CT.data;
check(selfInit, "report did not self-initialize (CT.init not called; missing main script?)");
if (!selfInit && data) CT.init(data);

check(CT.tabs.length >= 5, "expected >=5 tabs, got " + CT.tabs.length);

for (let i = 0; i < CT.tabs.length; i++) {
  try {
    CT.showTab(i);
  } catch (e) {
    failures++;
    console.error("FAIL: tab " + CT.tabs[i].id + " threw: " + (e && e.stack ? e.stack : e));
  }
}

function showTabById(id) {
  for (let i = 0; i < CT.tabs.length; i++) {
    if (CT.tabs[i].id === id) { CT.showTab(i); return true; }
  }
  return false;
}

check(showTabById("flame"), "flame tab registered");
function flameRects() {
  return collect("rect").filter(function (r) {
    return !r.parentNode || r.parentNode.tagName !== "CLIPPATH";
  });
}
function flameCrumb() {
  const c = collectByClass("crumb");
  return c.length ? c[c.length - 1].textContent : "";
}
function flameActive() {
  return CT.tabs[CT.state.active] && CT.tabs[CT.state.active].id === "flame";
}
const rects = flameRects();
check(rects.length >= 3, "expected >=3 flame frames, got " + rects.length);
let drilled = false;
if (rects.length >= 2) {
  const rootNames = CT.data.aggregated.map(function (r) { return CT.name(r.fn); });
  let t1 = "";
  let rootName = null;
  for (let i = 0; i < rects.length && rootName == null; i++) {
    const rs = flameRects();
    if (i >= rs.length) break;
    rs[i]._fire("click");
    if (flameActive()) {
      const t = flameCrumb();
      const nm = rootNames.filter(function (n) { return t.indexOf(n) >= 0; })[0];
      if (nm != null) { t1 = t; rootName = nm; }
      else showTabById("flame");
    } else {
      showTabById("flame");
    }
  }
  check(rootName != null, "could not find a drillable flame frame");
  if (rootName != null) {
    const rects2 = flameRects();
    if (rects2.length >= 2) {
      rects2[1]._fire("click");
      if (flameActive()) {
        const t2 = flameCrumb();
        check(t2.indexOf(rootName) >= 0, "breadcrumb lost ancestor after child drill: " + t2);
        check(t2.length > t1.length, "breadcrumb did not extend after child drill: " + t2);
      } else {
        showTabById("flame");
        const t2 = flameCrumb();
        check(t2.indexOf(rootName) >= 0, "breadcrumb lost root after leaf child navigation: " + t2);
      }
      drilled = true;
    }
  }
}
if (!drilled) check(flameRects().length >= 1, "expected at least one flame frame");
flushAnim();
const revealed = collect("g").filter(function (n) { return n.getAttribute && n.getAttribute("clip-path"); });
check(revealed.every(function (n) { return n.getAttribute("opacity") !== "0"; }),
  "flame labels must be revealed after animation");

const flameSvgs = collect("svg");
check(flameSvgs.length >= 1, "flame svg missing");
if (flameSvgs.length) {
  const svg = flameSvgs[flameSvgs.length - 1];
  const before = svg.getAttribute("viewBox");
  svg._fire("wheel", { deltaY: -1, clientX: 0, preventDefault: function () {} });
  const current = collect("svg");
  const afterSvg = current[current.length - 1];
  const after = afterSvg ? afterSvg.getAttribute("viewBox") : null;
  check(typeof before === "string" && typeof after === "string" && before !== after,
    "flame wheel zoom did not change viewBox (" + before + " -> " + after + ")");
  check(afterSvg && afterSvg.getAttribute("preserveAspectRatio") === "none",
    "flame svg must use preserveAspectRatio=none for time-axis zoom");
  const flameTexts = collectIn(afterSvg, "text");
  if (flameTexts.length) {
    const clipped = flameTexts.filter(function (t) {
      return t.parentNode && t.parentNode.getAttribute && t.parentNode.getAttribute("clip-path");
    });
    check(clipped.length === flameTexts.length,
      "flame labels must be clipped to their frame (" + clipped.length + "/" + flameTexts.length + ")");
  }
}

check(showTabById("trace"), "trace tab registered");
const traceSearch = document.getElementById("trace-search");
check(traceSearch != null, "trace search input missing");
function traceItems() {
  const list = document.getElementById("trace-list");
  return list ? collectClassIn(list, "item") : [];
}
check(traceItems().length >= 1, "expected root trace items on first render");
check(collectByClass("fill").length >= 1, "expected trace bars on first render");
if (traceSearch) {
  traceSearch.value = "zzz-no-such-function-name";
  if (typeof traceSearch.oninput === "function") traceSearch.oninput();
  check(traceItems().length === 0, "expected no trace items after no-match filter");
  check(collectByClass("fill").length === 0, "expected no trace bars after no-match filter");
  traceSearch.value = "";
  if (typeof traceSearch.oninput === "function") traceSearch.oninput();
}
const fills = collectByClass("fill");
check(fills.length >= 1, "waterfall did not recover after clearing the filter");
for (const f of fills) {
  check(typeof f.style.left === "string" && f.style.left.endsWith("%"), "trace bar left not %-based: " + f.style.left);
  check(typeof f.style.width === "string" && f.style.width.endsWith("%"), "trace bar width not %-based: " + f.style.width);
}

const rootsCount = traceItems().length;
const allBtn = document.getElementById("trace-mode-all");
const rootsBtn = document.getElementById("trace-mode-roots");
check(allBtn != null && rootsBtn != null, "trace mode buttons missing");
if (allBtn && rootsBtn) {
  allBtn.onclick();
  const allCount = traceItems().length;
  check(allCount >= rootsCount, "全部调用 should list at least as many items as 顶层调用");
  if (CT.data.instances.length > rootsCount) {
    const cap = CT.TRACE_MAX_ROWS || Infinity;
    check(allCount === Math.min(CT.data.instances.length, cap), "全部调用 should list every instance up to the cap (" + allCount + " vs " + Math.min(CT.data.instances.length, cap) + ")");
  }
  rootsBtn.onclick();
  check(traceItems().length === rootsCount, "顶层调用 should restore the root list");
  allBtn.onclick();
}

const threadSel = document.getElementById("trace-thread");
check(threadSel != null, "trace thread select missing");
if (threadSel && CT.data.threads.length) {
  const tid = CT.data.threads[0].tid;
  threadSel.value = String(tid);
  threadSel.onchange();
  const expected = CT.data.instances.filter(function (i) { return i.tid === tid; }).length;
  const cap = CT.TRACE_MAX_ROWS || Infinity;
  check(traceItems().length === Math.min(expected, cap), "thread filter mismatch: " + traceItems().length + " vs " + Math.min(expected, cap));
  threadSel.value = "all";
  threadSel.onchange();
  check(traceItems().length === Math.min(CT.data.instances.length, cap), "全部线程 should restore every instance up to the cap");
}

const sortSel = document.getElementById("trace-sort");
check(sortSel != null, "trace sort select missing");
if (sortSel) {
  sortSel.value = "name";
  sortSel.onchange();
  const names = traceItems().map(function (it) { return it.childNodes[0].textContent; });
  let sortedByName = true;
  for (let i = 1; i < names.length; i++) {
    if (names[i - 1].localeCompare(names[i]) > 0) { sortedByName = false; break; }
  }
  check(sortedByName, "trace list not sorted by 按函数名");
  sortSel.value = "dur";
  sortSel.onchange();
}

const pickable = traceItems();
check(pickable.length >= 1, "expected trace items before arbitrary selection");
if (pickable.length) {
  const arbitrary = pickable[pickable.length - 1];
  arbitrary.onclick();
  check(collectByClass("fill").length >= 1, "waterfall did not render for arbitrary selected instance");
}

const gsearch = document.getElementById("global-search");
check(gsearch != null, "global search input missing");
const searchable = CT.data.functions.filter(function (f) { return f.calls > 0; });
check(searchable.length >= 1, "no callable function available for global search");
if (gsearch && searchable.length) {
  const sample = searchable[0];
  const q = sample.name.slice(0, Math.max(1, Math.min(3, sample.name.length))).toLowerCase();
  gsearch.value = q;
  if (typeof gsearch.oninput === "function") gsearch.oninput();
  const sugg = collectClassIn(header, "item");
  check(sugg.length >= 1, "global search produced no suggestions for '" + q + "'");
  const activeBefore = CT.tabs[CT.state.active] && CT.tabs[CT.state.active].id;
  if (sugg.length) sugg[0].onclick();
  const activeAfter = CT.tabs[CT.state.active] && CT.tabs[CT.state.active].id;
  check(activeAfter === "callers", "global search did not open callers tab (was " + activeBefore + ")");
  const callersBody = document.getElementById("callers-body");
  check(callersBody != null && callersBody.childNodes.length >= 1, "callers tab did not render after global search");
}

const bigInstances = [];
for (let i = 0; i < 1200; i++) bigInstances.push({ id: i, fn: 0, tid: 1, depth: 0, parent: -1, start_ns: i, end_ns: i + 1, self_ns: 1, children: [] });
const bigData = {
  trace: { exe: "synthetic", pid: 1, flags: 0, modules: 1, symbols: 1, threads: 1, total_events: 2400, dropped: 0, unbalanced_enter: 0, orphan_exit: 0 },
  functions: [{ id: 0, name: "fn", module: 0, offset: 0, calls: 1200, total_ns: 1200, self_ns: 1200, min_ns: 1, max_ns: 1, kept: true }],
  call_graph: [],
  aggregated: [{ fn: 0, calls: 1200, total_ns: 1200, self_ns: 1200, children: [] }],
  threads: [{ tid: 1, roots: bigInstances.map(function (x) { return x.id; }) }],
  instances: bigInstances,
};
CT.init(bigData);
showTabById("trace");
const capAllBtn = document.getElementById("trace-mode-all");
if (capAllBtn) capAllBtn.onclick();
check(traceItems().length === (CT.TRACE_MAX_ROWS || Infinity), "trace list should cap at TRACE_MAX_ROWS for large data (" + traceItems().length + ")");

if (failures) {
  console.error(failures + " smoke check(s) failed");
  process.exit(1);
}
console.log("viewer smoke ok");
process.exit(0);
