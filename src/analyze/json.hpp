#pragma once
#include <cstdint>
#include <string>
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
  void number(int v);
  void number(int64_t v);
  void number(uint64_t v);
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
