#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "trace.h"

namespace ct {

struct ModuleInfo { uint64_t base = 0; std::string path; };

struct SymbolInfo { uint32_t module = 0; uint64_t offset = 0; std::string name; };

struct TraceEvent {
  uint32_t tid = 0;
  uint8_t kind = 0;
  uint64_t ts = 0;
  uint32_t fn_id = 0;
  bool has_call_site = false;
  uint64_t call_site = 0;
};

struct ThreadEvents {
  uint32_t tid = 0;
  std::vector<TraceEvent> events;
};

struct Trace {
  std::string exe;
  uint32_t pid = 0;
  uint32_t flags = 0;
  std::vector<ModuleInfo> modules;
  std::vector<SymbolInfo> symbols;
  uint32_t total_events = 0;
  uint32_t dropped = 0;
  std::vector<ThreadEvents> threads;

  const SymbolInfo *symbol(uint32_t fn_id) const {
    return fn_id < symbols.size() ? &symbols[fn_id] : nullptr;
  }
  std::string name_of(uint32_t fn_id) const;
};

bool load_trace(const std::string &path, Trace &out, std::string &err);

} // namespace ct
