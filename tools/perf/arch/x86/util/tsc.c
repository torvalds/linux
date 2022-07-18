// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <string.h>

#include "../../../util/tsc.h"
#include "cpuid.h"

u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

double arch_get_tsc_freq(void)
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
	if (lvl < 0x15)
		return 0;

	cpuid(0x15, 0, &a, &b, &c, &d);
	/* TSC frequency is not enumerated */
	if (!a || !b || !c)
		return 0;

	tsc = (double)c * (double)b / (double)a;
	return tsc;
}
