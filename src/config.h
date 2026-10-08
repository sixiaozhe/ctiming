#ifndef CT_CONFIG_H
#define CT_CONFIG_H

#include "trace.h"
#include "ct_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int enabled;
  unsigned max_depth;
  unsigned buf_kb;
  unsigned buf_max_kb;
  int drop_unknown;
  int exclude_lib;
  char *include;
  char *exclude;
  char out_path[CT_PATH_MAX];
} ct_config;

CTIMING_HIDDEN void ct_config_load(ct_config *c, const char *progname);
CTIMING_HIDDEN void ct_config_clear(ct_config *c);
CTIMING_HIDDEN int ct_lib_name_match(const char *name);

#ifdef __cplusplus
}
#endif

#endif
