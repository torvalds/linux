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
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

static char auxv[4096];

void *get_auxv_entry(int type)
{
	ElfW(auxv_t) *p;
	void *result;
	ssize_t num;
	int fd;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return NULL;
	}

	result = NULL;

	num = read(fd, auxv, sizeof(auxv));
	if (num < 0) {
		perror("read");
		goto out;
	}

	if (num > sizeof(auxv)) {
		printf("Overflowed auxv buffer\n");
		goto out;
	}

	p = (ElfW(auxv_t) *)auxv;

	while (p->a_type != AT_NULL) {
		if (p->a_type == type) {
			result = (void *)p->a_un.a_val;
			break;
		}

		p++;
	}
out:
	close(fd);
	return result;
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
