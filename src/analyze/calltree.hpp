#pragma once
#include <cstdint>
#include <vector>
#include "event.hpp"

namespace ct {

struct Instance {
  uint32_t id = 0;
  uint32_t fn_id = 0;
  uint32_t tid = 0;
  uint32_t depth = 0;
  int parent = -1;
  std::vector<uint32_t> children;
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  uint64_t self_ns = 0;
};

struct CallTree {
  std::vector<Instance> instances;
  std::vector<uint32_t> roots;
  uint32_t unbalanced_enter = 0;
  uint32_t orphan_exit = 0;
};

CallTree build_call_tree(const std::vector<ThreadEvents> &threads);

} // namespace ct
