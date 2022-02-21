// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <proc/readproc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sched.h>
#include <stdio.h>

#include "utils.h"

#define MAX_MSG_LENGTH	1024
int config_debug;

/*
 * err_msg - print an error message to the stderr
 */
void err_msg(const char *fmt, ...)
{
	char message[MAX_MSG_LENGTH];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", message);
}

/*
 * debug_msg - print a debug message to stderr if debug is set
 */
void debug_msg(const char *fmt, ...)
{
	char message[MAX_MSG_LENGTH];
	va_list ap;

	if (!config_debug)
		return;

	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", message);
}

/*
 * get_llong_from_str - get a long long int from a string
 */
long long get_llong_from_str(char *start)
{
	long long value;
	char *end;

	errno = 0;
	value = strtoll(start, &end, 10);
	if (errno || start == end)
		return -1;

	return value;
}

/*
 * get_duration - fill output with a human readable duration since start_time
 */
void get_duration(time_t start_time, char *output, int output_size)
{
	time_t now = time(NULL);
	struct tm *tm_info;
	time_t duration;

	duration = difftime(now, start_time);
	tm_info = gmtime(&duration);

	snprintf(output, output_size, "%3d %02d:%02d:%02d",
			tm_info->tm_yday,
			tm_info->tm_hour,
			tm_info->tm_min,
			tm_info->tm_sec);
}

/*
 * parse_cpu_list - parse a cpu_list filling a char vector with cpus set
 *
 * Receives a cpu list, like 1-3,5 (cpus 1, 2, 3, 5), and then set the char
 * in the monitored_cpus.
 *
 * XXX: convert to a bitmask.
 */
int parse_cpu_list(char *cpu_list, char **monitored_cpus)
{
	char *mon_cpus;
	const char *p;
	int end_cpu;
	int nr_cpus;
	int cpu;
	int i;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	mon_cpus = malloc(nr_cpus * sizeof(char));
	memset(mon_cpus, 0, (nr_cpus * sizeof(char)));

	for (p = cpu_list; *p; ) {
		cpu = atoi(p);
		if (cpu < 0 || (!cpu && *p != '0') || cpu >= nr_cpus)
			goto err;

		while (isdigit(*p))
			p++;
		if (*p == '-') {
			p++;
			end_cpu = atoi(p);
			if (end_cpu < cpu || (!end_cpu && *p != '0') || end_cpu >= nr_cpus)
				goto err;
			while (isdigit(*p))
				p++;
		} else
			end_cpu = cpu;

		if (cpu == end_cpu) {
			debug_msg("cpu_list: adding cpu %d\n", cpu);
			mon_cpus[cpu] = 1;
		} else {
			for (i = cpu; i <= end_cpu; i++) {
				debug_msg("cpu_list: adding cpu %d\n", i);
				mon_cpus[i] = 1;
			}
		}

		if (*p == ',')
			p++;
	}

	*monitored_cpus = mon_cpus;

	return 0;

err:
	debug_msg("Error parsing the cpu list %s", cpu_list);
	return 1;
}

/*
 * parse_duration - parse duration with s/m/h/d suffix converting it to seconds
 */
long parse_seconds_duration(char *val)
{
	char *end;
	long t;

	t = strtol(val, &end, 10);

	if (end) {
		switch (*end) {
		case 's':
		case 'S':
			break;
		case 'm':
		case 'M':
			t *= 60;
			break;
		case 'h':
		case 'H':
			t *= 60 * 60;
			break;

		case 'd':
		case 'D':
			t *= 24 * 60 * 60;
			break;
		}
	}

	return t;
}

/*
 * parse_ns_duration - parse duration with ns/us/ms/s converting it to nanoseconds
 */
long parse_ns_duration(char *val)
{
	char *end;
	long t;

	t = strtol(val, &end, 10);

	if (end) {
		if (!strncmp(end, "ns", 2)) {
			return t;
		} else if (!strncmp(end, "us", 2)) {
			t *= 1000;
			return t;
		} else if (!strncmp(end, "ms", 2)) {
			t *= 1000 * 1000;
			return t;
		} else if (!strncmp(end, "s", 1)) {
			t *= 1000 * 1000 * 1000;
			return t;
		}
		return -1;
	}

	return t;
}

/*
 * This is a set of helper functions to use SCHED_DEADLINE.
 */
#ifdef __x86_64__
# define __NR_sched_setattr	314
# define __NR_sched_getattr	315
#elif __i386__
# define __NR_sched_setattr	351
# define __NR_sched_getattr	352
#elif __arm__
# define __NR_sched_setattr	380
# define __NR_sched_getattr	381
#elif __aarch64__
# define __NR_sched_setattr	274
# define __NR_sched_getattr	275
#elif __powerpc__
# define __NR_sched_setattr	355
# define __NR_sched_getattr	356
#elif __s390x__
# define __NR_sched_setattr	345
# define __NR_sched_getattr	346
#endif

#define SCHED_DEADLINE		6

static inline int sched_setattr(pid_t pid, const struct sched_attr *attr,
				unsigned int flags) {
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

static inline int sched_getattr(pid_t pid, struct sched_attr *attr,
				unsigned int size, unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

int __set_sched_attr(int pid, struct sched_attr *attr)
{
	int flags = 0;
	int retval;

	retval = sched_setattr(pid, attr, flags);
	if (retval < 0) {
		err_msg("boost_with_deadline failed to boost pid %d: %s\n",
			pid, strerror(errno));
		return 1;
	}

	return 0;
}
/*
 * set_comm_sched_attr - set sched params to threads starting with char *comm
 *
 * This function uses procps to list the currently running threads and then
 * set the sched_attr *attr to the threads that start with char *comm. It is
 * mainly used to set the priority to the kernel threads created by the
 * tracers.
 */
int set_comm_sched_attr(const char *comm, struct sched_attr *attr)
{
	int flags = PROC_FILLCOM | PROC_FILLSTAT;
	PROCTAB *ptp;
	proc_t task;
	int retval;

	ptp = openproc(flags);
	if (!ptp) {
		err_msg("error openproc()\n");
		return -ENOENT;
	}

	memset(&task, 0, sizeof(task));

	while (readproc(ptp, &task)) {
		retval = strncmp(comm, task.cmd, strlen(comm));
		if (retval)
			continue;
		retval = __set_sched_attr(task.tid, attr);
		if (retval)
			goto out_err;
	}

	closeproc(ptp);
	return 0;

out_err:
	closeproc(ptp);
	return 1;
}

#define INVALID_VAL	(~0L)
static long get_long_ns_after_colon(char *start)
{
	long val = INVALID_VAL;

	/* find the ":" */
	start = strstr(start, ":");
	if (!start)
		return -1;

	/* skip ":" */
	start++;
	val = parse_ns_duration(start);

	return val;
}

static long get_long_after_colon(char *start)
{
	long val = INVALID_VAL;

	/* find the ":" */
	start = strstr(start, ":");
	if (!start)
		return -1;

	/* skip ":" */
	start++;
	val = get_llong_from_str(start);

	return val;
}

/*
 * parse priority in the format:
 * SCHED_OTHER:
 *		o:<prio>
 *		O:<prio>
 * SCHED_RR:
 *		r:<prio>
 *		R:<prio>
 * SCHED_FIFO:
 *		f:<prio>
 *		F:<prio>
 * SCHED_DEADLINE:
 *		d:runtime:period
 *		D:runtime:period
 */
int parse_prio(char *arg, struct sched_attr *sched_param)
{
	long prio;
	long runtime;
	long period;

	memset(sched_param, 0, sizeof(*sched_param));
	sched_param->size = sizeof(*sched_param);

	switch (arg[0]) {
	case 'd':
	case 'D':
		/* d:runtime:period */
		if (strlen(arg) < 4)
			return -1;

		runtime = get_long_ns_after_colon(arg);
		if (runtime == INVALID_VAL)
			return -1;

		period = get_long_ns_after_colon(&arg[2]);
		if (period == INVALID_VAL)
			return -1;

		if (runtime > period)
			return -1;

		sched_param->sched_policy   = SCHED_DEADLINE;
		sched_param->sched_runtime  = runtime;
		sched_param->sched_deadline = period;
		sched_param->sched_period   = period;
		break;
	case 'f':
	case 'F':
		/* f:prio */
		prio = get_long_after_colon(arg);
		if (prio == INVALID_VAL)
			return -1;

		if (prio < sched_get_priority_min(SCHED_FIFO))
			return -1;
		if (prio > sched_get_priority_max(SCHED_FIFO))
			return -1;

		sched_param->sched_policy   = SCHED_FIFO;
		sched_param->sched_priority = prio;
		break;
	case 'r':
	case 'R':
		/* r:prio */
		prio = get_long_after_colon(arg);
		if (prio == INVALID_VAL)
			return -1;

		if (prio < sched_get_priority_min(SCHED_RR))
			return -1;
		if (prio > sched_get_priority_max(SCHED_RR))
			return -1;

		sched_param->sched_policy   = SCHED_RR;
		sched_param->sched_priority = prio;
		break;
	case 'o':
	case 'O':
		/* o:prio */
		prio = get_long_after_colon(arg);
		if (prio == INVALID_VAL)
			return -1;

		if (prio < sched_get_priority_min(SCHED_OTHER))
			return -1;
		if (prio > sched_get_priority_max(SCHED_OTHER))
			return -1;

		sched_param->sched_policy   = SCHED_OTHER;
		sched_param->sched_priority = prio;
		break;
	default:
		return -1;
	}
	return 0;
}
