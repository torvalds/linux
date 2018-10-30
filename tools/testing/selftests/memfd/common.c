// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <stdio.h>
#include <stdlib.h>
#include <linux/fcntl.h>
#include <linux/memfd.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "common.h"

int hugetlbfs_test = 0;

/*
 * Copied from mlock2-tests.c
 */
unsigned long default_huge_page_size(void)
{
	unsigned long hps = 0;
	char *line = NULL;
	size_t linelen = 0;
	FILE *f = fopen("/proc/meminfo", "r");

	if (!f)
		return 0;
	while (getline(&line, &linelen, f) > 0) {
		if (sscanf(line, "Hugepagesize:       %lu kB", &hps) == 1) {
			hps <<= 10;
			break;
		}
	}

	free(line);
	fclose(f);
	return hps;
}

int sys_memfd_create(const char *name, unsigned int flags)
{
	if (hugetlbfs_test)
		flags |= MFD_HUGETLB;

	return syscall(__NR_memfd_create, name, flags);
}
