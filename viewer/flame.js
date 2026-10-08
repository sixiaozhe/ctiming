(function () {
  const CT = window.CT;
  let rootNode = null;
  let path = [];
  let reversed = false;
  let zoom = 1;
  let panX = 0;
  let curSvg = null;
  let curH = 0;
  let curLabel = null;
  let drag = null;
  let suppressClick = false;
  const W = 1000;

  function clampPan() {
    const vw = W / zoom;
    if (panX < 0) panX = 0;
    if (panX > W - vw) panX = W - vw;
  }

  function applyViewBox() {
    if (!curSvg) return;
    clampPan();
    curSvg.setAttribute("viewBox", panX + " 0 " + (W / zoom) + " " + curH);
    if (curLabel) curLabel.textContent = "缩放 ×" + zoom.toFixed(1);
  }

  function onWheel(e) {
    if (e && e.preventDefault) e.preventDefault();
    const rect = curSvg && curSvg.getBoundingClientRect ? curSvg.getBoundingClientRect() : null;
    const vw = W / zoom;
    let frac = 0.5;
    if (rect && rect.width) frac = (e.clientX - rect.left) / rect.width;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const anchor = panX + frac * vw;
    const factor = e && e.deltaY < 0 ? 1.2 : 1 / 1.2;
    zoom = Math.max(1, Math.min(20, zoom * factor));
    panX = anchor - frac * (W / zoom);
    applyViewBox();
  }

  function onDown(e) {
    if (zoom <= 1) return;
    drag = { x: e.clientX, pan: panX, moved: false };
  }

  function onMove(e) {
    if (!drag) return;
    const rect = curSvg && curSvg.getBoundingClientRect ? curSvg.getBoundingClientRect() : null;
    if (!rect || !rect.width) return;
    const dx = e.clientX - drag.x;
    if (dx > 3 || dx < -3) drag.moved = true;
    panX = drag.pan - (dx / rect.width) * (W / zoom);
    applyViewBox();
  }

  function onUp() {
    suppressClick = !!(drag && drag.moved);
    drag = null;
  }

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
    crumb.appendChild(document.createTextNode(" "));
    crumb.appendChild(CT.el("button", { type: "button", text: "重置缩放", onclick: function () { zoom = 1; panX = 0; applyViewBox(); } }));
    curLabel = CT.el("span", { class: "muted", text: "缩放 ×1.0" });
    crumb.appendChild(document.createTextNode(" "));
    crumb.appendChild(curLabel);
    root.appendChild(crumb);

    const flat = flatten();
    const frames = flat.frames;
    const total = flat.total;
    let maxDepth = 0;
    for (const f of frames) if (f.depth > maxDepth) maxDepth = f.depth;
    const rowH = 22;
    const H = Math.max(120, (maxDepth + 1) * rowH);
    curH = H;
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    svg.setAttribute("height", String(H));
    svg.style.cursor = "grab";
    svg.addEventListener("wheel", onWheel, { passive: false });
    svg.addEventListener("mousedown", onDown);
    svg.addEventListener("mousemove", onMove);
    svg.addEventListener("mouseup", onUp);
    svg.addEventListener("mouseleave", onUp);
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
        if (suppressClick) { suppressClick = false; return; }
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
    curSvg = svg;
    applyViewBox();
    root.appendChild(CT.el("p", { class: "muted", text: "宽度=累计耗时；滚轮缩放（以光标为中心）、拖动平移、可重置；点击下钻，点击叶子查看调用者/被调用者。" }));
  }

  CT.registerTab("flame", "火焰图", render);
})();
