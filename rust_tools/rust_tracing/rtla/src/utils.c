// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#define _GNU_SOURCE
#ifdef HAVE_LIBCPUPOWER_SUPPORT
#include <cpuidle.h>
#endif /* HAVE_LIBCPUPOWER_SUPPORT */
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
 * parse_cpu_set - parse a cpu_list filling cpu_set_t argument
 *
 * Receives a cpu list, like 1-3,5 (cpus 1, 2, 3, 5), and then set
 * filling cpu_set_t argument.
 *
 * Returns 1 on success, 0 otherwise.
 */
int parse_cpu_set(char *cpu_list, cpu_set_t *set)
{
	const char *p;
	int end_cpu;
	int nr_cpus;
	int cpu;
	int i;

	CPU_ZERO(set);

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

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
			debug_msg("cpu_set: adding cpu %d\n", cpu);
			CPU_SET(cpu, set);
		} else {
			for (i = cpu; i <= end_cpu; i++) {
				debug_msg("cpu_set: adding cpu %d\n", i);
				CPU_SET(i, set);
			}
		}

		if (*p == ',')
			p++;
	}

	return 0;
err:
	debug_msg("Error parsing the cpu set %s\n", cpu_list);
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
#ifndef __NR_sched_setattr
# ifdef __x86_64__
#  define __NR_sched_setattr	314
# elif __i386__
#  define __NR_sched_setattr	351
# elif __arm__
#  define __NR_sched_setattr	380
# elif __aarch64__ || __riscv
#  define __NR_sched_setattr	274
# elif __powerpc__
#  define __NR_sched_setattr	355
# elif __s390x__
#  define __NR_sched_setattr	345
# elif __loongarch__
#  define __NR_sched_setattr	274
# endif
#endif

#define SCHED_DEADLINE		6

static inline int syscall_sched_setattr(pid_t pid, const struct sched_attr *attr,
				unsigned int flags) {
	return syscall(__NR_sched_setattr, pid, attr, flags);
}

int __set_sched_attr(int pid, struct sched_attr *attr)
{
	int flags = 0;
	int retval;

	retval = syscall_sched_setattr(pid, attr, flags);
	if (retval < 0) {
		err_msg("Failed to set sched attributes to the pid %d: %s\n",
			pid, strerror(errno));
		return 1;
	}

	return 0;
}

/*
 * procfs_is_workload_pid - check if a procfs entry contains a comm_prefix* comm
 *
 * Check if the procfs entry is a directory of a process, and then check if the
 * process has a comm with the prefix set in char *comm_prefix. As the
 * current users of this function only check for kernel threads, there is no
 * need to check for the threads for the process.
 *
 * Return: True if the proc_entry contains a comm file with comm_prefix*.
 * Otherwise returns false.
 */
static int procfs_is_workload_pid(const char *comm_prefix, struct dirent *proc_entry)
{
	char buffer[MAX_PATH];
	int comm_fd, retval;
	char *t_name;

	if (proc_entry->d_type != DT_DIR)
		return 0;

	if (*proc_entry->d_name == '.')
		return 0;

	/* check if the string is a pid */
	for (t_name = proc_entry->d_name; t_name; t_name++) {
		if (!isdigit(*t_name))
			break;
	}

	if (*t_name != '\0')
		return 0;

	snprintf(buffer, MAX_PATH, "/proc/%s/comm", proc_entry->d_name);
	comm_fd = open(buffer, O_RDONLY);
	if (comm_fd < 0)
		return 0;

	memset(buffer, 0, MAX_PATH);
	retval = read(comm_fd, buffer, MAX_PATH);

	close(comm_fd);

	if (retval <= 0)
		return 0;

	retval = strncmp(comm_prefix, buffer, strlen(comm_prefix));
	if (retval)
		return 0;

	/* comm already have \n */
	debug_msg("Found workload pid:%s comm:%s", proc_entry->d_name, buffer);

	return 1;
}

/*
 * set_comm_sched_attr - set sched params to threads starting with char *comm_prefix
 *
 * This function uses procfs to list the currently running threads and then set the
 * sched_attr *attr to the threads that start with char *comm_prefix. It is
 * mainly used to set the priority to the kernel threads created by the
 * tracers.
 */
int set_comm_sched_attr(const char *comm_prefix, struct sched_attr *attr)
{
	struct dirent *proc_entry;
	DIR *procfs;
	int retval;

	if (strlen(comm_prefix) >= MAX_PATH) {
		err_msg("Command prefix is too long: %d < strlen(%s)\n",
			MAX_PATH, comm_prefix);
		return 1;
	}

	procfs = opendir("/proc");
	if (!procfs) {
		err_msg("Could not open procfs\n");
		return 1;
	}

	while ((proc_entry = readdir(procfs))) {

		retval = procfs_is_workload_pid(comm_prefix, proc_entry);
		if (!retval)
			continue;

		/* procfs_is_workload_pid confirmed it is a pid */
		retval = __set_sched_attr(atoi(proc_entry->d_name), attr);
		if (retval) {
			err_msg("Error setting sched attributes for pid:%s\n", proc_entry->d_name);
			goto out_err;
		}

		debug_msg("Set sched attributes for pid:%s\n", proc_entry->d_name);
	}
	return 0;

out_err:
	closedir(procfs);
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

		if (prio < MIN_NICE)
			return -1;
		if (prio > MAX_NICE)
			return -1;

		sched_param->sched_policy   = SCHED_OTHER;
		sched_param->sched_nice = prio;
		break;
	default:
		return -1;
	}
	return 0;
}

/*
 * set_cpu_dma_latency - set the /dev/cpu_dma_latecy
 *
 * This is used to reduce the exit from idle latency. The value
 * will be reset once the file descriptor of /dev/cpu_dma_latecy
 * is closed.
 *
 * Return: the /dev/cpu_dma_latecy file descriptor
 */
int set_cpu_dma_latency(int32_t latency)
{
	int retval;
	int fd;

	fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (fd < 0) {
		err_msg("Error opening /dev/cpu_dma_latency\n");
		return -1;
	}

	retval = write(fd, &latency, 4);
	if (retval < 1) {
		err_msg("Error setting /dev/cpu_dma_latency\n");
		close(fd);
		return -1;
	}

	debug_msg("Set /dev/cpu_dma_latency to %d\n", latency);

	return fd;
}

#ifdef HAVE_LIBCPUPOWER_SUPPORT
static unsigned int **saved_cpu_idle_disable_state;
static size_t saved_cpu_idle_disable_state_alloc_ctr;

/*
 * save_cpu_idle_state_disable - save disable for all idle states of a cpu
 *
 * Saves the current disable of all idle states of a cpu, to be subsequently
 * restored via restore_cpu_idle_disable_state.
 *
 * Return: idle state count on success, negative on error
 */
int save_cpu_idle_disable_state(unsigned int cpu)
{
	unsigned int nr_states;
	unsigned int state;
	int disabled;
	int nr_cpus;

	nr_states = cpuidle_state_count(cpu);

	if (nr_states == 0)
		return 0;

	if (saved_cpu_idle_disable_state == NULL) {
		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
		saved_cpu_idle_disable_state = calloc(nr_cpus, sizeof(unsigned int *));
		if (!saved_cpu_idle_disable_state)
			return -1;
	}

	saved_cpu_idle_disable_state[cpu] = calloc(nr_states, sizeof(unsigned int));
	if (!saved_cpu_idle_disable_state[cpu])
		return -1;
	saved_cpu_idle_disable_state_alloc_ctr++;

	for (state = 0; state < nr_states; state++) {
		disabled = cpuidle_is_state_disabled(cpu, state);
		if (disabled < 0)
			return disabled;
		saved_cpu_idle_disable_state[cpu][state] = disabled;
	}

	return nr_states;
}

/*
 * restore_cpu_idle_disable_state - restore disable for all idle states of a cpu
 *
 * Restores the current disable state of all idle states of a cpu that was
 * previously saved by save_cpu_idle_disable_state.
 *
 * Return: idle state count on success, negative on error
 */
int restore_cpu_idle_disable_state(unsigned int cpu)
{
	unsigned int nr_states;
	unsigned int state;
	int disabled;
	int result;

	nr_states = cpuidle_state_count(cpu);

	if (nr_states == 0)
		return 0;

	if (!saved_cpu_idle_disable_state)
		return -1;

	for (state = 0; state < nr_states; state++) {
		if (!saved_cpu_idle_disable_state[cpu])
			return -1;
		disabled = saved_cpu_idle_disable_state[cpu][state];
		result = cpuidle_state_disable(cpu, state, disabled);
		if (result < 0)
			return result;
	}

	free(saved_cpu_idle_disable_state[cpu]);
	saved_cpu_idle_disable_state[cpu] = NULL;
	saved_cpu_idle_disable_state_alloc_ctr--;
	if (saved_cpu_idle_disable_state_alloc_ctr == 0) {
		free(saved_cpu_idle_disable_state);
		saved_cpu_idle_disable_state = NULL;
	}

	return nr_states;
}

/*
 * free_cpu_idle_disable_states - free saved idle state disable for all cpus
 *
 * Frees the memory used for storing cpu idle state disable for all cpus
 * and states.
 *
 * Normally, the memory is freed automatically in
 * restore_cpu_idle_disable_state; this is mostly for cleaning up after an
 * error.
 */
void free_cpu_idle_disable_states(void)
{
	int cpu;
	int nr_cpus;

	if (!saved_cpu_idle_disable_state)
		return;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		free(saved_cpu_idle_disable_state[cpu]);
		saved_cpu_idle_disable_state[cpu] = NULL;
	}

	free(saved_cpu_idle_disable_state);
	saved_cpu_idle_disable_state = NULL;
}

/*
 * set_deepest_cpu_idle_state - limit idle state of cpu
 *
 * Disables all idle states deeper than the one given in
 * deepest_state (assuming states with higher number are deeper).
 *
 * This is used to reduce the exit from idle latency. Unlike
 * set_cpu_dma_latency, it can disable idle states per cpu.
 *
 * Return: idle state count on success, negative on error
 */
int set_deepest_cpu_idle_state(unsigned int cpu, unsigned int deepest_state)
{
	unsigned int nr_states;
	unsigned int state;
	int result;

	nr_states = cpuidle_state_count(cpu);

	for (state = deepest_state + 1; state < nr_states; state++) {
		result = cpuidle_state_disable(cpu, state, 1);
		if (result < 0)
			return result;
	}

	return nr_states;
}
#endif /* HAVE_LIBCPUPOWER_SUPPORT */

#define _STR(x) #x
#define STR(x) _STR(x)

/*
 * find_mount - find a the mount point of a given fs
 *
 * Returns 0 if mount is not found, otherwise return 1 and fill mp
 * with the mount point.
 */
static const int find_mount(const char *fs, char *mp, int sizeof_mp)
{
	char mount_point[MAX_PATH+1];
	char type[100];
	int found = 0;
	FILE *fp;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return 0;

	while (fscanf(fp, "%*s %" STR(MAX_PATH) "s %99s %*s %*d %*d\n",	mount_point, type) == 2) {
		if (strcmp(type, fs) == 0) {
			found = 1;
			break;
		}
	}
	fclose(fp);

	if (!found)
		return 0;

	memset(mp, 0, sizeof_mp);
	strncpy(mp, mount_point, sizeof_mp - 1);

	debug_msg("Fs %s found at %s\n", fs, mp);
	return 1;
}

/*
 * get_self_cgroup - get the current thread cgroup path
 *
 * Parse /proc/$$/cgroup file to get the thread's cgroup. As an example of line to parse:
 *
 * 0::/user.slice/user-0.slice/session-3.scope'\n'
 *
 * This function is interested in the content after the second : and before the '\n'.
 *
 * Returns 1 if a string was found, 0 otherwise.
 */
static int get_self_cgroup(char *self_cg, int sizeof_self_cg)
{
	char path[MAX_PATH], *start;
	int fd, retval;

	snprintf(path, MAX_PATH, "/proc/%d/cgroup", getpid());

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;

	retval = read(fd, path, MAX_PATH);

	close(fd);

	if (retval <= 0)
		return 0;

	start = path;

	start = strstr(start, ":");
	if (!start)
		return 0;

	/* skip ":" */
	start++;

	start = strstr(start, ":");
	if (!start)
		return 0;

	/* skip ":" */
	start++;

	if (strlen(start) >= sizeof_self_cg)
		return 0;

	snprintf(self_cg, sizeof_self_cg, "%s", start);

	/* Swap '\n' with '\0' */
	start = strstr(self_cg, "\n");

	/* there must be '\n' */
	if (!start)
		return 0;

	/* ok, it found a string after the second : and before the \n */
	*start = '\0';

	return 1;
}

/*
 * set_comm_cgroup - Set cgroup to pid_t pid
 *
 * If cgroup argument is not NULL, the threads will move to the given cgroup.
 * Otherwise, the cgroup of the calling, i.e., rtla, thread will be used.
 *
 * Supports cgroup v2.
 *
 * Returns 1 on success, 0 otherwise.
 */
int set_pid_cgroup(pid_t pid, const char *cgroup)
{
	char cgroup_path[MAX_PATH - strlen("/cgroup.procs")];
	char cgroup_procs[MAX_PATH];
	char pid_str[24];
	int retval;
	int cg_fd;

	retval = find_mount("cgroup2", cgroup_path, sizeof(cgroup_path));
	if (!retval) {
		err_msg("Did not find cgroupv2 mount point\n");
		return 0;
	}

	if (!cgroup) {
		retval = get_self_cgroup(&cgroup_path[strlen(cgroup_path)],
				sizeof(cgroup_path) - strlen(cgroup_path));
		if (!retval) {
			err_msg("Did not find self cgroup\n");
			return 0;
		}
	} else {
		snprintf(&cgroup_path[strlen(cgroup_path)],
				sizeof(cgroup_path) - strlen(cgroup_path), "%s/", cgroup);
	}

	snprintf(cgroup_procs, MAX_PATH, "%s/cgroup.procs", cgroup_path);

	debug_msg("Using cgroup path at: %s\n", cgroup_procs);

	cg_fd = open(cgroup_procs, O_RDWR);
	if (cg_fd < 0)
		return 0;

	snprintf(pid_str, sizeof(pid_str), "%d\n", pid);

	retval = write(cg_fd, pid_str, strlen(pid_str));
	if (retval < 0)
		err_msg("Error setting cgroup attributes for pid:%s - %s\n",
				pid_str, strerror(errno));
	else
		debug_msg("Set cgroup attributes for pid:%s\n", pid_str);

	close(cg_fd);

	return (retval >= 0);
}

/**
 * set_comm_cgroup - Set cgroup to threads starting with char *comm_prefix
 *
 * If cgroup argument is not NULL, the threads will move to the given cgroup.
 * Otherwise, the cgroup of the calling, i.e., rtla, thread will be used.
 *
 * Supports cgroup v2.
 *
 * Returns 1 on success, 0 otherwise.
 */
int set_comm_cgroup(const char *comm_prefix, const char *cgroup)
{
	char cgroup_path[MAX_PATH - strlen("/cgroup.procs")];
	char cgroup_procs[MAX_PATH];
	struct dirent *proc_entry;
	DIR *procfs;
	int retval;
	int cg_fd;

	if (strlen(comm_prefix) >= MAX_PATH) {
		err_msg("Command prefix is too long: %d < strlen(%s)\n",
			MAX_PATH, comm_prefix);
		return 0;
	}

	retval = find_mount("cgroup2", cgroup_path, sizeof(cgroup_path));
	if (!retval) {
		err_msg("Did not find cgroupv2 mount point\n");
		return 0;
	}

	if (!cgroup) {
		retval = get_self_cgroup(&cgroup_path[strlen(cgroup_path)],
				sizeof(cgroup_path) - strlen(cgroup_path));
		if (!retval) {
			err_msg("Did not find self cgroup\n");
			return 0;
		}
	} else {
		snprintf(&cgroup_path[strlen(cgroup_path)],
				sizeof(cgroup_path) - strlen(cgroup_path), "%s/", cgroup);
	}

	snprintf(cgroup_procs, MAX_PATH, "%s/cgroup.procs", cgroup_path);

	debug_msg("Using cgroup path at: %s\n", cgroup_procs);

	cg_fd = open(cgroup_procs, O_RDWR);
	if (cg_fd < 0)
		return 0;

	procfs = opendir("/proc");
	if (!procfs) {
		err_msg("Could not open procfs\n");
		goto out_cg;
	}

	while ((proc_entry = readdir(procfs))) {

		retval = procfs_is_workload_pid(comm_prefix, proc_entry);
		if (!retval)
			continue;

		retval = write(cg_fd, proc_entry->d_name, strlen(proc_entry->d_name));
		if (retval < 0) {
			err_msg("Error setting cgroup attributes for pid:%s - %s\n",
				proc_entry->d_name, strerror(errno));
			goto out_procfs;
		}

		debug_msg("Set cgroup attributes for pid:%s\n", proc_entry->d_name);
	}

	closedir(procfs);
	close(cg_fd);
	return 1;

out_procfs:
	closedir(procfs);
out_cg:
	close(cg_fd);
	return 0;
}

/**
 * auto_house_keeping - Automatically move rtla out of measurement threads
 *
 * Try to move rtla away from the tracer, if possible.
 *
 * Returns 1 on success, 0 otherwise.
 */
int auto_house_keeping(cpu_set_t *monitored_cpus)
{
	cpu_set_t rtla_cpus, house_keeping_cpus;
	int retval;

	/* first get the CPUs in which rtla can actually run. */
	retval = sched_getaffinity(getpid(), sizeof(rtla_cpus), &rtla_cpus);
	if (retval == -1) {
		debug_msg("Could not get rtla affinity, rtla might run with the threads!\n");
		return 0;
	}

	/* then check if the existing setup is already good. */
	CPU_AND(&house_keeping_cpus, &rtla_cpus, monitored_cpus);
	if (!CPU_COUNT(&house_keeping_cpus)) {
		debug_msg("rtla and the monitored CPUs do not share CPUs.");
		debug_msg("Skipping auto house-keeping\n");
		return 1;
	}

	/* remove the intersection */
	CPU_XOR(&house_keeping_cpus, &rtla_cpus, monitored_cpus);

	/* get only those that rtla can run */
	CPU_AND(&house_keeping_cpus, &house_keeping_cpus, &rtla_cpus);

	/* is there any cpu left? */
	if (!CPU_COUNT(&house_keeping_cpus)) {
		debug_msg("Could not find any CPU for auto house-keeping\n");
		return 0;
	}

	retval = sched_setaffinity(getpid(), sizeof(house_keeping_cpus), &house_keeping_cpus);
	if (retval == -1) {
		debug_msg("Could not set affinity for auto house-keeping\n");
		return 0;
	}

	debug_msg("rtla automatically moved to an auto house-keeping cpu set\n");

	return 1;
}
