#ifndef CT_CHECK_H
#define CT_CHECK_H
#include <stdio.h>
#define CHECK(cond) do { \
  if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } \
} while (0)
#define CHECK_EQ_LONG(a, b) do { \
  long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { fprintf(stderr, "FAIL %s:%d: %s=%lld != %s=%lld\n", __FILE__, __LINE__, #a, _a, #b, _b); fails++; } \
} while (0)
#endif
