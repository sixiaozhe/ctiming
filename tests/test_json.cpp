#include "check.h"
#include "json.hpp"
#include <string>

using namespace ct;

int main() {
  int fails = 0;
  CHECK(json_escape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
  CHECK(json_escape("\x01") == "\\u0001");
  CHECK(json_escape("中文") == "中文");

  JsonWriter w;
  w.begin_object();
  w.key("n"); w.number(42);
  w.key("s"); w.str("hi");
  w.key("b"); w.boolean(true);
  w.key("arr"); w.begin_array(); w.number(1); w.number(2); w.end_array();
  w.end_object();
  std::string out = w.str_out();
  CHECK(out == "{\"n\":42,\"s\":\"hi\",\"b\":true,\"arr\":[1,2]}");

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
