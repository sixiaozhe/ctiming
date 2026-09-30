#include "check.h"
#include "ctiming.h"
#include <string.h>

int main(void) {
  int fails = 0;
  const char *v = ctiming_version();
  CHECK(v != NULL);
  CHECK(strlen(v) > 0);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
