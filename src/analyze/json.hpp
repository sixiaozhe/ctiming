#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace ct {

std::string json_escape(const std::string &s);

class JsonWriter {
public:
  void begin_object();
  void end_object();
  void begin_array();
  void end_array();
  void key(const std::string &k);
  template <class T,
            typename std::enable_if<std::is_integral<T>::value &&
                                        !std::is_same<T, bool>::value,
                                    int>::type = 0>
  void number(T v) {
    comma();
    out_ += std::to_string(v);
  }
  void number(double v);
  void boolean(bool b);
  void str(const std::string &s);
  void null_value();
  const std::string &str_out() const { return out_; }

private:
  std::vector<bool> first_;
  std::string out_;
  void comma();
};

} // namespace ct
