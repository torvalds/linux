// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>

#include "cpu.h"
#include "fs/fs.h"

int cpu__get_max_freq(unsigned long long *freq)
{
	char entry[PATH_MAX];
	int cpu;

	if (sysfs__read_int("devices/system/cpu/online", &cpu) < 0)
		return -1;

	snprintf(entry, sizeof(entry),
		 "devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);

	return sysfs__read_ull(entry, freq);
}
