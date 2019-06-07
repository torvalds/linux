// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <stdio.h>
#include <stdlib.h>
#include <api/fs/fs.h>
#include "header.h"

#define STR_LEN 1024

char *get_cpuid_str(struct perf_pmu *pmu)
{
	/* In nds32, we only have one cpu */
	char *buf = NULL;
	struct cpu_map *cpus;
	const char *sysfs = sysfs__mountpoint();

	if (!sysfs || !pmu || !pmu->cpus)
		return NULL;

	buf = malloc(STR_LEN);
	if (!buf)
		return NULL;

	cpus = cpu_map__get(pmu->cpus);
	sprintf(buf, "0x%x", cpus->nr - 1);
	cpu_map__put(cpus);
	return buf;
}
