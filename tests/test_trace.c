#include "check.h"
#include "trace.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static void w16(FILE *f, uint16_t v) {
  unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) };
  fwrite(b, 1, 2, f);
}
static void w32(FILE *f, uint32_t v) {
  unsigned char b[4];
  for (int i = 0; i < 4; i++) b[i] = (unsigned char)(v >> (8 * i));
  fwrite(b, 1, 4, f);
}
static void w64(FILE *f, uint64_t v) {
  unsigned char b[8];
  for (int i = 0; i < 8; i++) b[i] = (unsigned char)(v >> (8 * i));
  fwrite(b, 1, 8, f);
}

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

  {
    const char *p2 = "/tmp/ct_test_trace_extra.bin";
    unlink(p2);

    ct_trace_meta m2;
    memset(&m2, 0, sizeof(m2));
    m2.pid = 7;
    strcpy(m2.exe, "/bin/app");

    ct_trace_module md2[1];
    md2[0].base = 0x400000ULL;
    strcpy(md2[0].path, "/bin/app");

    ct_trace_symbol sy2[1];
    memset(sy2, 0, sizeof(sy2));
    sy2[0].module = 0; sy2[0].offset = 0x1000ULL; strcpy(sy2[0].name, "main");

    ct_buffer *b2 = ct_buffer_new(4);
    ct_event e2 = { .tid = 3, .ts = 100, .fn = 0x777777ULL, .call_site = 0, .kind = CT_EV_ENTER };
    ct_buffer_push(b2, e2);
    e2.ts = 200; e2.kind = CT_EV_EXIT;
    ct_buffer_push(b2, e2);
    e2.ts = 300; e2.kind = CT_EV_ENTER;
    ct_buffer_push(b2, e2);
    e2.ts = 400; e2.kind = CT_EV_EXIT;
    ct_buffer_push(b2, e2);
    e2.ts = 500; e2.kind = CT_EV_ENTER;
    ct_buffer_push(b2, e2);
    e2.ts = 600; e2.fn = 0x401000ULL; e2.call_site = 0xABCDEFULL; e2.kind = CT_EV_ENTER;
    ct_buffer_push(b2, e2);

    const ct_buffer *bufs2[1] = { b2 };
    CHECK_EQ_LONG(ct_trace_write(p2, bufs2, 1, md2, 1, sy2, 1, &m2), 0);

    ct_trace_reader r2;
    CHECK_EQ_LONG(ct_trace_open(p2, &r2), 0);
    CHECK_EQ_LONG(r2.total_events, 6);
    CHECK_EQ_LONG(r2.n_symbols, 2);
    CHECK_EQ_LONG(r2.symbols[1].module, 0xFFFFFFFFu);
    CHECK_EQ_LONG(r2.symbols[1].offset, 0x777777ULL);
    for (int i = 0; i < 5; i++) CHECK_EQ_LONG(r2.threads[0].events[i].fn_id, 1);
    CHECK_EQ_LONG(r2.threads[0].events[0].has_call_site, 0);
    CHECK_EQ_LONG(r2.threads[0].events[5].fn_id, 0);
    CHECK_EQ_LONG(r2.threads[0].events[5].has_call_site, 1);
    CHECK_EQ_LONG(r2.threads[0].events[5].call_site, 0xABCDEFULL);

    ct_trace_close(&r2);
    ct_buffer_free(b2);
    unlink(p2);
  }

  {
    const char *bp1 = "/tmp/ct_test_trace_bad1.bin";
    FILE *fp = fopen(bp1, "wb");
    CHECK(fp != NULL);
    if (fp) { fwrite("CTMG", 1, 4, fp); fclose(fp); }
    ct_trace_reader rr;
    CHECK(ct_trace_open(bp1, &rr) < 0);
    unlink(bp1);

    const char *bp2 = "/tmp/ct_test_trace_bad2.bin";
    fp = fopen(bp2, "wb");
    CHECK(fp != NULL);
    if (fp) { fwrite("XXXX", 1, 4, fp); fclose(fp); }
    CHECK(ct_trace_open(bp2, &rr) < 0);
    unlink(bp2);
  }

  {
    const char *p3 = "/tmp/ct_test_trace_badmod.bin";
    unlink(p3);

    ct_trace_meta m3;
    memset(&m3, 0, sizeof(m3));
    m3.pid = 1;
    strcpy(m3.exe, "x");

    ct_trace_module md3[1];
    md3[0].base = 0x1000ULL;
    strcpy(md3[0].path, "x");

    ct_trace_symbol sy3[1];
    memset(sy3, 0, sizeof(sy3));
    sy3[0].module = 5; sy3[0].offset = 1; strcpy(sy3[0].name, "bad");

    ct_buffer *b3 = ct_buffer_new(4);
    ct_event e3 = { .tid = 1, .ts = 1, .fn = 0x2000ULL, .call_site = 0, .kind = CT_EV_ENTER };
    ct_buffer_push(b3, e3);

    const ct_buffer *bufs3[1] = { b3 };
    CHECK_EQ_LONG(ct_trace_write(p3, bufs3, 1, md3, 1, sy3, 1, &m3), 0);

    ct_trace_reader r3;
    CHECK_EQ_LONG(ct_trace_open(p3, &r3), 0);
    CHECK_EQ_LONG(r3.n_symbols, 1);
    CHECK(r3.symbols[0].module == 0xFFFFFFFFu);
    CHECK_EQ_LONG((long)r3.threads[0].n_events, 1);
    CHECK_EQ_LONG(r3.threads[0].events[0].fn_id, 0);
    ct_trace_close(&r3);

    ct_buffer_free(b3);
    unlink(p3);
  }

  {
    const char *bp = "/tmp/ct_test_trace_badfn.bin";
    FILE *fp = fopen(bp, "wb");
    CHECK(fp != NULL);
    if (fp) {
      fwrite("CTMG", 1, 4, fp);
      w16(fp, 1);
      fputc(1, fp);
      fputc(8, fp);
      w32(fp, 0);
      w64(fp, 0);
      w32(fp, 0);
      w32(fp, 0);
      w32(fp, 0);
      w32(fp, 1);
      w32(fp, 0xFFFFFFFFu);
      w64(fp, 0x10);
      w32(fp, 0);
      w32(fp, 1);
      w32(fp, 1);
      w32(fp, 1);
      fputc(5, fp);
      fputc(0, fp);
      fputc(0, fp);
      w32(fp, 0);
      w32(fp, 1);
      w32(fp, 0);
      fwrite("CTME", 1, 4, fp);
      fclose(fp);
    }
    ct_trace_reader rr;
    CHECK(ct_trace_open(bp, &rr) < 0);
    unlink(bp);
  }

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
