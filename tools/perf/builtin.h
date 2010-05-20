#ifndef BUILTIN_H
#define BUILTIN_H

#include "util/util.h"
#include "util/strbuf.h"

extern const char perf_version_string[];
extern const char perf_usage_string[];
extern const char perf_more_info_string[];

extern void list_common_cmds_help(void);
extern const char *help_unknown_cmd(const char *cmd);
extern void prune_packed_objects(int);
extern int read_line_with_nul(char *buf, int size, FILE *file);
extern int check_pager_config(const char *cmd);

extern int cmd_annotate(int argc, const char **argv, const char *prefix);
extern int cmd_bench(int argc, const char **argv, const char *prefix);
extern int cmd_buildid_cache(int argc, const char **argv, const char *prefix);
extern int cmd_buildid_list(int argc, const char **argv, const char *prefix);
extern int cmd_diff(int argc, const char **argv, const char *prefix);
extern int cmd_help(int argc, const char **argv, const char *prefix);
extern int cmd_sched(int argc, const char **argv, const char *prefix);
extern int cmd_list(int argc, const char **argv, const char *prefix);
extern int cmd_record(int argc, const char **argv, const char *prefix);
extern int cmd_report(int argc, const char **argv, const char *prefix);
extern int cmd_stat(int argc, const char **argv, const char *prefix);
extern int cmd_timechart(int argc, const char **argv, const char *prefix);
extern int cmd_top(int argc, const char **argv, const char *prefix);
extern int cmd_trace(int argc, const char **argv, const char *prefix);
extern int cmd_version(int argc, const char **argv, const char *prefix);
extern int cmd_probe(int argc, const char **argv, const char *prefix);
extern int cmd_kmem(int argc, const char **argv, const char *prefix);
extern int cmd_lock(int argc, const char **argv, const char *prefix);
extern int cmd_kvm(int argc, const char **argv, const char *prefix);
extern int cmd_test(int argc, const char **argv, const char *prefix);
extern int cmd_inject(int argc, const char **argv, const char *prefix);

#endif
