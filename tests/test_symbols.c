#include "check.h"
#include "symbols.h"
#include <string.h>

static void dummy_target(void) { }

int main(void) {
  int fails = 0;
  ct_symbol_table t;
  CHECK_EQ_LONG(ct_symbols_load(&t), 0);
  CHECK(t.n_modules >= 1);
  CHECK(t.n_symbols >= 1);

  uintptr_t off = (uintptr_t)-1;
  uint32_t mod = 0xFFFFFFFFu;
  const char *name = ct_symbols_lookup(&t, (uintptr_t)&main, &off, &mod);
  CHECK(name != NULL);
  if (name) CHECK(strstr(name, "main") != NULL);
  CHECK(mod < t.n_modules);
  CHECK(off < 65536);

  const char *dt = ct_symbols_lookup(&t, (uintptr_t)&dummy_target, NULL, NULL);
  CHECK(dt != NULL);
  if (dt) CHECK(strstr(dt, "dummy_target") != NULL);

  off = 123;
  mod = 0xFFFFFFFFu;
  CHECK_EQ_LONG(ct_symbols_lookup(&t, (uintptr_t)1, &off, &mod), 0);
  CHECK_EQ_LONG(off, 123);
  CHECK_EQ_LONG(mod, 0xFFFFFFFFu);

  ct_symbols_free(&t);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
