(function () {
  const CT = window.CT;
  let selected = null;

  function rootInstances() {
    const rows = [];
    for (const th of CT.data.threads) {
      for (const id of th.roots) {
        const i = CT.inst.get(id);
        if (i) rows.push(i);
      }
    }
    return rows.sort(function (a, b) { return (b.end_ns - b.start_ns) - (a.end_ns - a.start_ns); });
  }

  function renderList(listEl, items, onPick) {
    CT.clear(listEl);
    for (const i of items) {
      const dur = i.end_ns - i.start_ns;
      const item = CT.el("div", { class: "item" + (selected && selected.id === i.id ? " sel" : "") }, [
        CT.el("span", { text: CT.name(i.fn) }),
        CT.el("span", { class: "muted", text: CT.fmtNs(dur) }),
      ]);
      item.onclick = function () { onPick(i); };
      listEl.appendChild(item);
    }
  }

  function renderWaterfall(host, inst) {
    CT.clear(host);
    const frames = [];
    const stack = [{ id: inst.id, depth: 0 }];
    while (stack.length) {
      const cur = stack.pop();
      const n = CT.inst.get(cur.id);
      if (!n) continue;
      frames.push({ n: n, depth: cur.depth });
      for (let i = n.children.length - 1; i >= 0; i--) stack.push({ id: n.children[i], depth: cur.depth + 1 });
    }
    frames.sort(function (a, b) { return a.depth - b.depth || a.n.start_ns - b.n.start_ns; });
    const base = inst.start_ns;
    const span = Math.max(1, inst.end_ns - inst.start_ns);
    host.appendChild(CT.el("p", { class: "muted", text: CT.name(inst.fn) + " · 时长 " + CT.fmtNs(span) + " · tid " + inst.tid + " · 直接子调用 " + inst.children.length }));
    for (const f of frames) {
      const left = ((f.n.start_ns - base) / span) * 100;
      const width = ((f.n.end_ns - f.n.start_ns) / span) * 100;
      const row = CT.el("div", { class: "row" });
      const lbl = CT.el("div", { class: "lbl", title: CT.name(f.n.fn) });
      for (let d = 0; d < f.depth; d++) lbl.appendChild(document.createTextNode("  "));
      lbl.appendChild(document.createTextNode(CT.name(f.n.fn)));
      const tr = CT.el("div", { class: "track" });
      const fill = CT.el("div", { class: "fill" });
      fill.style.left = left + "%";
      fill.style.width = width + "%";
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
    const search = CT.el("input", { type: "search", placeholder: "按函数名过滤顶层调用…" });
    const list = CT.el("div", { class: "list" });
    left.appendChild(search);
    left.appendChild(list);
    const right = CT.el("div");
    split.appendChild(left);
    split.appendChild(right);
    root.appendChild(split);

    const all = rootInstances();
    function pick(i) { selected = i; refresh(); }
    function refresh() {
      const q = search.value.trim().toLowerCase();
      const items = q ? all.filter(function (i) { return CT.name(i.fn).toLowerCase().indexOf(q) >= 0; }) : all;
      if (selected && items.indexOf(selected) < 0) selected = items.length ? items[0] : null;
      renderList(list, items, pick);
      CT.clear(right);
      if (selected) renderWaterfall(right, selected);
      else right.appendChild(CT.el("p", { class: "muted", text: "无匹配的顶层调用。" }));
    }
    search.oninput = refresh;
    if (all.length) selected = all[0];
    refresh();
  }

  CT.registerTab("trace", "单次追踪", render);
})();
