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
      c.addEventListener("click", function () { CT.hideTooltip(); CT.openCallers(f.id); });
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
