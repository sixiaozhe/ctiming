#ifndef CT_GLOB_H
#define CT_GLOB_H

#include "ct_common.h"

#ifdef __cplusplus
extern "C" {
#endif

CTIMING_HIDDEN int ct_glob_match(const char *pattern, const char *str);
CTIMING_HIDDEN int ct_filter_match(const char *include, const char *exclude, const char *name);

#ifdef __cplusplus
}
#endif

#endif
