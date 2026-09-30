#ifndef CT_CONFIG_H
#define CT_CONFIG_H

#include "trace.h"
#include "ct_common.h"

typedef struct {
  int enabled;
  unsigned max_depth;
  char *include;
  char *exclude;
  char out_path[CT_PATH_MAX];
} ct_config;

CTIMING_HIDDEN void ct_config_load(ct_config *c, const char *progname);
CTIMING_HIDDEN void ct_config_clear(ct_config *c);

#endif
