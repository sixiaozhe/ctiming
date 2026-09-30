#ifndef CT_SYMBOLS_H
#define CT_SYMBOLS_H

#include "trace.h"
#include "ct_common.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint64_t addr;
  char name[256];
  uint32_t module;
} ct_sym_entry;

typedef struct {
  ct_trace_module *modules;
  size_t n_modules;
  ct_sym_entry *syms;
  size_t n_symbols;
} ct_symbol_table;

CTIMING_HIDDEN int ct_symbols_load(ct_symbol_table *t);
CTIMING_HIDDEN void ct_symbols_free(ct_symbol_table *t);

CTIMING_HIDDEN const char *ct_symbols_lookup(const ct_symbol_table *t, uintptr_t addr,
                                             uintptr_t *offset, uint32_t *module);

#endif
