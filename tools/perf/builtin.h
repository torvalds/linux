#ifndef BUILTIN_H
#define BUILTIN_H

#include "util/util.h"
#include "util/strbuf.h"

extern const char perf_usage_string[];
extern const char perf_more_info_string[];

void list_common_cmds_help(void);
const char *help_unknown_cmd(const char *cmd);
void prune_packed_objects(int);
int read_line_with_nul(char *buf, int size, FILE *file);
int check_pager_config(const char *cmd);

int cmd_annotate(int argc, const char **argv, const char *prefix);
int cmd_bench(int argc, const char **argv, const char *prefix);
int cmd_buildid_cache(int argc, const char **argv, const char *prefix);
int cmd_buildid_list(int argc, const char **argv, const char *prefix);
int cmd_config(int argc, const char **argv, const char *prefix);
int cmd_c2c(int argc, const char **argv, const char *prefix);
int cmd_diff(int argc, const char **argv, const char *prefix);
int cmd_evlist(int argc, const char **argv, const char *prefix);
int cmd_help(int argc, const char **argv, const char *prefix);
int cmd_sched(int argc, const char **argv, const char *prefix);
int cmd_kallsyms(int argc, const char **argv, const char *prefix);
int cmd_list(int argc, const char **argv, const char *prefix);
int cmd_record(int argc, const char **argv, const char *prefix);
int cmd_report(int argc, const char **argv, const char *prefix);
int cmd_stat(int argc, const char **argv, const char *prefix);
int cmd_timechart(int argc, const char **argv, const char *prefix);
int cmd_top(int argc, const char **argv, const char *prefix);
int cmd_script(int argc, const char **argv, const char *prefix);
int cmd_version(int argc, const char **argv, const char *prefix);
int cmd_probe(int argc, const char **argv, const char *prefix);
int cmd_kmem(int argc, const char **argv, const char *prefix);
int cmd_lock(int argc, const char **argv, const char *prefix);
int cmd_kvm(int argc, const char **argv, const char *prefix);
int cmd_test(int argc, const char **argv, const char *prefix);
int cmd_trace(int argc, const char **argv, const char *prefix);
int cmd_inject(int argc, const char **argv, const char *prefix);
int cmd_mem(int argc, const char **argv, const char *prefix);
int cmd_data(int argc, const char **argv, const char *prefix);
int cmd_ftrace(int argc, const char **argv, const char *prefix);

int find_scripts(char **scripts_array, char **scripts_path_array);
#endif
