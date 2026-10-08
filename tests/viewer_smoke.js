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

const sandbox = { console: console, document: document, window: {} };
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
const rects = collect("rect");
check(rects.length >= 3, "expected >=3 flame frames, got " + rects.length);
const aggRoot = CT.data && CT.data.aggregated && CT.data.aggregated[0];
const drillId = aggRoot && aggRoot.children && aggRoot.children.length ? aggRoot.children[0].fn : null;
if (drillId !== null && rects.length >= 2) {
  rects[1]._fire("click");
  const crumbs = collectByClass("crumb");
  check(crumbs.length >= 1, "expected breadcrumb after drill-down");
  if (crumbs.length) {
    const text = crumbs[crumbs.length - 1].textContent;
    check(text.indexOf(CT.name(aggRoot.fn)) >= 0, "breadcrumb lost ancestor " + CT.name(aggRoot.fn) + ": " + text);
    check(text.indexOf(CT.name(drillId)) >= 0, "breadcrumb missing drilled " + CT.name(drillId) + ": " + text);
  }
} else {
  check(rects.length >= 1, "expected at least one flame frame");
}

check(showTabById("trace"), "trace tab registered");
const inputs = collect("input");
check(inputs.length >= 1, "trace search input missing");
const search = inputs[0];
check(collectByClass("fill").length >= 1, "expected trace bars on first render");
if (search) {
  search.value = "zzz-no-such-function-name";
  if (typeof search.oninput === "function") search.oninput();
  check(collectByClass("fill").length === 0, "expected no trace bars after no-match filter");
  search.value = "";
  if (typeof search.oninput === "function") search.oninput();
}
const fills = collectByClass("fill");
check(fills.length >= 1, "waterfall did not recover after clearing the filter");
for (const f of fills) {
  check(typeof f.style.left === "string" && f.style.left.endsWith("%"), "trace bar left not %-based: " + f.style.left);
  check(typeof f.style.width === "string" && f.style.width.endsWith("%"), "trace bar width not %-based: " + f.style.width);
}

if (failures) {
  console.error(failures + " smoke check(s) failed");
  process.exit(1);
}
console.log("viewer smoke ok");
process.exit(0);
