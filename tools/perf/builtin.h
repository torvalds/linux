/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BUILTIN_H
#define BUILTIN_H

struct feature_status {
	const char *name;
	const char *macro;
	const char *tip;
	int is_builtin;
};

extern struct feature_status supported_features[];

void feature_status__printf(const struct feature_status *feature);

struct cmdnames;

void list_common_cmds_help(void);
const char *help_unknown_cmd(const char *cmd, struct cmdnames *main_cmds);

int cmd_annotate(int argc, const char **argv);
int cmd_bench(int argc, const char **argv);
int cmd_buildid_cache(int argc, const char **argv);
int cmd_buildid_list(int argc, const char **argv);
int cmd_check(int argc, const char **argv);
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
int cmd_daemon(int argc, const char **argv);
int cmd_kwork(int argc, const char **argv);

#endif
