"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

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

const document = {
  body: body,
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
};

const view = makeElement("main");
view.setAttribute("id", "view");
body.appendChild(view);

function collect(tag) {
  const out = [];
  walk(view, function (n) {
    if (n.nodeType === 1 && n.tagName === String(tag).toUpperCase()) out.push(n);
  });
  return out;
}

function collectByClass(cls) {
  const out = [];
  walk(view, function (n) {
    if (n.nodeType === 1 && n.classList && n.classList.contains(cls)) out.push(n);
  });
  return out;
}

const viewerDir = path.join(__dirname, "..", "viewer");
const sources = ["core.js", "overview.js", "flame.js", "graph.js", "trace.js", "callers.js"];
const sandbox = { console: console, document: document, window: {} };
vm.createContext(sandbox);
for (const f of sources) {
  const code = fs.readFileSync(path.join(viewerDir, f), "utf8");
  vm.runInContext(code, sandbox, { filename: f });
}

const CT = sandbox.window.CT;
check(CT && typeof CT.init === "function", "CT.init is defined");
if (!CT || typeof CT.init !== "function") {
  console.error("cannot load viewer sources from " + viewerDir);
  process.exit(1);
}

const data = {
  trace: {
    exe: "smoke", pid: 1, flags: 0, modules: 1, symbols: 2, threads: 1,
    total_events: 4, dropped: 0, unbalanced_enter: 0, orphan_exit: 0,
  },
  functions: [
    { id: 0, name: "main", module: 0, offset: 0, calls: 2, total_ns: 100, self_ns: 30, min_ns: 40, max_ns: 60, kept: true },
    { id: 1, name: "leaf", module: 0, offset: 0, calls: 1, total_ns: 80, self_ns: 80, min_ns: 80, max_ns: 80, kept: true },
  ],
  call_graph: [
    { caller: 0, callee: 1, count: 1, total_ns: 80, recursive: false },
  ],
  aggregated: [
    {
      fn: 0, calls: 1, total_ns: 100, self_ns: 20,
      children: [
        {
          fn: 1, calls: 1, total_ns: 80, self_ns: 20,
          children: [
            { fn: 0, calls: 1, total_ns: 60, self_ns: 60, children: [] },
          ],
        },
      ],
    },
  ],
  threads: [{ tid: 1, roots: [0] }],
  instances: [
    { id: 0, fn: 0, tid: 1, depth: 0, parent: -1, start_ns: 0, end_ns: 100, self_ns: 20, children: [1] },
    { id: 1, fn: 1, tid: 1, depth: 1, parent: 0, start_ns: 5, end_ns: 80, self_ns: 60, children: [] },
  ],
};

CT.init(data);
check(CT.tabs.length === 5, "expected 5 tabs, got " + CT.tabs.length);

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
const rects = collect("rect");
check(rects.length === 3, "expected 3 flame frames, got " + rects.length);
if (rects.length >= 2) rects[1]._fire("click");
const crumbs = collectByClass("crumb");
check(crumbs.length === 1, "expected one breadcrumb, got " + crumbs.length);
if (crumbs.length) {
  const text = crumbs[0].textContent;
  check(text.indexOf("main") >= 0, "breadcrumb lost ancestor main: " + text);
  check(text.indexOf("leaf") >= 0, "breadcrumb missing current leaf: " + text);
}

check(showTabById("trace"), "trace tab registered");
const fills = collectByClass("fill");
check(fills.length >= 1, "expected trace bars, got " + fills.length);
for (const f of fills) {
  check(typeof f.style.left === "string" && f.style.left.endsWith("%"), "trace bar left not %: " + f.style.left);
  check(typeof f.style.width === "string" && f.style.width.endsWith("%"), "trace bar width not %: " + f.style.width);
}

if (failures) {
  console.error(failures + " smoke check(s) failed");
  process.exit(1);
}
console.log("viewer smoke ok");
process.exit(0);
