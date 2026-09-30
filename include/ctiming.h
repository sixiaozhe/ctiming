#ifndef CTIMING_H
#define CTIMING_H

#ifdef __cplusplus
extern "C" {
#endif

void ctiming_start(void);
void ctiming_stop(void);
int  ctiming_dump(const char *path);
void ctiming_set_filter(const char *include, const char *exclude);
void ctiming_set_max_depth(unsigned depth);
const char *ctiming_version(void);

#ifdef __cplusplus
}
#endif

#endif
