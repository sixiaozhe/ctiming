#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned calls;
  uint32_t fn_id;
} fn_count;

static void print_symbol(const ct_trace_reader *r, uint32_t fn_id) {
  if (fn_id < r->n_symbols) {
    const ct_trace_symbol *s = &r->symbols[fn_id];
    if (s->module == 0xFFFFFFFFu) printf("0x%llx", (unsigned long long)s->offset);
    else if (s->name[0]) printf("%s", s->name);
    else printf("0x%llx", (unsigned long long)s->offset);
  } else {
    printf("fn#%u", fn_id);
  }
}

static int cmp_fn_count(const void *a, const void *b) {
  const fn_count *x = (const fn_count *)a;
  const fn_count *y = (const fn_count *)b;
  if (x->calls != y->calls) return x->calls < y->calls ? 1 : -1;
  return x->fn_id < y->fn_id ? -1 : (x->fn_id > y->fn_id ? 1 : 0);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <trace.ctrace>\n", argv[0]);
    return 2;
  }

  ct_trace_reader r;
  int rc = ct_trace_open(argv[1], &r);
  if (rc != 0) {
    fprintf(stderr, "ct_trace_open failed: %d\n", rc);
    return 1;
  }

  printf("exe: %s\n", r.header.exe);
  printf("pid: %u\n", r.header.pid);
  printf("modules: %u\n", r.n_modules);
  printf("symbols: %u\n", r.n_symbols);
  printf("threads: %u\n", r.n_threads);
  printf("total_events: %u\n", r.total_events);
  printf("dropped: %u\n", r.dropped);

  unsigned *calls = (unsigned *)calloc(r.n_symbols ? r.n_symbols : 1, sizeof(unsigned));
  if (calls == NULL) {
    ct_trace_close(&r);
    fprintf(stderr, "out of memory\n");
    return 1;
  }

  for (uint32_t ti = 0; ti < r.n_threads; ti++)
    for (uint32_t i = 0; i < r.threads[ti].n_events; i++)
      if (r.threads[ti].events[i].kind == 0 && r.threads[ti].events[i].fn_id < r.n_symbols)
        calls[r.threads[ti].events[i].fn_id]++;

  fn_count *list = (fn_count *)calloc(r.n_symbols ? r.n_symbols : 1, sizeof(fn_count));
  if (list == NULL) {
    free(calls);
    ct_trace_close(&r);
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  uint32_t nlist = 0;
  for (uint32_t i = 0; i < r.n_symbols; i++) {
    if (!calls[i]) continue;
    list[nlist].calls = calls[i];
    list[nlist].fn_id = i;
    nlist++;
  }
  qsort(list, nlist, sizeof(list[0]), cmp_fn_count);

  printf("functions:\n");
  for (uint32_t i = 0; i < nlist; i++) {
    printf("  %u  ", list[i].calls);
    print_symbol(&r, list[i].fn_id);
    printf("\n");
  }

  free(list);
  free(calls);
  ct_trace_close(&r);
  return 0;
}
