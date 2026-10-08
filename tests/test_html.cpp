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
  std::string evil = "{\"functions\":[{\"name\":\"</script><x>\"}]}";
  std::string h2 = ct::to_html(evil);
  CHECK(h2.find("</script><x>") == std::string::npos);
  CHECK(h2.find("<\\/script>") != std::string::npos);
  if (fails) fprintf(stderr, "%d checks failed\n", fails);
  return fails ? 1 : 0;
}
