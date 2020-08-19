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
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "utils.h"

static char auxv[4096];

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
	int ncpus, cpu = -1;
	cpu_set_t *mask;
	size_t size;

	ncpus = get_nprocs_conf();
	size = CPU_ALLOC_SIZE(ncpus);
	mask = CPU_ALLOC(ncpus);
	if (!mask) {
		perror("malloc");
		return -1;
	}

	CPU_ZERO_S(size, mask);

	if (sched_getaffinity(0, size, mask)) {
		perror("sched_getaffinity");
		goto done;
	}

	/* We prefer a primary thread, but skip 0 */
	for (cpu = 8; cpu < ncpus; cpu += 8)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	/* Search for anything, but in reverse */
	for (cpu = ncpus - 1; cpu >= 0; cpu--)
		if (CPU_ISSET_S(cpu, size, mask))
			goto done;

	printf("No cpus in affinity mask?!\n");

done:
	CPU_FREE(mask);
	return cpu;
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
