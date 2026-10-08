#pragma once
#include <string>
#include <vector>
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"

namespace ct {

struct Options {
  std::string include;
  std::string exclude;
  uint64_t min_total_ns = 0;
  int top = 0;
  bool has_include = false;
  bool has_exclude = false;
};

struct AnalysisResult {
  CallTree tree;
  Analysis agg;
  std::vector<bool> keep;
};

AnalysisResult analyze(const Trace &trace, const Options &opt);
std::string to_json(const AnalysisResult &r, const Trace &trace);
std::string render_text(const AnalysisResult &r, const Trace &trace);

} // namespace ct
