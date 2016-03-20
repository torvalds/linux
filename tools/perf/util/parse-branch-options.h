#ifndef _PERF_PARSE_BRANCH_OPTIONS_H
#define _PERF_PARSE_BRANCH_OPTIONS_H 1
struct option;
int parse_branch_stack(const struct option *opt, const char *str, int unset);
#endif /* _PERF_PARSE_BRANCH_OPTIONS_H */
