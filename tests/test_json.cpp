#include "check.h"
#include "json.hpp"
#include <cstdint>
#include <limits>
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

  JsonWriter w_empty_obj;
  w_empty_obj.begin_object();
  w_empty_obj.end_object();
  CHECK(w_empty_obj.str_out() == "{}");

  JsonWriter w_empty_arr;
  w_empty_arr.begin_array();
  w_empty_arr.end_array();
  CHECK(w_empty_arr.str_out() == "[]");

  JsonWriter w_arr_obj;
  w_arr_obj.begin_array();
  w_arr_obj.begin_object(); w_arr_obj.key("a"); w_arr_obj.number(-1); w_arr_obj.end_object();
  w_arr_obj.begin_object(); w_arr_obj.key("a"); w_arr_obj.number(2); w_arr_obj.end_object();
  w_arr_obj.end_array();
  CHECK(w_arr_obj.str_out() == "[{\"a\":-1},{\"a\":2}]");

  JsonWriter w_types;
  size_t x = 3;
  w_types.begin_object();
  w_types.key("u"); w_types.number(static_cast<uint64_t>(18446744073709551615ULL));
  w_types.key("z"); w_types.null_value();
  w_types.key("d"); w_types.number(1.5);
  w_types.key("n"); w_types.number(x);
  w_types.end_object();
  CHECK(w_types.str_out() ==
        "{\"u\":18446744073709551615,\"z\":null,\"d\":1.5,\"n\":3}");

  JsonWriter w_nonfinite;
  w_nonfinite.begin_object();
  w_nonfinite.key("i"); w_nonfinite.number(std::numeric_limits<double>::infinity());
  w_nonfinite.key("nan"); w_nonfinite.number(std::numeric_limits<double>::quiet_NaN());
  w_nonfinite.end_object();
  CHECK(w_nonfinite.str_out() == "{\"i\":null,\"nan\":null}");

  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
