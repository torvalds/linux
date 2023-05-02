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
	int cpu;
	char pmu_name[PATH_MAX];
	struct perf_pmu pmu = {.name = pmu_name};

	if (strncmp(name, "cpu_", 4))
		return false;

	strlcpy(pmu_name, name, sizeof(pmu_name));
	return perf_pmu__scan_file(&pmu, "cpus", "%u", &cpu) > 0;
}

struct perf_pmu *perf_pmu__find_hybrid_pmu(const char *name)
{
	struct perf_pmu *pmu;

	if (!name)
		return NULL;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		if (!strcmp(name, pmu->name))
			return pmu;
	}

	return NULL;
}

bool perf_pmu__is_hybrid(const char *name)
{
	return perf_pmu__find_hybrid_pmu(name) != NULL;
}
