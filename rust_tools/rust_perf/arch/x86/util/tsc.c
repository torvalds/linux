// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "../../../util/debug.h"
#include "../../../util/tsc.h"
#include "cpuid.h"

u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

/*
 * Derive the TSC frequency in Hz from the /proc/cpuinfo, for example:
 * ...
 * model name      : Intel(R) Xeon(R) Gold 6154 CPU @ 3.00GHz
 * ...
 * will return 3000000000.
 */
static u64 cpuinfo_tsc_freq(void)
{
	u64 result = 0;
	FILE *cpuinfo;
	char *line = NULL;
	size_t len = 0;

	cpuinfo = fopen("/proc/cpuinfo", "r");
	if (!cpuinfo) {
		pr_err("Failed to read /proc/cpuinfo for TSC frequency\n");
		return 0;
	}
	while (getline(&line, &len, cpuinfo) > 0) {
		if (!strncmp(line, "model name", 10)) {
			char *pos = strstr(line + 11, " @ ");
			double float_result;

			if (pos && sscanf(pos, " @ %lfGHz", &float_result) == 1) {
				float_result *= 1000000000;
				result = (u64)float_result;
				goto out;
			}
		}
	}
out:
	if (result == 0)
		pr_err("Failed to find TSC frequency in /proc/cpuinfo\n");

	free(line);
	fclose(cpuinfo);
	return result;
}

u64 arch_get_tsc_freq(void)
{
	unsigned int a, b, c, d, lvl;
	static bool cached;
	static double tsc;
	char vendor[16];

	if (cached)
		return tsc;

	cached = true;
	get_cpuid_0(vendor, &lvl);
	if (!strstr(vendor, "Intel"))
		return 0;

	/*
	 * Don't support Time Stamp Counter and
	 * Nominal Core Crystal Clock Information Leaf.
	 */
	if (lvl < 0x15) {
		tsc = cpuinfo_tsc_freq();
		return tsc;
	}

	cpuid(0x15, 0, &a, &b, &c, &d);
	/* TSC frequency is not enumerated */
	if (!a || !b || !c) {
		tsc = cpuinfo_tsc_freq();
		return tsc;
	}

	tsc = (u64)c * (u64)b / (u64)a;
	return tsc;
}
