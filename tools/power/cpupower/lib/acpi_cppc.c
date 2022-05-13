// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "cpupower_intern.h"
#include "acpi_cppc.h"

/* ACPI CPPC sysfs access ***********************************************/

static int acpi_cppc_read_file(unsigned int cpu, const char *fname,
			       char *buf, size_t buflen)
{
	char path[SYSFS_PATH_MAX];

	snprintf(path, sizeof(path), PATH_TO_CPU "cpu%u/acpi_cppc/%s",
		 cpu, fname);
	return cpupower_read_sysfs(path, buf, buflen);
}

static const char * const acpi_cppc_value_files[] = {
	[HIGHEST_PERF] = "highest_perf",
	[LOWEST_PERF] = "lowest_perf",
	[NOMINAL_PERF] = "nominal_perf",
	[LOWEST_NONLINEAR_PERF] = "lowest_nonlinear_perf",
	[LOWEST_FREQ] = "lowest_freq",
	[NOMINAL_FREQ] = "nominal_freq",
	[REFERENCE_PERF] = "reference_perf",
	[WRAPAROUND_TIME] = "wraparound_time"
};

unsigned long acpi_cppc_get_data(unsigned int cpu, enum acpi_cppc_value which)
{
	unsigned long long value;
	unsigned int len;
	char linebuf[MAX_LINE_LEN];
	char *endp;

	if (which >= MAX_CPPC_VALUE_FILES)
		return 0;

	len = acpi_cppc_read_file(cpu, acpi_cppc_value_files[which],
				  linebuf, sizeof(linebuf));
	if (len == 0)
		return 0;

	value = strtoull(linebuf, &endp, 0);

	if (endp == linebuf || errno == ERANGE)
		return 0;

	return value;
}
