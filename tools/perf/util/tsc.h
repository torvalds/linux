/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_TSC_H
#define __PERF_TSC_H

#include <linux/types.h>

struct perf_tsc_conversion {
	u16 time_shift;
	u32 time_mult;
	u64 time_zero;
	u64 time_cycles;
	u64 time_mask;

	bool cap_user_time_zero;
	bool cap_user_time_short;
};

struct perf_event_mmap_page;

int perf_read_tsc_conversion(const struct perf_event_mmap_page *pc,
			     struct perf_tsc_conversion *tc);

u64 perf_time_to_tsc(u64 ns, struct perf_tsc_conversion *tc);
u64 tsc_to_perf_time(u64 cyc, struct perf_tsc_conversion *tc);
u64 rdtsc(void);

#endif // __PERF_TSC_H
