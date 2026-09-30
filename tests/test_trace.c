#include "check.h"
#include "trace.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
  int fails = 0;
  const char *path = "/tmp/ct_test_trace.bin";
  unlink(path);

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = 4242;
  meta.start_ns = 1000000ULL;
  meta.flags = 0;
  strcpy(meta.exe, "/bin/app");

  ct_trace_module mods[1];
  mods[0].base = 0x400000ULL;
  strcpy(mods[0].path, "/bin/app");

  ct_trace_symbol syms[2];
  memset(syms, 0, sizeof(syms));
  syms[0].module = 0; syms[0].offset = 0x1000ULL; strcpy(syms[0].name, "main");
  syms[1].module = 0; syms[1].offset = 0x2000ULL; strcpy(syms[1].name, "foo");

  ct_buffer *b = ct_buffer_new(4);
  ct_event e = { .tid = 1, .ts = 1000, .fn = 0x401000ULL, .call_site = 0, .kind = CT_EV_ENTER };
  ct_buffer_push(b, e);
  e.ts = 1500; e.fn = 0x402000ULL; e.kind = CT_EV_ENTER;
  ct_buffer_push(b, e);
  e.ts = 1700; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  e.ts = 2000; e.fn = 0x401000ULL; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);

  const ct_buffer *bufs[1] = { b };
  CHECK_EQ_LONG(ct_trace_write(path, bufs, 1, mods, 1, syms, 2, &meta), 0);

  ct_trace_reader r;
  CHECK_EQ_LONG(ct_trace_open(path, &r), 0);
  CHECK_EQ_LONG(r.header.pid, 4242);
  CHECK_EQ_LONG(r.header.start_ns, 1000000ULL);
  CHECK_EQ_LONG(r.n_modules, 1);
  CHECK_EQ_LONG(r.n_symbols, 2);
  CHECK_EQ_LONG(r.total_events, 4);
  CHECK_EQ_LONG(r.dropped, 0);
  CHECK(strcmp(r.modules[0].path, "/bin/app") == 0);
  CHECK_EQ_LONG(r.modules[0].base, 0x400000ULL);
  CHECK(strcmp(r.symbols[0].name, "main") == 0);
  CHECK_EQ_LONG(r.symbols[1].offset, 0x2000ULL);

  ct_trace_thread *t = &r.threads[0];
  CHECK_EQ_LONG(t->tid, 1);
  CHECK_EQ_LONG(t->n_events, 4);
  CHECK_EQ_LONG(t->events[0].fn_id, 0);
  CHECK_EQ_LONG(t->events[0].kind, CT_EV_ENTER);
  CHECK_EQ_LONG(t->events[0].ts, 1000);
  CHECK_EQ_LONG(t->events[1].fn_id, 1);
  CHECK_EQ_LONG(t->events[2].kind, CT_EV_EXIT);
  CHECK_EQ_LONG(t->events[3].ts, 2000);

  ct_trace_close(&r);
  ct_buffer_free(b);
  unlink(path);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
