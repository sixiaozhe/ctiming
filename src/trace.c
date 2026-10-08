#include "trace.h"
#include "ct_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CT_MAGIC "CTMG"
#define CT_MAGIC_END "CTME"
#define CT_VERSION 1

CT_NOINSTR static void put_u8(FILE *f, uint8_t v) { fputc(v, f); }
CT_NOINSTR static void put_u16(FILE *f, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; fwrite(b, 1, 2, f); }
CT_NOINSTR static void put_u32(FILE *f, uint32_t v) { uint8_t b[4]; for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i)); fwrite(b, 1, 4, f); }
CT_NOINSTR static void put_u64(FILE *f, uint64_t v) { uint8_t b[8]; for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i)); fwrite(b, 1, 8, f); }
CT_NOINSTR static void put_str(FILE *f, const char *s) { size_t n = strlen(s); put_u32(f, (uint32_t)n); if (n) fwrite(s, 1, n, f); }
CT_NOINSTR static void put_varint(FILE *f, uint64_t v) {
  while (v >= 0x80) { put_u8(f, (uint8_t)(v | 0x80)); v >>= 7; }
  put_u8(f, (uint8_t)v);
}

typedef struct {
  uint64_t addr;
  uint32_t id;
} ct_addr_id;

CT_NOINSTR static int cmp_addr_id(const void *a, const void *b) {
  const ct_addr_id *x = (const ct_addr_id *)a;
  const ct_addr_id *y = (const ct_addr_id *)b;
  if (x->addr < y->addr) return -1;
  if (x->addr > y->addr) return 1;
  return 0;
}

CT_NOINSTR static long lookup_addr(const ct_addr_id *index, size_t n, uint64_t addr) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (index[mid].addr == addr) return (long)index[mid].id;
    if (index[mid].addr < addr) lo = mid + 1;
    else hi = mid;
  }
  return -1;
}

CT_NOINSTR static long lookup_extra(const ct_trace_symbol *extra, size_t nextra, uint64_t fn) {
  for (size_t i = 0; i < nextra; i++) {
    if (extra[i].offset == fn) return (long)i;
  }
  return -1;
}

CT_NOINSTR CTIMING_HIDDEN int ct_trace_write(const char *path,
                                             const ct_buffer *const *bufs, size_t nbufs,
                                             const ct_trace_module *mods, size_t nmods,
                                             const ct_trace_symbol *syms, size_t nsyms,
                                             const ct_trace_meta *meta) {
  if (path == NULL || meta == NULL) return -1;
  if (nbufs && bufs == NULL) return -1;
  if (nmods && mods == NULL) return -1;
  if (nsyms && syms == NULL) return -1;

  FILE *f = fopen(path, "wb");
  if (f == NULL) return -1;

  ct_addr_id *index = NULL;
  size_t nindex = 0;
  if (nsyms) {
    index = (ct_addr_id *)malloc(nsyms * sizeof(ct_addr_id));
    if (index == NULL) { fclose(f); return -1; }
    for (size_t i = 0; i < nsyms; i++) {
      if (syms[i].module == CT_UNKNOWN_MODULE || syms[i].module >= nmods) continue;
      index[nindex].addr = mods[syms[i].module].base + syms[i].offset;
      index[nindex].id = (uint32_t)i;
      nindex++;
    }
    qsort(index, nindex, sizeof(index[0]), cmp_addr_id);
  }

  ct_trace_symbol *extra = NULL;
  size_t nextra = 0, capextra = 0;
  uint32_t total = 0, dropped = 0;
  int failed = 0;

  fwrite(CT_MAGIC, 1, 4, f);
  put_u16(f, CT_VERSION);
  put_u8(f, 1);
  put_u8(f, (uint8_t)sizeof(void *));
  put_u32(f, meta->pid);
  put_u64(f, meta->start_ns);
  put_u32(f, meta->flags);
  put_str(f, meta->exe);

  put_u32(f, (uint32_t)nmods);
  for (size_t i = 0; i < nmods; i++) { put_u64(f, mods[i].base); put_str(f, mods[i].path); }

  put_u32(f, (uint32_t)nsyms);
  for (size_t i = 0; i < nsyms; i++) {
    put_u32(f, syms[i].module); put_u64(f, syms[i].offset); put_str(f, syms[i].name);
  }

  put_u32(f, (uint32_t)nbufs);
  for (size_t bi = 0; bi < nbufs && !failed; bi++) {
    const ct_buffer *b = bufs[bi];
    put_u32(f, (b != NULL && b->count) ? b->data[0].tid : 0);
    put_u32(f, (b != NULL) ? (uint32_t)b->count : 0);
    uint64_t prev_ts = 0;
    for (size_t i = 0; b != NULL && i < b->count; i++) {
      const ct_event *e = &b->data[i];
      long id = lookup_addr(index, nindex, (uint64_t)e->fn);
      if (id < 0) {
        long ei = lookup_extra(extra, nextra, (uint64_t)e->fn);
        if (ei >= 0) {
          id = (long)(nsyms + (size_t)ei);
        } else {
          id = (long)(nsyms + nextra);
          if (nextra == capextra) {
            size_t nc = capextra ? capextra * 2 : 8;
            ct_trace_symbol *ne = (ct_trace_symbol *)realloc(extra, nc * sizeof(*ne));
            if (ne == NULL) { failed = 1; break; }
            extra = ne;
            capextra = nc;
          }
          memset(&extra[nextra], 0, sizeof(extra[nextra]));
          extra[nextra].module = CT_UNKNOWN_MODULE;
          extra[nextra].offset = (uint64_t)e->fn;
          nextra++;
        }
      }
      put_varint(f, (uint64_t)id);
      put_varint(f, i == 0 ? e->ts : e->ts - prev_ts);
      prev_ts = e->ts;
      uint8_t fl = (uint8_t)(e->kind & 1);
      if (e->call_site) fl |= 2;
      put_u8(f, fl);
      if (fl & 2) put_u64(f, (uint64_t)e->call_site);
      total++;
    }
    if (b != NULL) dropped += (uint32_t)b->dropped;
  }

  if (!failed) {
    put_u32(f, (uint32_t)nextra);
    for (size_t i = 0; i < nextra; i++) {
      put_u32(f, extra[i].module); put_u64(f, extra[i].offset); put_str(f, extra[i].name);
    }
    put_u32(f, total);
    put_u32(f, dropped);
    fwrite(CT_MAGIC_END, 1, 4, f);
  }

  free(index);
  free(extra);
  if (failed || ferror(f)) { fclose(f); return -1; }
  if (fflush(f) != 0) { fclose(f); return -1; }
  if (fclose(f) != 0) return -1;
  return 0;
}

CT_NOINSTR static int get_u8(FILE *f, uint8_t *v) { int c = fgetc(f); if (c < 0) return -1; *v = (uint8_t)c; return 0; }
CT_NOINSTR static int get_u16(FILE *f, uint16_t *v) { uint8_t b[2]; if (fread(b, 1, 2, f) != 2) return -1; *v = (uint16_t)(b[0] | (b[1] << 8)); return 0; }
CT_NOINSTR static int get_u32(FILE *f, uint32_t *v) { uint8_t b[4]; if (fread(b, 1, 4, f) != 4) return -1; *v = 0; for (int i = 0; i < 4; i++) *v |= (uint32_t)b[i] << (8 * i); return 0; }
CT_NOINSTR static int get_u64(FILE *f, uint64_t *v) { uint8_t b[8]; if (fread(b, 1, 8, f) != 8) return -1; *v = 0; for (int i = 0; i < 8; i++) *v |= (uint64_t)b[i] << (8 * i); return 0; }
CT_NOINSTR static int get_str(FILE *f, char *out, size_t cap) {
  uint32_t n;
  if (get_u32(f, &n)) return -1;
  if (n >= cap) return -1;
  if (n && fread(out, 1, n, f) != n) return -1;
  out[n] = '\0';
  return 0;
}
CT_NOINSTR static int get_varint(FILE *f, uint64_t *v) {
  *v = 0;
  int shift = 0;
  for (;;) {
    uint8_t b;
    if (get_u8(f, &b)) return -1;
    if (shift == 63 && (b & 0x7f) > 1) return -1;
    *v |= (uint64_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) break;
    shift += 7;
    if (shift > 63) return -1;
  }
  return 0;
}

CT_NOINSTR static void reader_reset(ct_trace_reader *r) {
  if (r == NULL) return;
  if (r->threads != NULL) {
    for (uint32_t i = 0; i < r->n_threads; i++) free(r->threads[i].events);
  }
  free(r->threads);
  free(r->symbols);
  free(r->modules);
  memset(r, 0, sizeof(*r));
}

CT_NOINSTR CTIMING_HIDDEN int ct_trace_open(const char *path, ct_trace_reader *r) {
  if (path == NULL || r == NULL) return -1;
  memset(r, 0, sizeof(*r));
  FILE *f = fopen(path, "rb");
  if (f == NULL) return -1;

  int rc = -2;
  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CT_MAGIC, 4) != 0) goto done;

  uint16_t ver; uint8_t endian, ptr;
  if (get_u16(f, &ver) || get_u8(f, &endian) || get_u8(f, &ptr)) goto done;
  if (ver != CT_VERSION || endian != 1) { rc = -3; goto done; }

  if (get_u32(f, &r->header.pid) || get_u64(f, &r->header.start_ns) ||
      get_u32(f, &r->header.flags) || get_str(f, r->header.exe, CT_PATH_MAX)) goto done;

  uint32_t n;
  if (get_u32(f, &n)) goto done;
  r->n_modules = n;
  if (n) {
    r->modules = (ct_trace_module *)calloc(n, sizeof(ct_trace_module));
    if (r->modules == NULL) goto done;
  }
  for (uint32_t i = 0; i < n; i++) {
    if (get_u64(f, &r->modules[i].base) || get_str(f, r->modules[i].path, CT_PATH_MAX)) goto done;
  }

  if (get_u32(f, &n)) goto done;
  r->n_symbols = n;
  if (n) {
    r->symbols = (ct_trace_symbol *)calloc(n, sizeof(ct_trace_symbol));
    if (r->symbols == NULL) goto done;
  }
  for (uint32_t i = 0; i < n; i++) {
    uint32_t m;
    if (get_u32(f, &m) || get_u64(f, &r->symbols[i].offset) ||
        get_str(f, r->symbols[i].name, sizeof(r->symbols[i].name))) goto done;
    if (m != CT_UNKNOWN_MODULE && m >= r->n_modules) goto done;
    r->symbols[i].module = m;
  }

  if (get_u32(f, &n)) goto done;
  r->n_threads = n;
  if (n) {
    r->threads = (ct_trace_thread *)calloc(n, sizeof(ct_trace_thread));
    if (r->threads == NULL) goto done;
  }
  for (uint32_t ti = 0; ti < n; ti++) {
    uint32_t tid, ne;
    if (get_u32(f, &tid) || get_u32(f, &ne)) goto done;
    r->threads[ti].tid = tid;
    r->threads[ti].n_events = ne;
    if (ne) {
      r->threads[ti].events = (ct_trace_event *)calloc(ne, sizeof(ct_trace_event));
      if (r->threads[ti].events == NULL) goto done;
    }
    uint64_t ts = 0;
    for (uint32_t i = 0; i < ne; i++) {
      uint64_t fid, delta; uint8_t fl;
      if (get_varint(f, &fid) || get_varint(f, &delta) || get_u8(f, &fl)) goto done;
      if (i) ts += delta; else ts = delta;
      ct_trace_event *ev = &r->threads[ti].events[i];
      ev->fn_id = (uint32_t)fid;
      ev->ts = ts;
      ev->kind = (uint8_t)(fl & 1);
      ev->has_call_site = (fl & 2) ? 1 : 0;
      if (fl & 2) { if (get_u64(f, &ev->call_site)) goto done; }
    }
  }

  uint32_t nextra;
  if (get_u32(f, &nextra)) goto done;
  if (nextra) {
    uint64_t merged = (uint64_t)r->n_symbols + nextra;
    if (merged > (uint64_t)UINT32_MAX) goto done;
    if (merged > (uint64_t)(SIZE_MAX / sizeof(ct_trace_symbol))) goto done;
    ct_trace_symbol *ns = (ct_trace_symbol *)realloc(r->symbols, (size_t)merged * sizeof(ct_trace_symbol));
    if (ns == NULL) goto done;
    r->symbols = ns;
    for (uint32_t i = 0; i < nextra; i++) {
      ct_trace_symbol *dst = &r->symbols[r->n_symbols];
      uint32_t m; uint64_t off;
      if (get_u32(f, &m) || get_u64(f, &off) || get_str(f, dst->name, sizeof(dst->name))) goto done;
      if (m != CT_UNKNOWN_MODULE && m >= r->n_modules) goto done;
      dst->module = m;
      dst->offset = off;
      r->n_symbols++;
    }
  }

  for (uint32_t ti = 0; ti < r->n_threads; ti++) {
    for (uint32_t i = 0; i < r->threads[ti].n_events; i++) {
      if (r->threads[ti].events[i].fn_id >= r->n_symbols) goto done;
    }
  }

  if (get_u32(f, &r->total_events) || get_u32(f, &r->dropped)) goto done;
  char me[4];
  if (fread(me, 1, 4, f) != 4 || memcmp(me, CT_MAGIC_END, 4) != 0) goto done;

  rc = 0;
done:
  fclose(f);
  if (rc != 0) reader_reset(r);
  return rc;
}

CT_NOINSTR CTIMING_HIDDEN void ct_trace_close(ct_trace_reader *r) {
  reader_reset(r);
}
