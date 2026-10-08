#include "check.h"
#include "event.hpp"
#include "trace.h"
#include <cstring>
#include <string>
#include <unistd.h>

using namespace ct;

int main() {
  int fails = 0;
  const char *path = "/tmp/ct_ev.bin";
  unlink(path);

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = 9;
  strcpy(meta.exe, "/bin/app");
  ct_trace_module mods[1];
  mods[0].base = 0x400000ULL;
  strcpy(mods[0].path, "/bin/app");
  ct_trace_symbol syms[1];
  memset(syms, 0, sizeof(syms));
  syms[0].module = 0; syms[0].offset = 0x1000ULL; strcpy(syms[0].name, "main");

  ct_buffer *b = ct_buffer_new(4);
  ct_event e = {.tid = 5, .kind = CT_EV_ENTER, .ts = 10, .fn = 0x401000ULL, .call_site = 0};
  ct_buffer_push(b, e);
  e.ts = 40; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  const ct_buffer *bufs[1] = {b};
  ct_trace_write(path, bufs, 1, mods, 1, syms, 1, &meta);

  Trace t;
  std::string err;
  CHECK(load_trace(path, t, err));
  CHECK_EQ_LONG(t.pid, 9);
  CHECK_EQ_LONG(t.total_events, 2);
  CHECK_EQ_LONG((long)t.threads.size(), 1);
  CHECK_EQ_LONG(t.threads[0].tid, 5);
  CHECK_EQ_LONG((long)t.threads[0].events.size(), 2);
  CHECK_EQ_LONG(t.threads[0].events[0].kind, 0);
  CHECK_EQ_LONG(t.threads[0].events[0].fn_id, 0);
  CHECK_EQ_LONG(t.threads[0].events[1].ts, 40);
  CHECK(t.name_of(0) == "main");
  CHECK(t.name_of(99).substr(0, 2) == "0x");

  CHECK(load_trace(path, t, err));
  CHECK_EQ_LONG((long)t.threads.size(), 1);
  CHECK_EQ_LONG((long)t.symbols.size(), 1);

  Trace bad;
  CHECK(!load_trace("/tmp/does_not_exist_xyz.bin", bad, err));
  ct_buffer_free(b);
  unlink(path);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
