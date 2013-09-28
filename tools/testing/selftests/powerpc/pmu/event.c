/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "event.h"


int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
		int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu,
			   group_fd, flags);
}

void event_init_opts(struct event *e, u64 config, int type, char *name)
{
	memset(e, 0, sizeof(*e));

	e->name = name;

	e->attr.type = type;
	e->attr.config = config;
	e->attr.size = sizeof(e->attr);
	/* This has to match the structure layout in the header */
	e->attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | \
				  PERF_FORMAT_TOTAL_TIME_RUNNING;
}

void event_init_named(struct event *e, u64 config, char *name)
{
	event_init_opts(e, config, PERF_TYPE_RAW, name);
}

#define PERF_CURRENT_PID	0
#define PERF_NO_CPU		-1
#define PERF_NO_GROUP		-1

int event_open_with_options(struct event *e, pid_t pid, int cpu, int group_fd)
{
	e->fd = perf_event_open(&e->attr, pid, cpu, group_fd, 0);
	if (e->fd == -1) {
		perror("perf_event_open");
		return -1;
	}

	return 0;
}

int event_open_with_group(struct event *e, int group_fd)
{
	return event_open_with_options(e, PERF_CURRENT_PID, PERF_NO_CPU, group_fd);
}

int event_open(struct event *e)
{
	return event_open_with_options(e, PERF_CURRENT_PID, PERF_NO_CPU, PERF_NO_GROUP);
}

void event_close(struct event *e)
{
	close(e->fd);
}

int event_reset(struct event *e)
{
	return ioctl(e->fd, PERF_EVENT_IOC_RESET);
}

int event_read(struct event *e)
{
	int rc;

	rc = read(e->fd, &e->result, sizeof(e->result));
	if (rc != sizeof(e->result)) {
		fprintf(stderr, "read error on event %p!\n", e);
		return -1;
	}

	return 0;
}

void event_report_justified(struct event *e, int name_width, int result_width)
{
	printf("%*s: result %*llu ", name_width, e->name, result_width,
	       e->result.value);

	if (e->result.running == e->result.enabled)
		printf("running/enabled %llu\n", e->result.running);
	else
		printf("running %llu enabled %llu\n", e->result.running,
			e->result.enabled);
}

void event_report(struct event *e)
{
	event_report_justified(e, 0, 0);
}
