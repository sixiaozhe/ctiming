#include "check.h"
#include "glob.h"

int main(void) {
  int fails = 0;
  CHECK_EQ_LONG(ct_glob_match("foo*", "foobar"), 1);
  CHECK_EQ_LONG(ct_glob_match("foo*", "barfoo"), 0);
  CHECK_EQ_LONG(ct_glob_match("*bar", "foobar"), 1);
  CHECK_EQ_LONG(ct_glob_match("f?o", "foo"), 1);
  CHECK_EQ_LONG(ct_glob_match("f?o", "fooo"), 0);
  CHECK_EQ_LONG(ct_glob_match("*", "anything"), 1);
  CHECK_EQ_LONG(ct_glob_match("std::*", "std::vector"), 1);
  CHECK_EQ_LONG(ct_glob_match("std::*", "std::"), 1);
  CHECK_EQ_LONG(ct_glob_match("a*b*c", "aXXbYYc"), 1);
  CHECK_EQ_LONG(ct_glob_match("a*b*c", "aXXcYYb"), 0);

  CHECK_EQ_LONG(ct_filter_match(NULL, NULL, "foo"), 1);
  CHECK_EQ_LONG(ct_filter_match("foo*,bar*", NULL, "foobar"), 1);
  CHECK_EQ_LONG(ct_filter_match("foo*,bar*", NULL, "baz"), 0);
  CHECK_EQ_LONG(ct_filter_match(NULL, "*std::*", "std::vector"), 0);
  CHECK_EQ_LONG(ct_filter_match("foo*", "foo_bar*", "foo_bar_x"), 0);
  CHECK_EQ_LONG(ct_filter_match("foo*", "foo_bar*", "foo_qux"), 1);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
