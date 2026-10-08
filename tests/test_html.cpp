#include "check.h"
#include "html.hpp"
#include <string>

int main() {
  int fails = 0;
  std::string js = "{\"trace\":{\"pid\":1},\"functions\":[],\"call_graph\":[],\"aggregated\":[],\"threads\":[],\"instances\":[]}";
  std::string h = ct::to_html(js);
  CHECK(h.rfind("<!DOCTYPE html>", 0) == 0);
  CHECK(h.find("id=\"ct-data\"") != std::string::npos);
  CHECK(h.find("\"pid\":1") != std::string::npos);
  CHECK(h.find("CT.init") != std::string::npos);
  CHECK(h.find("</html>") != std::string::npos);
  CHECK(h.find("--accent") != std::string::npos);
  CHECK(h.find("CT.registerTab") != std::string::npos);
  CHECK(h.find("概览") != std::string::npos);
  CHECK(h.find("火焰图") != std::string::npos);
  CHECK(h.find("调用关系图") != std::string::npos);
  CHECK(h.find("单次追踪") != std::string::npos);
  CHECK(h.find("调用者/被调用者") != std::string::npos);
  std::string evil = "{\"functions\":[{\"name\":\"</script><x>\"}]}";
  std::string h2 = ct::to_html(evil);
  CHECK(h2.find("</script><x>") == std::string::npos);
  CHECK(h2.find("\\u003c/script") != std::string::npos);
  std::string comment = "{\"functions\":[{\"name\":\"<!--\"}]}";
  std::string h3 = ct::to_html(comment);
  CHECK(h3.find("<!--") == std::string::npos);
  CHECK(h3.find("\\u003c!--") != std::string::npos);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
