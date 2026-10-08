#include "check.h"
#include "event.hpp"
#include "calltree.hpp"

using namespace ct;

static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid;
  for (auto e : evs) t.events.push_back(e);
  return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  std::vector<ThreadEvents> threads;
  threads.push_back(mk(7, {
    ev(0,0,0), ev(0,1,10), ev(0,2,20), ev(1,2,50), ev(1,1,60),
    ev(0,1,70), ev(1,1,90), ev(1,0,100)
  }));

  CallTree tree = build_call_tree(threads);
  CHECK_EQ_LONG((long)tree.instances.size(), 4);
  CHECK_EQ_LONG(tree.instances[0].fn_id, 0);
  CHECK_EQ_LONG(tree.instances[0].depth, 0);
  CHECK_EQ_LONG(tree.instances[0].parent, -1);
  CHECK_EQ_LONG(tree.instances[0].start_ns, 0);
  CHECK_EQ_LONG(tree.instances[0].end_ns, 100);
  CHECK_EQ_LONG(tree.instances[0].self_ns, 30);
  CHECK_EQ_LONG((long)tree.instances[0].children.size(), 2);
  CHECK_EQ_LONG(tree.instances[1].fn_id, 1);
  CHECK_EQ_LONG(tree.instances[1].depth, 1);
  CHECK_EQ_LONG(tree.instances[1].parent, 0);
  CHECK_EQ_LONG(tree.instances[1].end_ns, 60);
  CHECK_EQ_LONG(tree.instances[1].self_ns, 20);
  CHECK_EQ_LONG(tree.instances[2].fn_id, 2);
  CHECK_EQ_LONG(tree.instances[2].depth, 2);
  CHECK_EQ_LONG(tree.instances[2].self_ns, 30);
  CHECK_EQ_LONG((long)tree.roots.size(), 1);
  CHECK_EQ_LONG(tree.roots[0], 0);
  CHECK_EQ_LONG(tree.unbalanced_enter, 0);
  CHECK_EQ_LONG(tree.orphan_exit, 0);

  std::vector<ThreadEvents> t2;
  t2.push_back(mk(9, { ev(1,3,5) }));
  CallTree tree2 = build_call_tree(t2);
  CHECK_EQ_LONG((long)tree2.instances.size(), 0);
  CHECK_EQ_LONG(tree2.orphan_exit, 1);

  std::vector<ThreadEvents> rec;
  rec.push_back(mk(1, { ev(0,1,0), ev(0,1,10), ev(1,1,20), ev(1,1,40) }));
  CallTree tree_rec = build_call_tree(rec);
  CHECK_EQ_LONG((long)tree_rec.instances.size(), 2);
  CHECK_EQ_LONG(tree_rec.instances[0].fn_id, 1);
  CHECK_EQ_LONG(tree_rec.instances[1].fn_id, 1);
  CHECK_EQ_LONG(tree_rec.instances[0].parent, -1);
  CHECK_EQ_LONG(tree_rec.instances[1].parent, 0);
  CHECK_EQ_LONG(tree_rec.instances[0].end_ns, 40);
  CHECK_EQ_LONG(tree_rec.instances[1].end_ns, 20);
  CHECK_EQ_LONG(tree_rec.instances[1].self_ns, 10);
  CHECK_EQ_LONG(tree_rec.instances[0].self_ns, 30);

  std::vector<ThreadEvents> clamp;
  clamp.push_back(mk(1, { ev(0,1,100), ev(1,1,50) }));
  CallTree tree_clamp = build_call_tree(clamp);
  CHECK_EQ_LONG((long)tree_clamp.instances.size(), 1);
  CHECK_EQ_LONG(tree_clamp.instances[0].end_ns, 100);
  CHECK_EQ_LONG(tree_clamp.instances[0].self_ns, 0);

  std::vector<ThreadEvents> over;
  over.push_back(mk(1, { ev(0,1,0), ev(0,2,10), ev(1,2,100), ev(1,1,20) }));
  CallTree tree_over = build_call_tree(over);
  CHECK_EQ_LONG((long)tree_over.instances.size(), 2);
  CHECK(tree_over.instances[0].end_ns >= tree_over.instances[0].start_ns);
  CHECK_EQ_LONG(tree_over.instances[0].self_ns, 0);

  std::vector<ThreadEvents> mt;
  mt.push_back(mk(1, { ev(0,1,0), ev(1,1,5) }));
  mt.push_back(mk(2, { ev(0,2,0), ev(1,2,5) }));
  CallTree tree_mt = build_call_tree(mt);
  CHECK_EQ_LONG((long)tree_mt.roots.size(), 2);
  CHECK_EQ_LONG(tree_mt.roots[0], 0);
  CHECK_EQ_LONG(tree_mt.roots[1], 1);
  CHECK_EQ_LONG(tree_mt.instances[0].tid, 1);
  CHECK_EQ_LONG(tree_mt.instances[1].tid, 2);
  CHECK_EQ_LONG(tree_mt.instances[1].parent, -1);
  CHECK_EQ_LONG(tree_mt.instances[1].depth, 0);

  std::vector<ThreadEvents> unbal;
  unbal.push_back(mk(1, { ev(0,1,0), ev(0,1,10) }));
  CallTree tree_unbal = build_call_tree(unbal);
  CHECK_EQ_LONG((long)tree_unbal.instances.size(), 2);
  CHECK_EQ_LONG(tree_unbal.unbalanced_enter, 2);
  CHECK_EQ_LONG(tree_unbal.instances[0].end_ns, 0);
  CHECK_EQ_LONG(tree_unbal.instances[0].self_ns, 0);
  CHECK_EQ_LONG(tree_unbal.instances[1].end_ns, 10);
  CHECK_EQ_LONG(tree_unbal.instances[1].self_ns, 0);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
