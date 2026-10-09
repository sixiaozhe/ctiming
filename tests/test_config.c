#include "check.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
  int fails = 0;
  unsetenv("CTIMING_ENABLE"); unsetenv("CTIMING_MAX_DEPTH"); unsetenv("CTIMING_OUT");
  unsetenv("CTIMING_INCLUDE"); unsetenv("CTIMING_EXCLUDE"); unsetenv("CTIMING_EXCLUDE_LIB");
  unsetenv("CTIMING_BUF_KB"); unsetenv("CTIMING_BUF_MAX_KB"); unsetenv("CTIMING_DROP_UNKNOWN");
  unsetenv("CTIMING_CTL"); unsetenv("CTIMING_TRACE");
  ct_config c;
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 1);
  CHECK_EQ_LONG(c.max_depth, 0);
  CHECK_EQ_LONG(c.buf_kb, 1024);
  CHECK_EQ_LONG(c.buf_max_kb, 65536);
  CHECK_EQ_LONG(c.drop_unknown, 0);
  CHECK_EQ_LONG(c.exclude_lib, 1);
  CHECK(c.include == NULL && c.exclude == NULL);
  CHECK(c.ctl_path == NULL && c.trace_pattern == NULL);
  CHECK(strcmp(c.out_path, "./myprog.ctrace") == 0);

  setenv("CTIMING_ENABLE", "off", 1);
  setenv("CTIMING_MAX_DEPTH", "5", 1);
  setenv("CTIMING_BUF_KB", "64", 1);
  setenv("CTIMING_BUF_MAX_KB", "256", 1);
  setenv("CTIMING_DROP_UNKNOWN", "1", 1);
  setenv("CTIMING_EXCLUDE_LIB", "off", 1);
  setenv("CTIMING_INCLUDE", "foo*,bar*", 1);
  setenv("CTIMING_EXCLUDE", "mine::*", 1);
  setenv("CTIMING_CTL", "/tmp/x.fifo", 1);
  setenv("CTIMING_TRACE", "demo::hot", 1);
  setenv("CTIMING_OUT", "/tmp/x.ctrace", 1);
  ct_config_load(&c, "myprog");
  CHECK_EQ_LONG(c.enabled, 0);
  CHECK_EQ_LONG(c.max_depth, 5);
  CHECK_EQ_LONG(c.buf_kb, 64);
  CHECK_EQ_LONG(c.buf_max_kb, 256);
  CHECK_EQ_LONG(c.drop_unknown, 1);
  CHECK_EQ_LONG(c.exclude_lib, 0);
  CHECK(c.include && strcmp(c.include, "foo*,bar*") == 0);
  CHECK(c.exclude && strcmp(c.exclude, "mine::*") == 0);
  CHECK(c.ctl_path && strcmp(c.ctl_path, "/tmp/x.fifo") == 0);
  CHECK(c.trace_pattern && strcmp(c.trace_pattern, "demo::hot") == 0);
  CHECK(strcmp(c.out_path, "/tmp/x.ctrace") == 0);
  ct_config_clear(&c);

  CHECK_EQ_LONG(ct_lib_name_match("std::vector<int>::push_back(int)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("void std::this_thread::sleep_for<long, std::ratio<1l, 1000l> >(std::chrono::duration<long, std::ratio<1l, 1000l> > const&)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("bool __gnu_cxx::operator!=<double const*>(double const*, double const*)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("operator new(unsigned long)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("demo::fib(int)"), 0);
  CHECK_EQ_LONG(ct_lib_name_match("mystd::foo(int)"), 0);
  CHECK_EQ_LONG(ct_lib_name_match("double demo::reduce_sum<double>(std::vector<double, std::allocator<double> > const&)"), 0);
  CHECK_EQ_LONG(ct_lib_name_match("void (*&&std::forward<void (*)(int, int)>(std::remove_reference<void (*)(int, int)>::type&))(int, int)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("void (&std::forward<void (&)(int, int)>(std::remove_reference<void (&)(int, int)>::type&))(int, int)"), 1);
  CHECK_EQ_LONG(ct_lib_name_match("std::vector<int> demo::make()"), 0);
  CHECK_EQ_LONG(ct_lib_name_match("std::string demo::to_string(int)"), 0);
  CHECK_EQ_LONG(ct_lib_name_match("void (*demo::make_fn())(int)"), 0);
  CHECK_EQ_LONG(ct_lib_name_match(NULL), 0);

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
