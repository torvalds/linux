// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/stringify.h>
#include "header.h"
#include "utils_header.h"
#include "metricgroup.h"
#include <api/fs/fs.h>
#include <sys/auxv.h>

static bool is_compat_mode(void)
{
	unsigned long base_platform = getauxval(AT_BASE_PLATFORM);
	unsigned long platform = getauxval(AT_PLATFORM);

	if (!strcmp((char *)platform, (char *)base_platform))
		return false;

	return true;
}

int
get_cpuid(char *buffer, size_t sz, struct perf_cpu cpu __maybe_unused)
{
	unsigned long pvr;
	int nb;

	pvr = mfspr(SPRN_PVR);

	nb = scnprintf(buffer, sz, "%lu,%lu$", PVR_VER(pvr), PVR_REV(pvr));

	/* look for end marker to ensure the entire data fit */
	if (strchr(buffer, '$')) {
		buffer[nb-1] = '\0';
		return 0;
	}
	return ENOBUFS;
}

char *
get_cpuid_str(struct perf_cpu cpu __maybe_unused)
{
	char *bufp;
	unsigned long pvr;

	/*
	 * IBM Power System supports compatible mode. That is
	 * Nth generation platform can support previous generation
	 * OS in a mode called compatibile mode. For ex. LPAR can be
	 * booted in a Power9 mode when the system is a Power10.
	 *
	 * In the compatible mode, care must be taken when generating
	 * PVR value. When read, PVR will be of the AT_BASE_PLATFORM
	 * To support generic events, return 0x00ffffff as pvr when
	 * booted in compat mode. Based on this pvr value, json will
	 * pick events from pmu-events/arch/powerpc/compat
	 */
	if (!is_compat_mode())
		pvr = mfspr(SPRN_PVR);
	else
		pvr = 0x00ffffff;

	if (asprintf(&bufp, "0x%.8lx", pvr) < 0)
		bufp = NULL;

	return bufp;
}

int arch_get_runtimeparam(const struct pmu_metric *pm)
{
	int count;
	char path[PATH_MAX] = "/devices/hv_24x7/interface/";

	strcat(path, pm->aggr_mode == PerChip ? "sockets" : "coresperchip");
	return sysfs__read_int(path, &count) < 0 ? 1 : count;
}
