/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_PMU_EVENT_H
#define _SELFTESTS_POWERPC_PMU_EVENT_H

#include <unistd.h>
#include <linux/perf_event.h>

#include "utils.h"


struct event {
	struct perf_event_attr attr;
	char *name;
	int fd;
	/* This must match the read_format we use */
	struct {
		u64 value;
		u64 running;
		u64 enabled;
	} result;
};

void event_init(struct event *e, u64 config);
void event_init_named(struct event *e, u64 config, char *name);
void event_init_opts(struct event *e, u64 config, int type, char *name);
int event_open_with_options(struct event *e, pid_t pid, int cpu, int group_fd);
int event_open_with_group(struct event *e, int group_fd);
int event_open_with_pid(struct event *e, pid_t pid);
int event_open_with_cpu(struct event *e, int cpu);
int event_open(struct event *e);
void event_close(struct event *e);
int event_enable(struct event *e);
int event_disable(struct event *e);
int event_reset(struct event *e);
int event_read(struct event *e);
void event_report_justified(struct event *e, int name_width, int result_width);
void event_report(struct event *e);

#endif /* _SELFTESTS_POWERPC_PMU_EVENT_H */
