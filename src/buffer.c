#include "buffer.h"
#include "ct_common.h"
#include <stdlib.h>

CT_NOINSTR CTIMING_HIDDEN ct_buffer *ct_buffer_new(size_t initial_cap) {
  if (initial_cap < 16) initial_cap = 16;
  ct_buffer *b = calloc(1, sizeof(*b));
  if (b == NULL) return NULL;
  b->data = calloc(initial_cap, sizeof(ct_event));
  if (b->data == NULL) {
    free(b);
    return NULL;
  }
  b->cap = initial_cap;
  b->count = 0;
  b->max_cap = 0;
  b->dropped = 0;
  b->truncated = 0;
  b->next = NULL;
  return b;
}

CT_NOINSTR CTIMING_HIDDEN void ct_buffer_free(ct_buffer *b) {
  if (b == NULL) return;
  free(b->data);
  free(b);
}

CT_NOINSTR CTIMING_HIDDEN void ct_buffer_set_max(ct_buffer *b, size_t max_cap) {
  if (b == NULL) return;
  b->max_cap = max_cap;
}

CT_NOINSTR CTIMING_HIDDEN int ct_buffer_push(ct_buffer *b, ct_event ev) {
  if (b->count == b->cap) {
    if (b->max_cap && b->cap >= b->max_cap) {
      b->truncated = 1;
      b->dropped++;
      return 0;
    }
    size_t new_cap = b->cap * 2;
    ct_event *grown = realloc(b->data, new_cap * sizeof(ct_event));
    if (grown == NULL) {
      b->dropped++;
      return 0;
    }
    b->data = grown;
    b->cap = new_cap;
  }
  b->data[b->count++] = ev;
  return 1;
}
