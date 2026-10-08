#pragma once
#include <cstdint>
#include <vector>
#include "calltree.hpp"

namespace ct {

struct FuncStats {
  uint32_t fn_id = 0;
  uint64_t calls = 0;
  uint64_t total_ns = 0;
  uint64_t self_ns = 0;
  uint64_t min_ns = 0;
  uint64_t max_ns = 0;
};

struct Edge {
  uint32_t caller = 0;
  uint32_t callee = 0;
  uint64_t count = 0;
  uint64_t total_ns = 0;
  bool recursive = false;
};

struct AggNode {
  uint32_t fn_id = 0;
  uint64_t calls = 0;
  uint64_t total_ns = 0;
  uint64_t self_ns = 0;
  std::vector<AggNode> children;
};

struct Analysis {
  std::vector<FuncStats> funcs;
  std::vector<Edge> edges;
  std::vector<AggNode> aggregated;
  const Edge *find_edge(uint32_t caller, uint32_t callee) const;
};

Analysis aggregate(const CallTree &tree, uint32_t n_symbols);

} // namespace ct
