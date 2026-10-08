#include "calltree.hpp"

namespace ct {

CallTree build_call_tree(const std::vector<ThreadEvents> &threads) {
  CallTree tree;
  for (const ThreadEvents &te : threads) {
    std::vector<uint32_t> stack;
    for (const TraceEvent &e : te.events) {
      if (e.kind == 0) {
        Instance inst;
        inst.id = (uint32_t)tree.instances.size();
        inst.fn_id = e.fn_id;
        inst.tid = te.tid;
        inst.depth = (uint32_t)stack.size();
        inst.start_ns = e.ts;
        if (stack.empty()) {
          tree.roots.push_back(inst.id);
        } else {
          inst.parent = (int)stack.back();
          tree.instances[stack.back()].children.push_back(inst.id);
        }
        stack.push_back(inst.id);
        tree.instances.push_back(inst);
      } else if (e.kind == 1) {
        if (stack.empty()) {
          tree.orphan_exit++;
          continue;
        }
        uint32_t id = stack.back();
        stack.pop_back();
        Instance &inst = tree.instances[id];
        inst.end_ns = e.ts > inst.start_ns ? e.ts : inst.start_ns;
        uint64_t child_total = 0;
        for (uint32_t c : inst.children) {
          const Instance &ci = tree.instances[c];
          child_total += ci.end_ns - ci.start_ns;
        }
        uint64_t dur = inst.end_ns - inst.start_ns;
        inst.self_ns = dur > child_total ? dur - child_total : 0;
      }
    }
    if (!stack.empty()) {
      tree.unbalanced_enter += (uint32_t)stack.size();
      for (uint32_t id : stack) {
        Instance &inst = tree.instances[id];
        inst.end_ns = inst.start_ns;
        inst.self_ns = 0;
      }
    }
  }
  return tree;
}

} // namespace ct
