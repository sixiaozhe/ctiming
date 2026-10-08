#include "analysis.hpp"
#include "json.hpp"
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>
#include "glob.h"

namespace ct {

AnalysisResult analyze(const Trace &trace, const Options &opt) {
  AnalysisResult r;
  r.tree = build_call_tree(trace.threads);
  r.agg = aggregate(r.tree, (uint32_t)trace.symbols.size());
  r.top = opt.top;
  uint32_t n = (uint32_t)r.agg.funcs.size();
  r.keep.assign(n, true);
  for (uint32_t i = 0; i < n; i++) {
    const std::string name = trace.name_of(i);
    bool pass = ct_filter_match(opt.has_include ? opt.include.c_str() : nullptr,
                                opt.has_exclude ? opt.exclude.c_str() : nullptr,
                                name.c_str()) != 0;
    if (opt.min_total_ns > 0 && r.agg.funcs[i].total_ns < opt.min_total_ns) pass = false;
    r.keep[i] = pass;
  }
  return r;
}

static bool kept(const std::vector<bool> &keep, uint32_t fn) {
  return fn >= keep.size() ? true : keep[fn];
}

static void write_funcs(JsonWriter &w, const AnalysisResult &r, const Trace &trace) {
  w.key("functions"); w.begin_array();
  for (const FuncStats &f : r.agg.funcs) {
    if (!kept(r.keep, f.fn_id)) continue;
    w.begin_object();
    w.key("id"); w.number((uint64_t)f.fn_id);
    w.key("name"); w.str(trace.name_of(f.fn_id));
    const SymbolInfo *s = trace.symbol(f.fn_id);
    w.key("module"); w.number((uint64_t)(s ? (int64_t)s->module : (int64_t)0xFFFFFFFFu));
    w.key("offset"); w.number((uint64_t)(s ? s->offset : 0));
    w.key("calls"); w.number(f.calls);
    w.key("total_ns"); w.number(f.total_ns);
    w.key("self_ns"); w.number(f.self_ns);
    w.key("min_ns"); w.number(f.min_ns);
    w.key("max_ns"); w.number(f.max_ns);
    w.end_object();
  }
  w.end_array();
}

static void write_agg(JsonWriter &w, const AggNode &n, const Trace &trace) {
  w.begin_object();
  w.key("fn"); w.number((uint64_t)n.fn_id);
  w.key("name"); w.str(trace.name_of(n.fn_id));
  w.key("calls"); w.number(n.calls);
  w.key("total_ns"); w.number(n.total_ns);
  w.key("self_ns"); w.number(n.self_ns);
  w.key("children"); w.begin_array();
  for (const AggNode &c : n.children) write_agg(w, c, trace);
  w.end_array();
  w.end_object();
}

std::string to_json(const AnalysisResult &r, const Trace &trace) {
  JsonWriter w;
  w.begin_object();
  w.key("trace"); w.begin_object();
  w.key("exe"); w.str(trace.exe);
  w.key("pid"); w.number((uint64_t)trace.pid);
  w.key("flags"); w.number((uint64_t)trace.flags);
  w.key("modules"); w.number((uint64_t)trace.modules.size());
  w.key("symbols"); w.number((uint64_t)trace.symbols.size());
  w.key("threads"); w.number((uint64_t)trace.threads.size());
  w.key("total_events"); w.number((uint64_t)trace.total_events);
  w.key("dropped"); w.number((uint64_t)trace.dropped);
  w.end_object();

  write_funcs(w, r, trace);

  w.key("call_graph"); w.begin_array();
  for (const Edge &e : r.agg.edges) {
    if (!kept(r.keep, e.caller) || !kept(r.keep, e.callee)) continue;
    w.begin_object();
    w.key("caller"); w.number((uint64_t)e.caller);
    w.key("callee"); w.number((uint64_t)e.callee);
    w.key("count"); w.number(e.count);
    w.key("total_ns"); w.number(e.total_ns);
    w.key("recursive"); w.boolean(e.recursive);
    w.end_object();
  }
  w.end_array();

  w.key("aggregated"); w.begin_array();
  for (const AggNode &n : r.agg.aggregated) write_agg(w, n, trace);
  w.end_array();

  w.key("threads"); w.begin_array();
  for (const ThreadEvents &te : trace.threads) {
    w.begin_object();
    w.key("tid"); w.number((uint64_t)te.tid);
    w.key("roots"); w.begin_array();
    for (const Instance &in : r.tree.instances)
      if (in.parent == -1 && in.tid == te.tid) w.number((uint64_t)in.id);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  w.key("instances"); w.begin_array();
  for (const Instance &in : r.tree.instances) {
    w.begin_object();
    w.key("id"); w.number((uint64_t)in.id);
    w.key("fn"); w.number((uint64_t)in.fn_id);
    w.key("name"); w.str(trace.name_of(in.fn_id));
    w.key("tid"); w.number((uint64_t)in.tid);
    w.key("depth"); w.number((uint64_t)in.depth);
    w.key("parent"); w.number((int64_t)in.parent);
    w.key("start_ns"); w.number(in.start_ns);
    w.key("end_ns"); w.number(in.end_ns);
    w.key("self_ns"); w.number(in.self_ns);
    w.key("children"); w.begin_array();
    for (uint32_t c : in.children) w.number((uint64_t)c);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  w.end_object();
  return w.str_out();
}

static std::string fmt_ns(uint64_t ns) {
  char b[32];
  if (ns >= 1000000000ULL) std::snprintf(b, sizeof(b), "%.3fs", (double)ns / 1e9);
  else if (ns >= 1000000ULL) std::snprintf(b, sizeof(b), "%.3fms", (double)ns / 1e6);
  else if (ns >= 1000ULL) std::snprintf(b, sizeof(b), "%.3fus", (double)ns / 1e3);
  else std::snprintf(b, sizeof(b), "%lluns", (unsigned long long)ns);
  return b;
}

std::string render_text(const AnalysisResult &r, const Trace &trace) {
  std::vector<const FuncStats *> rows;
  for (const FuncStats &f : r.agg.funcs)
    if (f.calls && kept(r.keep, f.fn_id)) rows.push_back(&f);
  std::sort(rows.begin(), rows.end(), [](const FuncStats *a, const FuncStats *b) {
    return a->total_ns > b->total_ns;
  });
  if (r.top > 0 && (size_t)r.top < rows.size()) rows.resize((size_t)r.top);
  std::ostringstream os;
  os << "exe: " << trace.exe << "\n";
  os << "threads: " << trace.threads.size() << "  total_events: " << trace.total_events
     << "  dropped: " << trace.dropped << "\n";
  os << "functions (by total time):\n";
  os << "  calls        total       self     function\n";
  size_t limit = rows.size();
  for (size_t i = 0; i < limit; i++) {
    const FuncStats *f = rows[i];
    char total[32], self[32];
    std::snprintf(total, sizeof(total), "%s", fmt_ns(f->total_ns).c_str());
    std::snprintf(self, sizeof(self), "%s", fmt_ns(f->self_ns).c_str());
    os << "  " << f->calls << "  " << total << "  " << self << "  " << trace.name_of(f->fn_id) << "\n";
  }
  return os.str();
}

} // namespace ct
