#ifndef CT_TRACE_H
#define CT_TRACE_H

#include "buffer.h"
#include "ct_common.h"
#include <stdint.h>
#include <stddef.h>

#define CT_PATH_MAX 512
#define CT_UNKNOWN_MODULE 0xFFFFFFFFu

typedef struct {
  uint32_t pid;
  uint64_t start_ns;
  uint32_t flags;
  char exe[CT_PATH_MAX];
} ct_trace_meta;

typedef struct {
  uint64_t base;
  char path[CT_PATH_MAX];
} ct_trace_module;

typedef struct {
  uint32_t module;
  uint64_t offset;
  char name[256];
} ct_trace_symbol;

CTIMING_HIDDEN int ct_trace_write(const char *path,
                                  const ct_buffer *const *bufs, size_t nbufs,
                                  const ct_trace_module *mods, size_t nmods,
                                  const ct_trace_symbol *syms, size_t nsyms,
                                  const ct_trace_meta *meta);

typedef struct {
  uint32_t fn_id;
  uint8_t  kind;
  uint64_t ts;
  uint64_t call_site;
  int      has_call_site;
} ct_trace_event;

typedef struct {
  uint32_t tid;
  uint32_t n_events;
  ct_trace_event *events;
} ct_trace_thread;

typedef struct {
  ct_trace_meta header;
  ct_trace_module *modules;
  uint32_t n_modules;
  ct_trace_symbol *symbols;
  uint32_t n_symbols;
  ct_trace_thread *threads;
  uint32_t n_threads;
  uint32_t total_events;
  uint32_t dropped;
} ct_trace_reader;

CTIMING_HIDDEN int ct_trace_open(const char *path, ct_trace_reader *r);
CTIMING_HIDDEN void ct_trace_close(ct_trace_reader *r);

#endif
