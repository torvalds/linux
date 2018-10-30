/*
 * Copyright 2013-2015, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE	/* For CPU_ZERO etc. */

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <linux/limits.h>

#include "utils.h"

static char auxv[4096];
extern unsigned int dscr_insn[];

int read_auxv(char *buf, ssize_t buf_size)
{
	ssize_t num;
	int rc, fd;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return -errno;
	}

	num = read(fd, buf, buf_size);
	if (num < 0) {
		perror("read");
		rc = -EIO;
		goto out;
	}

	if (num > buf_size) {
		printf("overflowed auxv buffer\n");
		rc = -EOVERFLOW;
		goto out;
	}

	rc = 0;
out:
	close(fd);
	return rc;
}

void *find_auxv_entry(int type, char *auxv)
{
	ElfW(auxv_t) *p;

	p = (ElfW(auxv_t) *)auxv;

	while (p->a_type != AT_NULL) {
		if (p->a_type == type)
			return p;

		p++;
	}

	return NULL;
}

void *get_auxv_entry(int type)
{
	ElfW(auxv_t) *p;

	if (read_auxv(auxv, sizeof(auxv)))
		return NULL;

	p = find_auxv_entry(type, auxv);
	if (p)
		return (void *)p->a_un.a_val;

	return NULL;
}

int pick_online_cpu(void)
{
	cpu_set_t mask;
	int cpu;

	CPU_ZERO(&mask);

	if (sched_getaffinity(0, sizeof(mask), &mask)) {
		perror("sched_getaffinity");
		return -1;
	}

	/* We prefer a primary thread, but skip 0 */
	for (cpu = 8; cpu < CPU_SETSIZE; cpu += 8)
		if (CPU_ISSET(cpu, &mask))
			return cpu;

	/* Search for anything, but in reverse */
	for (cpu = CPU_SETSIZE - 1; cpu >= 0; cpu--)
		if (CPU_ISSET(cpu, &mask))
			return cpu;

	printf("No cpus in affinity mask?!\n");
	return -1;
}

bool is_ppc64le(void)
{
	struct utsname uts;
	int rc;

	errno = 0;
	rc = uname(&uts);
	if (rc) {
		perror("uname");
		return false;
	}

	return strcmp(uts.machine, "ppc64le") == 0;
}

int read_debugfs_file(char *debugfs_file, int *result)
{
	int rc = -1, fd;
	char path[PATH_MAX];
	char value[16];

	strcpy(path, "/sys/kernel/debug/");
	strncat(path, debugfs_file, PATH_MAX - strlen(path) - 1);

	if ((fd = open(path, O_RDONLY)) < 0)
		return rc;

	if ((rc = read(fd, value, sizeof(value))) < 0)
		return rc;

	value[15] = 0;
	*result = atoi(value);
	close(fd);

	return 0;
}

int write_debugfs_file(char *debugfs_file, int result)
{
	int rc = -1, fd;
	char path[PATH_MAX];
	char value[16];

	strcpy(path, "/sys/kernel/debug/");
	strncat(path, debugfs_file, PATH_MAX - strlen(path) - 1);

	if ((fd = open(path, O_WRONLY)) < 0)
		return rc;

	snprintf(value, 16, "%d", result);

	if ((rc = write(fd, value, strlen(value))) < 0)
		return rc;

	close(fd);

	return 0;
}

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
		int cpu, int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, hw_event, pid, cpu,
		      group_fd, flags);
}

static void perf_event_attr_init(struct perf_event_attr *event_attr,
					unsigned int type,
					unsigned long config)
{
	memset(event_attr, 0, sizeof(*event_attr));

	event_attr->type = type;
	event_attr->size = sizeof(struct perf_event_attr);
	event_attr->config = config;
	event_attr->read_format = PERF_FORMAT_GROUP;
	event_attr->disabled = 1;
	event_attr->exclude_kernel = 1;
	event_attr->exclude_hv = 1;
	event_attr->exclude_guest = 1;
}

int perf_event_open_counter(unsigned int type,
			    unsigned long config, int group_fd)
{
	int fd;
	struct perf_event_attr event_attr;

	perf_event_attr_init(&event_attr, type, config);

	fd = perf_event_open(&event_attr, 0, -1, group_fd, 0);

	if (fd < 0)
		perror("perf_event_open() failed");

	return fd;
}

int perf_event_enable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error while enabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_disable(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error disabling perf events");
		return -1;
	}

	return 0;
}

int perf_event_reset(int fd)
{
	if (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) == -1) {
		perror("error resetting perf events");
		return -1;
	}

	return 0;
}

static void sigill_handler(int signr, siginfo_t *info, void *unused)
{
	static int warned = 0;
	ucontext_t *ctx = (ucontext_t *)unused;
	unsigned long *pc = &UCONTEXT_NIA(ctx);

	if (*pc == (unsigned long)&dscr_insn) {
		if (!warned++)
			printf("WARNING: Skipping over dscr setup. Consider running 'ppc64_cpu --dscr=1' manually.\n");
		*pc += 4;
	} else {
		printf("SIGILL at %p\n", pc);
		abort();
	}
}

void set_dscr(unsigned long val)
{
	static int init = 0;
	struct sigaction sa;

	if (!init) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = sigill_handler;
		sa.sa_flags = SA_SIGINFO;
		if (sigaction(SIGILL, &sa, NULL))
			perror("sigill_handler");
		init = 1;
	}

	asm volatile("dscr_insn: mtspr %1,%0" : : "r" (val), "i" (SPRN_DSCR));
}
