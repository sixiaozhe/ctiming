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
    const s = String(n);
    let out = "";
    for (let i = 0; i < s.length; i++) {
      if (i > 0 && (s.length - i) % 3 === 0) out += "\u202f";
      out += s[i];
    }
    return out;
  };

  CT.pct = function (a, b) {
    return b > 0 ? ((a / b) * 100).toFixed(1) + "%" : "0%";
  };

  CT.el = function (tag, attrs, kids) {
    const n = document.createElement(tag);
    if (attrs) {
      for (const k in attrs) {
        const v = attrs[k];
        if (k === "class") n.className = v;
        else if (k === "text") n.textContent = v;
        else if (k.indexOf("on") === 0 && typeof v === "function") n[k] = v;
        else n.setAttribute(k, v);
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
    if (i < 0 || i >= CT.tabs.length) return;
    CT.state.active = i;
    const root = document.getElementById("view");
    CT.clear(root);
    document.querySelectorAll("nav button").forEach((b, j) => {
      b.classList.toggle("active", j === i);
    });
    CT.tabs[i].render(root);
  };

  CT.openCallers = function (fnId) {
    for (let i = 0; i < CT.tabs.length; i++)
      if (CT.tabs[i].id === "callers") { CT.pendingFn = fnId; CT.showTab(i); return; }
    CT.pendingFn = null;
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
