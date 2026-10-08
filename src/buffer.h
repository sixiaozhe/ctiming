#ifndef CT_BUFFER_H
#define CT_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include "ct_common.h"

typedef enum { CT_EV_ENTER = 0, CT_EV_EXIT = 1 } ct_event_kind;

typedef struct {
  uint32_t tid;
  uint8_t  kind;
  uint64_t ts;
  uintptr_t fn;
  uintptr_t call_site;
} ct_event;

typedef struct ct_buffer {
  ct_event *data;
  size_t count;
  size_t cap;
  size_t max_cap;
  size_t dropped;
  int truncated;
  struct ct_buffer *next;
} ct_buffer;

CTIMING_HIDDEN ct_buffer *ct_buffer_new(size_t initial_cap);
CTIMING_HIDDEN void ct_buffer_free(ct_buffer *b);
CTIMING_HIDDEN void ct_buffer_set_max(ct_buffer *b, size_t max_cap);
CTIMING_HIDDEN int ct_buffer_push(ct_buffer *b, ct_event ev);

#endif
