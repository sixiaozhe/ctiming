#include "json.hpp"
#include <cstdio>

namespace ct {

std::string json_escape(const std::string &s) {
  std::string r;
  r.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
    case '"': r += "\\\""; break;
    case '\\': r += "\\\\"; break;
    case '\n': r += "\\n"; break;
    case '\r': r += "\\r"; break;
    case '\t': r += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        r += buf;
      } else {
        r += static_cast<char>(c);
      }
    }
  }
  return r;
}

void JsonWriter::comma() {
  if (first_.empty()) return;
  if (first_.back()) first_.back() = false;
  else out_ += ',';
}

void JsonWriter::begin_object() {
  comma();
  out_ += '{';
  first_.push_back(true);
}

void JsonWriter::end_object() {
  out_ += '}';
  first_.pop_back();
}

void JsonWriter::begin_array() {
  comma();
  out_ += '[';
  first_.push_back(true);
}

void JsonWriter::end_array() {
  out_ += ']';
  first_.pop_back();
}

void JsonWriter::key(const std::string &k) {
  comma();
  out_ += '"';
  out_ += json_escape(k);
  out_ += "\":";
  first_.back() = true;
}

void JsonWriter::number(int v) { number(static_cast<int64_t>(v)); }

void JsonWriter::number(int64_t v) {
  comma();
  char buf[32];
  std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
  out_ += buf;
}

void JsonWriter::number(uint64_t v) {
  comma();
  char buf[32];
  std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v));
  out_ += buf;
}

void JsonWriter::number(double v) {
  comma();
  char buf[32];
  std::snprintf(buf, sizeof buf, "%g", v);
  out_ += buf;
}

void JsonWriter::boolean(bool b) {
  comma();
  out_ += b ? "true" : "false";
}

void JsonWriter::str(const std::string &s) {
  comma();
  out_ += '"';
  out_ += json_escape(s);
  out_ += '"';
}

void JsonWriter::null_value() {
  comma();
  out_ += "null";
}

} // namespace ct
