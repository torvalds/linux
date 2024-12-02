// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>

#include "../../../util/tsc.h"

u64 rdtsc(void)
{
	u64 val;

	/*
	 * According to ARM DDI 0487F.c, from Armv8.0 to Armv8.5 inclusive, the
	 * system counter is at least 56 bits wide; from Armv8.6, the counter
	 * must be 64 bits wide.  So the system counter could be less than 64
	 * bits wide and it is attributed with the flag 'cap_user_time_short'
	 * is true.
	 */
	asm volatile("mrs %0, cntvct_el0" : "=r" (val));

	return val;
}
