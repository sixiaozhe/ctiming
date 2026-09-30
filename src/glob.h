#ifndef CT_GLOB_H
#define CT_GLOB_H

int ct_glob_match(const char *pattern, const char *str);
int ct_filter_match(const char *include, const char *exclude, const char *name);

#endif
