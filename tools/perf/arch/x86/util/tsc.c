// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>

#include "../../../util/tsc.h"

u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}
