/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE	/* For CPU_ZERO etc. */

#include <errno.h>
#include <sched.h>
#include <setjmp.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "utils.h"
#include "lib.h"


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

int bind_to_cpu(int cpu)
{
	cpu_set_t mask;

	printf("Binding to cpu %d\n", cpu);

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	return sched_setaffinity(0, sizeof(mask), &mask);
}

#define PARENT_TOKEN	0xAA
#define CHILD_TOKEN	0x55

int sync_with_child(union pipe read_pipe, union pipe write_pipe)
{
	char c = PARENT_TOKEN;

	FAIL_IF(write(write_pipe.write_fd, &c, 1) != 1);
	FAIL_IF(read(read_pipe.read_fd, &c, 1) != 1);
	if (c != CHILD_TOKEN) /* sometimes expected */
		return 1;

	return 0;
}

int wait_for_parent(union pipe read_pipe)
{
	char c;

	FAIL_IF(read(read_pipe.read_fd, &c, 1) != 1);
	FAIL_IF(c != PARENT_TOKEN);

	return 0;
}

int notify_parent(union pipe write_pipe)
{
	char c = CHILD_TOKEN;

	FAIL_IF(write(write_pipe.write_fd, &c, 1) != 1);

	return 0;
}

int notify_parent_of_error(union pipe write_pipe)
{
	char c = ~CHILD_TOKEN;

	FAIL_IF(write(write_pipe.write_fd, &c, 1) != 1);

	return 0;
}

int wait_for_child(pid_t child_pid)
{
	int rc;

	if (waitpid(child_pid, &rc, 0) == -1) {
		perror("waitpid");
		return 1;
	}

	if (WIFEXITED(rc))
		rc = WEXITSTATUS(rc);
	else
		rc = 1; /* Signal or other */

	return rc;
}

int kill_child_and_wait(pid_t child_pid)
{
	kill(child_pid, SIGTERM);

	return wait_for_child(child_pid);
}

static int eat_cpu_child(union pipe read_pipe, union pipe write_pipe)
{
	volatile int i = 0;

	/*
	 * We are just here to eat cpu and die. So make sure we can be killed,
	 * and also don't do any custom SIGTERM handling.
	 */
	signal(SIGTERM, SIG_DFL);

	notify_parent(write_pipe);
	wait_for_parent(read_pipe);

	/* Soak up cpu forever */
	while (1) i++;

	return 0;
}

pid_t eat_cpu(int (test_function)(void))
{
	union pipe read_pipe, write_pipe;
	int cpu, rc;
	pid_t pid;

	cpu = pick_online_cpu();
	FAIL_IF(cpu < 0);
	FAIL_IF(bind_to_cpu(cpu));

	if (pipe(read_pipe.fds) == -1)
		return -1;

	if (pipe(write_pipe.fds) == -1)
		return -1;

	pid = fork();
	if (pid == 0)
		exit(eat_cpu_child(write_pipe, read_pipe));

	if (sync_with_child(read_pipe, write_pipe)) {
		rc = -1;
		goto out;
	}

	printf("main test running as pid %d\n", getpid());

	rc = test_function();
out:
	kill(pid, SIGKILL);

	return rc;
}

struct addr_range libc, vdso;

int parse_proc_maps(void)
{
	char execute, name[128];
	uint64_t start, end;
	FILE *f;
	int rc;

	f = fopen("/proc/self/maps", "r");
	if (!f) {
		perror("fopen");
		return -1;
	}

	do {
		/* This skips line with no executable which is what we want */
		rc = fscanf(f, "%lx-%lx %*c%*c%c%*c %*x %*d:%*d %*d %127s\n",
			    &start, &end, &execute, name);
		if (rc <= 0)
			break;

		if (execute != 'x')
			continue;

		if (strstr(name, "libc")) {
			libc.first = start;
			libc.last = end - 1;
		} else if (strstr(name, "[vdso]")) {
			vdso.first = start;
			vdso.last = end - 1;
		}
	} while(1);

	fclose(f);

	return 0;
}

#define PARANOID_PATH	"/proc/sys/kernel/perf_event_paranoid"

bool require_paranoia_below(int level)
{
	unsigned long current;
	char *end, buf[16];
	FILE *f;
	int rc;

	rc = -1;

	f = fopen(PARANOID_PATH, "r");
	if (!f) {
		perror("fopen");
		goto out;
	}

	if (!fgets(buf, sizeof(buf), f)) {
		printf("Couldn't read " PARANOID_PATH "?\n");
		goto out_close;
	}

	current = strtoul(buf, &end, 10);

	if (end == buf) {
		printf("Couldn't parse " PARANOID_PATH "?\n");
		goto out_close;
	}

	if (current >= level)
		goto out;

	rc = 0;
out_close:
	fclose(f);
out:
	return rc;
}
