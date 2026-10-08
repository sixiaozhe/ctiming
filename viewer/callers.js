(function () {
  const CT = window.CT;

  function edgeTable(edges, otherKey, title) {
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
      link.onclick = function (ev) { ev.preventDefault(); select(other); };
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

  function select(fnId) {
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
    host.appendChild(inc.length ? edgeTable(inc.slice(), "caller", "调用者") : CT.el("p", { class: "muted", text: "无（可能是线程入口）" }));
    host.appendChild(CT.el("h3", { text: "被调用者（它调用了谁）" }));
    host.appendChild(out.length ? edgeTable(out.slice(), "callee", "被调用者") : CT.el("p", { class: "muted", text: "无（叶子函数）" }));
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
        item.onclick = function () { CT.clear(sugg); select(f.id); };
        sugg.appendChild(item);
      }
    }
    input.oninput = function () { update(input.value); };

    if (CT.pendingFn != null) { const p = CT.pendingFn; CT.pendingFn = null; select(p); }
    else select(0);
  }

  CT.registerTab("callers", "调用者/被调用者", render);
})();
