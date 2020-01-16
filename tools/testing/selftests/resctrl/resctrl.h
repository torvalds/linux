/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#ifndef RESCTRL_H
#define RESCTRL_H
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>

#define RESCTRL_PATH		"/sys/fs/resctrl"
#define PHYS_ID_PATH		"/sys/devices/system/cpu/cpu"

#define PARENT_EXIT(err_msg)			\
	do {					\
		perror(err_msg);		\
		kill(ppid, SIGKILL);		\
		exit(EXIT_FAILURE);		\
	} while (0)

pid_t bm_pid, ppid;

int remount_resctrlfs(bool mum_resctrlfs);
int get_resource_id(int cpu_no, int *resource_id);
int validate_bw_report_request(char *bw_report);
bool validate_resctrl_feature_request(char *resctrl_val);
char *fgrep(FILE *inf, const char *str);
int taskset_benchmark(pid_t bm_pid, int cpu_no);
void run_benchmark(int signum, siginfo_t *info, void *ucontext);
int write_schemata(char *ctrlgrp, char *schemata, int cpu_no,
		   char *resctrl_val);
int write_bm_pid_to_resctrl(pid_t bm_pid, char *ctrlgrp, char *mongrp,
			    char *resctrl_val);
int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu,
		    int group_fd, unsigned long flags);
int run_fill_buf(unsigned long span, int malloc_and_init_memory, int memflush,
		 int op, char *resctrl_va);

#endif /* RESCTRL_H */
