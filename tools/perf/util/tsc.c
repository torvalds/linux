// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/types.h>

#include "tsc.h"

u64 perf_time_to_tsc(u64 ns, struct perf_tsc_conversion *tc)
{
	u64 t, quot, rem;

	t = ns - tc->time_zero;
	quot = t / tc->time_mult;
	rem  = t % tc->time_mult;
	return (quot << tc->time_shift) +
	       (rem << tc->time_shift) / tc->time_mult;
}

u64 tsc_to_perf_time(u64 cyc, struct perf_tsc_conversion *tc)
{
	u64 quot, rem;

	quot = cyc >> tc->time_shift;
	rem  = cyc & (((u64)1 << tc->time_shift) - 1);
	return tc->time_zero + quot * tc->time_mult +
	       ((rem * tc->time_mult) >> tc->time_shift);
}

u64 __weak rdtsc(void)
{
	return 0;
}
