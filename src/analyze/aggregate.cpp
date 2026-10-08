#include "aggregate.hpp"
#include <functional>
#include <map>
#include <utility>

namespace ct {

const Edge *Analysis::find_edge(uint32_t caller, uint32_t callee) const {
  for (const Edge &e : edges)
    if (e.caller == caller && e.callee == callee) return &e;
  return nullptr;
}

static void merge_child(AggNode &parent, const AggNode &child) {
  for (AggNode &c : parent.children) {
    if (c.fn_id == child.fn_id) {
      c.calls += child.calls;
      c.total_ns += child.total_ns;
      c.self_ns += child.self_ns;
      for (const AggNode &gc : child.children) merge_child(c, gc);
      return;
    }
  }
  parent.children.push_back(child);
}

static void mark_recursive(std::vector<Edge> &edges) {
  std::map<uint32_t, std::vector<uint32_t>> adj;
  for (const Edge &e : edges) adj[e.caller].push_back(e.callee);
  for (Edge &e : edges) {
    if (e.caller == e.callee) { e.recursive = true; continue; }
    std::vector<uint32_t> stack{e.callee};
    std::map<uint32_t, bool> seen;
    bool found = false;
    while (!stack.empty() && !found) {
      uint32_t cur = stack.back(); stack.pop_back();
      if (cur == e.caller) { found = true; break; }
      if (seen[cur]) continue;
      seen[cur] = true;
      auto it = adj.find(cur);
      if (it != adj.end()) for (uint32_t n : it->second) stack.push_back(n);
    }
    e.recursive = found;
  }
}

Analysis aggregate(const CallTree &tree, uint32_t n_symbols) {
  Analysis a;
  a.funcs.resize(n_symbols);
  for (uint32_t i = 0; i < n_symbols; i++) a.funcs[i].fn_id = i;

  std::map<std::pair<uint32_t, uint32_t>, Edge> emap;
  for (const Instance &in : tree.instances) {
    if (in.fn_id < n_symbols) {
      FuncStats &f = a.funcs[in.fn_id];
      uint64_t dur = in.end_ns - in.start_ns;
      if (f.calls == 0) { f.min_ns = dur; f.max_ns = dur; }
      else { if (dur < f.min_ns) f.min_ns = dur; if (dur > f.max_ns) f.max_ns = dur; }
      f.calls++;
      f.total_ns += dur;
      f.self_ns += in.self_ns;
    }
    if (in.parent >= 0) {
      const Instance &p = tree.instances[in.parent];
      if (p.fn_id >= n_symbols || in.fn_id >= n_symbols) continue;
      Edge &e = emap[std::make_pair(p.fn_id, in.fn_id)];
      e.caller = p.fn_id;
      e.callee = in.fn_id;
      e.count++;
      e.total_ns += in.end_ns - in.start_ns;
    }
  }
  for (auto &kv : emap) a.edges.push_back(kv.second);
  mark_recursive(a.edges);

  std::function<AggNode(const Instance &)> build = [&](const Instance &in) {
    AggNode node;
    node.fn_id = in.fn_id;
    node.calls = 1;
    node.total_ns = in.end_ns - in.start_ns;
    node.self_ns = in.self_ns;
    for (uint32_t cid : in.children) merge_child(node, build(tree.instances[cid]));
    return node;
  };
  for (uint32_t root : tree.roots) {
    AggNode node = build(tree.instances[root]);
    bool merged = false;
    for (AggNode &r : a.aggregated) {
      if (r.fn_id == node.fn_id) {
        r.calls += node.calls;
        r.total_ns += node.total_ns;
        r.self_ns += node.self_ns;
        for (const AggNode &c : node.children) merge_child(r, c);
        merged = true;
        break;
      }
    }
    if (!merged) a.aggregated.push_back(std::move(node));
  }
  return a;
}

} // namespace ct
