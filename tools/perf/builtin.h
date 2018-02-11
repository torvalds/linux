/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BUILTIN_H
#define BUILTIN_H

#include "util/util.h"

extern const char perf_usage_string[];
extern const char perf_more_info_string[];

void list_common_cmds_help(void);
const char *help_unknown_cmd(const char *cmd);

int cmd_annotate(int argc, const char **argv);
int cmd_bench(int argc, const char **argv);
int cmd_buildid_cache(int argc, const char **argv);
int cmd_buildid_list(int argc, const char **argv);
int cmd_config(int argc, const char **argv);
int cmd_c2c(int argc, const char **argv);
int cmd_diff(int argc, const char **argv);
int cmd_evlist(int argc, const char **argv);
int cmd_help(int argc, const char **argv);
int cmd_sched(int argc, const char **argv);
int cmd_kallsyms(int argc, const char **argv);
int cmd_list(int argc, const char **argv);
int cmd_record(int argc, const char **argv);
int cmd_report(int argc, const char **argv);
int cmd_stat(int argc, const char **argv);
int cmd_timechart(int argc, const char **argv);
int cmd_top(int argc, const char **argv);
int cmd_script(int argc, const char **argv);
int cmd_version(int argc, const char **argv);
int cmd_probe(int argc, const char **argv);
int cmd_kmem(int argc, const char **argv);
int cmd_lock(int argc, const char **argv);
int cmd_kvm(int argc, const char **argv);
int cmd_test(int argc, const char **argv);
int cmd_trace(int argc, const char **argv);
int cmd_inject(int argc, const char **argv);
int cmd_mem(int argc, const char **argv);
int cmd_data(int argc, const char **argv);
int cmd_ftrace(int argc, const char **argv);

int find_scripts(char **scripts_array, char **scripts_path_array);
#endif
