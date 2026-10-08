(function () {
  const CT = window.CT;
  const data = JSON.parse(document.getElementById("ct-data").textContent);
  CT.init(data);
  const meta = document.getElementById("meta");
  if (meta) {
    meta.textContent =
      data.trace.exe + " · threads " + data.trace.threads +
      " · events " + data.trace.total_events +
      (data.trace.dropped ? " · dropped " + data.trace.dropped : "");
  }
  const nav = document.getElementById("nav");
  CT.tabs.forEach(function (t, i) {
    nav.appendChild(
      CT.el("button", { type: "button", text: t.label, onclick: function () { CT.showTab(i); } })
    );
  });
  setupGlobalSearch();
  if (CT.tabs.length) CT.showTab(0);

  function setupGlobalSearch() {
    const header = document.querySelector("header");
    if (!header) return;
    const box = CT.el("div", { class: "globalsearch" });
    const input = CT.el("input", { type: "search", id: "global-search", placeholder: "全局搜索函数…" });
    const list = CT.el("div", { class: "list gs-list" });
    let hits = [];
    function hide() {
      CT.clear(list);
      list.style.display = "none";
      hits = [];
    }
    function pick(f) {
      hide();
      CT.openCallers(f.id);
    }
    function update() {
      const q = input.value.trim().toLowerCase();
      CT.clear(list);
      if (!q) { list.style.display = "none"; hits = []; return; }
      hits = CT.data.functions.filter(function (f) {
        return f.calls > 0 && f.name.toLowerCase().indexOf(q) >= 0;
      }).slice(0, 30);
      for (const f of hits) {
        const item = CT.el("div", { class: "item" }, [
          CT.el("span", { text: f.name }),
          CT.el("span", { class: "muted", text: CT.fmtNs(f.total_ns) }),
        ]);
        item.onclick = (function (fn) {
          return function (ev) {
            if (ev && ev.stopPropagation) ev.stopPropagation();
            pick(fn);
          };
        })(f);
        list.appendChild(item);
      }
      list.style.display = hits.length ? "block" : "none";
    }
    input.oninput = update;
    input.onclick = function (ev) { if (ev && ev.stopPropagation) ev.stopPropagation(); };
    input.onkeydown = function (e) {
      if (!e) return;
      if (e.key === "Enter" && hits.length) pick(hits[0]);
      else if (e.key === "Escape") { input.value = ""; hide(); }
    };
    if (document.addEventListener) document.addEventListener("click", hide);
    box.appendChild(input);
    box.appendChild(list);
    header.appendChild(box);
  }
})();
