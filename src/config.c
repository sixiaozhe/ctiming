#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CT_NOINSTR static char *dup_env(const char *name) {
  const char *v = getenv(name);
  if (v == NULL || v[0] == '\0') return NULL;
  size_t n = strlen(v) + 1;
  char *p = malloc(n);
  if (p == NULL) return NULL;
  memcpy(p, v, n);
  return p;
}

CT_NOINSTR CTIMING_HIDDEN void ct_config_load(ct_config *c, const char *progname) {
  memset(c, 0, sizeof(*c));
  c->enabled = 1;
  c->buf_kb = 1024;
  c->buf_max_kb = 65536;

  const char *en = getenv("CTIMING_ENABLE");
  if (en != NULL && (strcmp(en, "off") == 0 || strcmp(en, "0") == 0)) c->enabled = 0;

  const char *md = getenv("CTIMING_MAX_DEPTH");
  if (md != NULL) c->max_depth = (unsigned)strtoul(md, NULL, 10);

  const char *bk = getenv("CTIMING_BUF_KB");
  if (bk != NULL) c->buf_kb = (unsigned)strtoul(bk, NULL, 10);

  const char *bm = getenv("CTIMING_BUF_MAX_KB");
  if (bm != NULL) c->buf_max_kb = (unsigned)strtoul(bm, NULL, 10);

  const char *du = getenv("CTIMING_DROP_UNKNOWN");
  if (du != NULL && strcmp(du, "1") == 0) c->drop_unknown = 1;

  c->include = dup_env("CTIMING_INCLUDE");
  c->exclude = dup_env("CTIMING_EXCLUDE");

  const char *out = getenv("CTIMING_OUT");
  if (out != NULL && out[0] != '\0') {
    snprintf(c->out_path, CT_PATH_MAX, "%s", out);
  } else {
    const char *prog = (progname != NULL) ? progname : "ctiming";
    snprintf(c->out_path, CT_PATH_MAX, "./%s.ctrace", prog);
  }
}

CT_NOINSTR CTIMING_HIDDEN void ct_config_clear(ct_config *c) {
  free(c->include);
  free(c->exclude);
  c->include = NULL;
  c->exclude = NULL;
}
