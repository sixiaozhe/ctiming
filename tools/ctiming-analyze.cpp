#include "event.hpp"
#include "analysis.hpp"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

static void usage(const char *argv0) {
  std::fprintf(stderr, "usage: %s <trace.ctrace> [--json FILE] [--include GLOB] [--exclude GLOB] [--min-total NS] [--top N]\n", argv0);
}

static bool parse_u64(const char *s, uint64_t &out) {
  if (s == nullptr || *s == '\0' || *s == '-') return false;
  errno = 0;
  char *end = nullptr;
  unsigned long long v = std::strtoull(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return false;
  out = static_cast<uint64_t>(v);
  return true;
}

static bool parse_int(const char *s, int &out) {
  if (s == nullptr || *s == '\0') return false;
  errno = 0;
  char *end = nullptr;
  long v = std::strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return false;
  if (v < 0 || v > INT_MAX) return false;
  out = static_cast<int>(v);
  return true;
}

static bool missing_value(const char *argv0, const std::string &a) {
  std::fprintf(stderr, "missing value for %s\n", a.c_str());
  usage(argv0);
  return false;
}

int main(int argc, char **argv) {
  std::string trace_path, json_path, include, exclude;
  bool has_include = false, has_exclude = false;
  uint64_t min_total = 0;
  int top = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    bool wants_value = (a == "--json" || a == "--include" || a == "--exclude" ||
                        a == "--min-total" || a == "--top");
    if (wants_value) {
      if (i + 1 >= argc || argv[i + 1][0] == '-') { missing_value(argv[0], a); return 2; }
      const char *v = argv[++i];
      if (a == "--json") { json_path = v; }
      else if (a == "--include") { include = v; has_include = true; }
      else if (a == "--exclude") { exclude = v; has_exclude = true; }
      else if (a == "--min-total") {
        if (!parse_u64(v, min_total)) {
          std::fprintf(stderr, "invalid value for --min-total: %s\n", v);
          usage(argv[0]);
          return 2;
        }
      } else {
        if (!parse_int(v, top)) {
          std::fprintf(stderr, "invalid value for --top: %s\n", v);
          usage(argv[0]);
          return 2;
        }
      }
    }
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown option: %s\n", a.c_str()); usage(argv[0]); return 2; }
    else if (trace_path.empty()) { trace_path = a; }
    else { std::fprintf(stderr, "too many arguments\n"); usage(argv[0]); return 2; }
  }
  if (trace_path.empty()) {
    usage(argv[0]);
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
    size_t n = std::fwrite(js.data(), 1, js.size(), f);
    int cr = std::fclose(f);
    if (n != js.size() || cr != 0) {
      std::fprintf(stderr, "cannot write %s\n", json_path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", json_path.c_str());
  } else {
    std::fputs(ct::render_text(r, trace).c_str(), stdout);
  }
  return 0;
}
