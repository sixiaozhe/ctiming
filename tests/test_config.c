#include "check.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
  int fails = 0;
  unsetenv("CTIMING_ENABLE"); unsetenv("CTIMING_MAX_DEPTH"); unsetenv("CTIMING_OUT");
  unsetenv("CTIMING_INCLUDE"); unsetenv("CTIMING_EXCLUDE");
  ct_config c;
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 1);
  CHECK_EQ_LONG(c.max_depth, 0);
  CHECK(c.include == NULL && c.exclude == NULL);
  CHECK(strcmp(c.out_path, "./myprog.ctrace") == 0);

  setenv("CTIMING_ENABLE", "off", 1);
  setenv("CTIMING_MAX_DEPTH", "5", 1);
  setenv("CTIMING_INCLUDE", "foo*,bar*", 1);
  setenv("CTIMING_EXCLUDE", "std::*", 1);
  setenv("CTIMING_OUT", "/tmp/x.ctrace", 1);
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 0);
  CHECK_EQ_LONG(c.max_depth, 5);
  CHECK(c.include && strcmp(c.include, "foo*,bar*") == 0);
  CHECK(c.exclude && strcmp(c.exclude, "std::*") == 0);
  CHECK(strcmp(c.out_path, "/tmp/x.ctrace") == 0);
  ct_config_clear(&c);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
