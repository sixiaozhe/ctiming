#include "html.hpp"
#include "viewer_assets.hpp"
#include <string>

namespace ct {

static std::string escape_script(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '<') {
      out += "\\u003c";
    } else if (s[i] == '>') {
      out += "\\u003e";
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string to_html(const std::string &json) {
  std::string h;
  h += "<!DOCTYPE html>\n<html lang=\"zh\"><head><meta charset=\"utf-8\">";
  h += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  h += "<title>ctiming report</title>\n<style>";
  h += assets::css_viewer;
  h += "</style></head>\n<body>\n<header><h1>ctiming report</h1><span class=\"meta\" id=\"meta\"></span></header>\n";
  h += "<nav id=\"nav\"></nav>\n<main id=\"view\"></main>\n";
  h += "<script id=\"ct-data\" type=\"application/json\">";
  h += escape_script(json);
  h += "</script>\n";
  h += "<script>";
  h += assets::js_core;
  h += "</script>\n<script>";
  h += assets::js_overview;
  h += "</script>\n<script>";
  h += assets::js_flame;
  h += "</script>\n<script>";
  h += assets::js_graph;
  h += "</script>\n<script>";
  h += assets::js_trace;
  h += "</script>\n<script>";
  h += assets::js_callers;
  h += "</script>\n<script>";
  h += assets::js_main;
  h += "</script>\n</body></html>\n";
  return h;
}

}
