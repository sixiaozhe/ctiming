#include "check.h"
#include "event.hpp"
#include "calltree.hpp"
#include "aggregate.hpp"

using namespace ct;
static ThreadEvents mk(uint32_t tid, std::initializer_list<TraceEvent> evs) {
  ThreadEvents t; t.tid = tid; for (auto e : evs) t.events.push_back(e); return t;
}
static TraceEvent ev(uint8_t kind, uint32_t fn, uint64_t ts) {
  TraceEvent e; e.kind = kind; e.fn_id = fn; e.ts = ts; return e;
}

int main() {
  int fails = 0;
  std::vector<ThreadEvents> threads;
  threads.push_back(mk(1, {
    ev(0,0,0), ev(0,1,10), ev(1,1,30), ev(0,1,40), ev(1,1,50), ev(1,0,60)
  }));
  CallTree tree = build_call_tree(threads);
  Analysis a = aggregate(tree, 3);
  CHECK_EQ_LONG((long)a.funcs.size(), 3);
  CHECK_EQ_LONG(a.funcs[0].calls, 1);
  CHECK_EQ_LONG(a.funcs[0].total_ns, 60);
  CHECK_EQ_LONG(a.funcs[0].self_ns, 30);
  CHECK_EQ_LONG(a.funcs[0].min_ns, 60);
  CHECK_EQ_LONG(a.funcs[0].max_ns, 60);
  CHECK_EQ_LONG(a.funcs[1].calls, 2);
  CHECK_EQ_LONG(a.funcs[1].total_ns, 30);
  CHECK_EQ_LONG(a.funcs[1].self_ns, 30);
  CHECK_EQ_LONG(a.funcs[1].min_ns, 10);
  CHECK_EQ_LONG(a.funcs[1].max_ns, 20);
  CHECK_EQ_LONG(a.funcs[2].calls, 0);

  const Edge *e = a.find_edge(0, 1);
  CHECK(e != nullptr);
  if (e) { CHECK_EQ_LONG(e->count, 2); CHECK_EQ_LONG(e->total_ns, 30); CHECK(!e->recursive); }
  CHECK(a.find_edge(1, 0) == nullptr);

  CHECK_EQ_LONG((long)a.aggregated.size(), 1);
  CHECK_EQ_LONG(a.aggregated[0].fn_id, 0);
  CHECK_EQ_LONG(a.aggregated[0].calls, 1);
  CHECK_EQ_LONG(a.aggregated[0].total_ns, 60);
  CHECK_EQ_LONG((long)a.aggregated[0].children.size(), 1);
  CHECK_EQ_LONG(a.aggregated[0].children[0].fn_id, 1);
  CHECK_EQ_LONG(a.aggregated[0].children[0].calls, 2);

  std::vector<ThreadEvents> self;
  self.push_back(mk(1, { ev(0,5,0), ev(0,5,10), ev(1,5,20), ev(1,5,40) }));
  Analysis sa = aggregate(build_call_tree(self), 6);
  const Edge *es = sa.find_edge(5, 5);
  CHECK(es != nullptr);
  if (es) CHECK(es->recursive);
  CHECK_EQ_LONG(sa.funcs[5].calls, 2);
  CHECK_EQ_LONG(sa.funcs[5].total_ns, 50);
  CHECK_EQ_LONG(sa.aggregated[0].calls, 1);
  CHECK_EQ_LONG((long)sa.aggregated[0].children.size(), 1);
  CHECK_EQ_LONG(sa.aggregated[0].children[0].fn_id, 5);

  std::vector<ThreadEvents> cyc;
  cyc.push_back(mk(1, {
    ev(0,0,0), ev(0,1,10), ev(0,0,20), ev(1,0,30), ev(1,1,40), ev(1,0,50)
  }));
  Analysis ca = aggregate(build_call_tree(cyc), 2);
  const Edge *c01 = ca.find_edge(0, 1);
  const Edge *c10 = ca.find_edge(1, 0);
  CHECK(c01 != nullptr);
  CHECK(c10 != nullptr);
  if (c01) CHECK(c01->recursive);
  if (c10) CHECK(c10->recursive);

  std::vector<ThreadEvents> mr;
  mr.push_back(mk(1, { ev(0,3,0), ev(1,3,10) }));
  mr.push_back(mk(2, { ev(0,3,0), ev(1,3,20) }));
  Analysis ma = aggregate(build_call_tree(mr), 4);
  CHECK_EQ_LONG((long)ma.aggregated.size(), 1);
  CHECK_EQ_LONG(ma.aggregated[0].fn_id, 3);
  CHECK_EQ_LONG(ma.aggregated[0].calls, 2);
  CHECK_EQ_LONG(ma.aggregated[0].total_ns, 30);

  std::vector<ThreadEvents> long_cyc;
  long_cyc.push_back(mk(1, {
    ev(0,0,0), ev(0,1,10), ev(0,2,20), ev(0,0,30),
    ev(1,0,40), ev(1,2,50), ev(1,1,60), ev(1,0,70)
  }));
  Analysis lca = aggregate(build_call_tree(long_cyc), 3);
  const Edge *e01 = lca.find_edge(0, 1);
  const Edge *e12 = lca.find_edge(1, 2);
  const Edge *e20 = lca.find_edge(2, 0);
  CHECK(e01 != nullptr);
  CHECK(e12 != nullptr);
  CHECK(e20 != nullptr);
  if (e01) CHECK(e01->recursive);
  if (e12) CHECK(e12->recursive);
  if (e20) CHECK(e20->recursive);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
