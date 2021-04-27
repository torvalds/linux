// SPDX-License-Identifier: GPL-2.0
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <locale.h>
#include <api/fs/fs.h>
#include "fncache.h"
#include "pmu-hybrid.h"

LIST_HEAD(perf_pmu__hybrid_pmus);

bool perf_pmu__hybrid_mounted(const char *name)
{
	char path[PATH_MAX];
	const char *sysfs;
	FILE *file;
	int n, cpu;

	if (strncmp(name, "cpu_", 4))
		return false;

	sysfs = sysfs__mountpoint();
	if (!sysfs)
		return false;

	snprintf(path, PATH_MAX, CPUS_TEMPLATE_CPU, sysfs, name);
	if (!file_available(path))
		return false;

	file = fopen(path, "r");
	if (!file)
		return false;

	n = fscanf(file, "%u", &cpu);
	fclose(file);
	if (n <= 0)
		return false;

	return true;
}
