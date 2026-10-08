#include "event.hpp"
#include "analysis.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv) {
  std::string trace_path, json_path, include, exclude;
  bool has_include = false, has_exclude = false;
  uint64_t min_total = 0;
  int top = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--json" && i + 1 < argc) { json_path = argv[++i]; }
    else if (a == "--include" && i + 1 < argc) { include = argv[++i]; has_include = true; }
    else if (a == "--exclude" && i + 1 < argc) { exclude = argv[++i]; has_exclude = true; }
    else if (a == "--min-total" && i + 1 < argc) { min_total = std::strtoull(argv[++i], nullptr, 10); }
    else if (a == "--top" && i + 1 < argc) { top = std::atoi(argv[++i]); }
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); return 2; }
    else if (trace_path.empty()) { trace_path = a; }
    else { std::fprintf(stderr, "too many arguments\n"); return 2; }
  }
  if (trace_path.empty()) {
    std::fprintf(stderr, "usage: %s <trace.ctrace> [--json FILE] [--include GLOB] [--exclude GLOB] [--min-total NS] [--top N]\n", argv[0]);
    return 2;
  }
  ct::Trace trace;
  std::string err;
  if (!ct::load_trace(trace_path, trace, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
  ct::Options opt;
  opt.include = include; opt.exclude = exclude;
  opt.has_include = has_include; opt.has_exclude = has_exclude;
  opt.min_total_ns = min_total; opt.top = top;
  ct::AnalysisResult r = ct::analyze(trace, opt);
  if (!json_path.empty()) {
    std::string js = ct::to_json(r, trace);
    FILE *f = std::fopen(json_path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", json_path.c_str()); return 1; }
    std::fwrite(js.data(), 1, js.size(), f);
    std::fclose(f);
    std::printf("wrote %s\n", json_path.c_str());
  } else {
    std::fputs(ct::render_text(r, trace).c_str(), stdout);
  }
  return 0;
}
