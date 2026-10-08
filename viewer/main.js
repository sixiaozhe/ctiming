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
  if (CT.tabs.length) CT.showTab(0);
})();
