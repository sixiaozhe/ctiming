#include "check.h"
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"
#include "analysis.hpp"
#include <utility>

using namespace ct;
static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid; for (auto e : evs) t.events.push_back(e); return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  Trace tr;
  tr.pid = 1;
  tr.total_events = 6;
  tr.symbols.push_back(SymbolInfo{0, 0x1000, "main"});
  tr.symbols.push_back(SymbolInfo{0, 0x2000, "leaf"});
  tr.threads.push_back(mk(1, { ev(0,0,0), ev(0,1,10), ev(1,1,30), ev(0,1,40), ev(1,1,50), ev(1,0,60) }));

  Options opt;
  AnalysisResult r = analyze(tr, opt);
  CHECK_EQ_LONG((long)r.tree.instances.size(), 3);
  CHECK_EQ_LONG(r.agg.funcs[1].calls, 2);
  std::string js = to_json(r, tr);
  CHECK(js.find("\"main\"") != std::string::npos);
  CHECK(js.find("\"leaf\"") != std::string::npos);
  CHECK(js.find("\"call_graph\"") != std::string::npos);
  CHECK(js.find("\"instances\"") != std::string::npos);
  CHECK(js.find("\"unbalanced_enter\"") != std::string::npos);
  CHECK(js.find("\"orphan_exit\"") != std::string::npos);
  std::string txt = render_text(r, tr);
  CHECK(txt.find("leaf") != std::string::npos);
  CHECK(txt.find("total") != std::string::npos);

  Options inc; inc.include = "leaf"; inc.has_include = true;
  AnalysisResult r2 = analyze(tr, inc);
  std::string txt2 = render_text(r2, tr);
  CHECK(txt2.find("leaf") != std::string::npos);
  CHECK(txt2.find("main") == std::string::npos);
  std::string js2 = to_json(r2, tr);
  CHECK(js2.find("\"main\"") != std::string::npos);
  CHECK(js2.find("\"leaf\"") != std::string::npos);
  CHECK(js2.find("\"kept\":false") != std::string::npos);

  Options top; top.top = 1;
  std::string txt3 = render_text(analyze(tr, top), tr);
  CHECK(txt3.find("main") != std::string::npos);
  CHECK(txt3.find("leaf") == std::string::npos);

  const uint32_t kNest = 50000;
  ThreadEvents deep; deep.tid = 9;
  uint64_t ts = 0;
  for (uint32_t i = 0; i < kNest; i++) deep.events.push_back(ev(0, 0, ts++));
  for (uint32_t i = 0; i < kNest; i++) deep.events.push_back(ev(1, 0, ts++));
  Trace dtr;
  dtr.pid = 1;
  dtr.total_events = 2 * kNest;
  dtr.symbols.push_back(SymbolInfo{0, 0, "deep"});
  dtr.threads.push_back(std::move(deep));
  AnalysisResult rd = analyze(dtr, opt);
  CHECK_EQ_LONG((long)rd.tree.instances.size(), (long)kNest);
  CHECK_EQ_LONG(rd.tree.unbalanced_enter, 0);
  std::string jd = to_json(rd, dtr);
  CHECK(!jd.empty());
  CHECK(jd.find("\"instances\"") != std::string::npos);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
