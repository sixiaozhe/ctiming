(function () {
  const CT = window.CT;

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
    let maxTotal = 0;
    for (const f of funcs) if (f.total_ns > maxTotal) maxTotal = f.total_ns;

    CT.clear(root);
    root.appendChild(
      CT.el("div", { class: "grid" }, [
        kpi(CT.fmtNs(maxTotal), "最重函数总耗时"),
        kpi(CT.fmtCount(t.total_events), "事件数"),
        kpi(CT.fmtCount(t.threads), "线程数"),
        kpi(CT.fmtCount(funcs.length), "有调用的函数数"),
        kpi(t.dropped ? CT.fmtCount(t.dropped) : "0", "截断/丢弃"),
        kpi(t.unbalanced_enter ? CT.fmtCount(t.unbalanced_enter) : "0", "未配对 ENTER"),
      ])
    );

    const header = CT.el("tr");
    const cols = [
      { k: "name", label: "函数" },
      { k: "calls", label: "调用次数", num: true, sortable: true },
      { k: "self_ns", label: "自身耗时", num: true, sortable: true },
      { k: "total_ns", label: "总耗时", num: true, sortable: true },
      { k: "max_ns", label: "最慢一次", num: true },
      { k: "pct", label: "占比", num: true },
    ];
    for (const col of cols) {
      const th = CT.el("th", { text: col.label });
      if (col.num) th.classList.add("num");
      if (col.sortable) {
        th.style.cursor = "pointer";
        if (sortKey === col.k) th.appendChild(CT.el("span", { class: "muted", text: " ▾" }));
        th.onclick = function () { sortKey = col.k; render(root); };
      }
      header.appendChild(th);
    }
    const table = CT.el("table", null, [CT.el("thead", null, header)]);
    const tbody = CT.el("tbody");

    funcs.sort(function (a, b) { return b[sortKey] - a[sortKey]; });
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
      root.appendChild(CT.el("p", { class: "muted", text: "仅显示前 200 个函数，请用“调用者/被调用者”页搜索定位。" }));
    }
  }

  CT.registerTab("overview", "概览", render);
})();
