#include "check.h"
#include "buffer.h"

int main(void) {
  int fails = 0;
  ct_buffer *b = ct_buffer_new(2);
  CHECK(b != NULL);
  CHECK_EQ_LONG(b->count, 0);
  CHECK_EQ_LONG(b->dropped, 0);

  ct_event e = { .tid = 7, .ts = 100, .fn = 0x1000, .call_site = 0, .kind = CT_EV_ENTER };
  ct_buffer_push(b, e);
  e.ts = 200; e.kind = CT_EV_EXIT;
  ct_buffer_push(b, e);
  CHECK_EQ_LONG(b->count, 2);
  CHECK_EQ_LONG(b->data[0].fn, 0x1000);
  CHECK_EQ_LONG(b->data[0].kind, CT_EV_ENTER);
  CHECK_EQ_LONG(b->data[1].ts, 200);

  size_t cap_before = b->cap;
  for (int i = 0; i < 200; i++) { e.ts = 1000 + (uint64_t)i; ct_buffer_push(b, e); }
  CHECK_EQ_LONG(b->count, 202);
  CHECK(b->cap > cap_before);
  CHECK_EQ_LONG(b->data[201].ts, 1199);

  ct_buffer_free(b);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
