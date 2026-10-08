(function () {
  const CT = window.CT;
  let rootNode = null;
  let path = [];
  let reversed = false;

  function rootsArr() { return rootNode ? [rootNode] : CT.data.aggregated; }

  function pathTo(target) {
    const starts = CT.data.aggregated;
    const stack = starts.map(function (r) { return { node: r, p: [r] }; });
    while (stack.length) {
      const cur = stack.pop();
      if (cur.node === target) return cur.p;
      const kids = cur.node.children || [];
      for (let i = kids.length - 1; i >= 0; i--) stack.push({ node: kids[i], p: cur.p.concat([kids[i]]) });
    }
    return [];
  }

  function flatten() {
    const frames = [];
    const roots0 = rootsArr();
    let total = 0;
    for (const n of roots0) total += n.total_ns;
    if (total <= 0) total = 1;
    const stack = [];
    let x = 0;
    for (const n of roots0) {
      const w = n.total_ns / total;
      stack.push({ node: n, depth: 0, x0: x, x1: x + w });
      x += w;
    }
    while (stack.length) {
      const f = stack.pop();
      frames.push(f);
      const kids = f.node.children || [];
      if (!kids.length) continue;
      const t = f.node.total_ns || 1;
      let cx = f.x0;
      const cf = [];
      for (const c of kids) {
        const w = (c.total_ns / t) * (f.x1 - f.x0);
        cf.push({ node: c, depth: f.depth + 1, x0: cx, x1: cx + w });
        cx += w;
      }
      for (let i = cf.length - 1; i >= 0; i--) stack.push(cf[i]);
    }
    return { frames: frames, total: total };
  }

  function render(root) {
    CT.clear(root);
    const crumb = CT.el("p", { class: "crumb" });
    crumb.appendChild(CT.el("a", { text: "全部", onclick: function () { rootNode = null; path = []; render(root); } }));
    for (let i = 0; i < path.length; i++) {
      if (path[i] === rootNode) continue;
      (function (k) {
        crumb.appendChild(document.createTextNode(" / "));
        crumb.appendChild(CT.el("a", { text: CT.name(path[k].fn), onclick: function () { rootNode = path[k]; path = path.slice(0, k + 1); render(root); } }));
      })(i);
    }
    if (rootNode) { crumb.appendChild(document.createTextNode(" / ")); crumb.appendChild(CT.el("span", { text: CT.name(rootNode.fn) })); }
    crumb.appendChild(document.createTextNode("   "));
    crumb.appendChild(CT.el("button", { type: "button", text: reversed ? "自底向上 ✓" : "自底向上", onclick: function () { reversed = !reversed; render(root); } }));
    root.appendChild(crumb);

    const flat = flatten();
    const frames = flat.frames;
    const total = flat.total;
    let maxDepth = 0;
    for (const f of frames) if (f.depth > maxDepth) maxDepth = f.depth;
    const rowH = 22;
    const W = 1000;
    const H = Math.max(120, (maxDepth + 1) * rowH);
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    svg.setAttribute("height", String(H));
    for (const f of frames) {
      const x = f.x0 * W;
      const w = Math.max(1, (f.x1 - f.x0) * W);
      const y = (reversed ? maxDepth - f.depth : f.depth) * rowH;
      const rect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
      rect.setAttribute("x", String(x));
      rect.setAttribute("y", String(y));
      rect.setAttribute("width", String(w));
      rect.setAttribute("height", String(rowH - 2));
      rect.setAttribute("rx", "3");
      const frac = f.node.total_ns / total;
      rect.setAttribute("fill", frac > 0.5 ? "var(--hot)" : frac > 0.2 ? "var(--warm)" : frac > 0.05 ? "var(--cool)" : "var(--cold)");
      const tip = CT.name(f.node.fn) + " · 总 " + CT.fmtNs(f.node.total_ns) + " · 自身 " + CT.fmtNs(f.node.self_ns) + " · " + CT.fmtCount(f.node.calls) + " 次";
      rect.addEventListener("mousemove", function (e) { CT.tooltip(tip, e.clientX, e.clientY); });
      rect.addEventListener("mouseleave", CT.hideTooltip);
      rect.addEventListener("click", function () {
        CT.hideTooltip();
        if (f.node.children && f.node.children.length) { path = pathTo(f.node); rootNode = f.node; render(root); }
        else CT.openCallers(f.node.fn);
      });
      svg.appendChild(rect);
      if (w > 60) {
        const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
        label.setAttribute("x", String(x + 4));
        label.setAttribute("y", String(y + 15));
        label.setAttribute("font-size", "11");
        label.setAttribute("fill", "#0b101f");
        label.textContent = CT.name(f.node.fn);
        svg.appendChild(label);
      }
    }
    root.appendChild(svg);
    root.appendChild(CT.el("p", { class: "muted", text: "宽度=累计耗时；点击下钻，点击叶子查看调用者/被调用者。" }));
  }

  CT.registerTab("flame", "火焰图", render);
})();
