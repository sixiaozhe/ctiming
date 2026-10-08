#include "event.hpp"
#include "trace.h"
#include <cstdio>
#include <utility>

namespace ct {

std::string Trace::name_of(uint32_t fn_id) const {
  const SymbolInfo *s = symbol(fn_id);
  if (!s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%x", fn_id);
    return buf;
  }
  if (!s->name.empty()) return s->name;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)s->offset);
  return buf;
}

bool load_trace(const std::string &path, Trace &out, std::string &err) {
  out = Trace{};
  err.clear();
  ct_trace_reader r;
  int rc = ct_trace_open(path.c_str(), &r);
  if (rc != 0) {
    err = "ct_trace_open failed: " + std::to_string(rc);
    return false;
  }
  out.exe = r.header.exe;
  out.pid = r.header.pid;
  out.flags = r.header.flags;
  out.total_events = r.total_events;
  out.dropped = r.dropped;
  for (uint32_t i = 0; i < r.n_modules; i++)
    out.modules.push_back(ModuleInfo{r.modules[i].base, r.modules[i].path});
  for (uint32_t i = 0; i < r.n_symbols; i++)
    out.symbols.push_back(SymbolInfo{r.symbols[i].module, r.symbols[i].offset, r.symbols[i].name});
  for (uint32_t ti = 0; ti < r.n_threads; ti++) {
    ThreadEvents te;
    te.tid = r.threads[ti].tid;
    for (uint32_t i = 0; i < r.threads[ti].n_events; i++) {
      const ct_trace_event &e = r.threads[ti].events[i];
      TraceEvent tev;
      tev.tid = te.tid;
      tev.kind = e.kind;
      tev.ts = e.ts;
      tev.fn_id = e.fn_id;
      tev.has_call_site = e.has_call_site != 0;
      tev.call_site = e.call_site;
      te.events.push_back(tev);
    }
    out.threads.push_back(std::move(te));
  }
  ct_trace_close(&r);
  return true;
}

} // namespace ct
