#include "glob.h"
#include "ct_common.h"
#include <stddef.h>
#include <string.h>

static CT_NOINSTR int ct_glob_core(const char *pat, size_t plen, const char *str, size_t slen) {
  size_t pi = 0;
  size_t si = 0;
  size_t star = (size_t)-1;
  size_t ss = 0;
  while (si < slen) {
    if (pi < plen && pat[pi] == '*') {
      star = pi++;
      ss = si;
    } else if (pi < plen && (pat[pi] == '?' || pat[pi] == str[si])) {
      pi++;
      si++;
    } else if (star != (size_t)-1) {
      pi = star + 1;
      si = ++ss;
    } else {
      return 0;
    }
  }
  while (pi < plen && pat[pi] == '*') pi++;
  return pi == plen;
}

CT_NOINSTR CTIMING_HIDDEN int ct_glob_match(const char *pattern, const char *str) {
  if (pattern == NULL || str == NULL) return 0;
  return ct_glob_core(pattern, strlen(pattern), str, strlen(str));
}

static CT_NOINSTR int ct_list_match(const char *list, const char *name) {
  if (list == NULL || name == NULL) return 0;
  size_t nlen = strlen(name);
  const char *p = list;
  while (*p) {
    const char *end = p;
    while (*end && *end != ',') end++;
    const char *b = p;
    const char *e = end;
    while (b < e && (*b == ' ' || *b == '\t')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t')) e--;
    if (b < e && ct_glob_core(b, (size_t)(e - b), name, nlen)) return 1;
    p = (*end == ',') ? end + 1 : end;
  }
  return 0;
}

CT_NOINSTR CTIMING_HIDDEN int ct_filter_match(const char *include, const char *exclude, const char *name) {
  if (ct_list_match(exclude, name)) return 0;
  if (include == NULL || *include == '\0') return 1;
  return ct_list_match(include, name) ? 1 : 0;
}
