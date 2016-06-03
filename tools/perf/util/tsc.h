#ifndef __PERF_TSC_H
#define __PERF_TSC_H

#include <linux/types.h>

#include "event.h"

struct perf_tsc_conversion {
	u16 time_shift;
	u32 time_mult;
	u64 time_zero;
};
struct perf_event_mmap_page;

int perf_read_tsc_conversion(const struct perf_event_mmap_page *pc,
			     struct perf_tsc_conversion *tc);

u64 perf_time_to_tsc(u64 ns, struct perf_tsc_conversion *tc);
u64 tsc_to_perf_time(u64 cyc, struct perf_tsc_conversion *tc);
u64 rdtsc(void);

struct perf_event_mmap_page;
struct perf_tool;
struct machine;

int perf_event__synth_time_conv(const struct perf_event_mmap_page *pc,
				struct perf_tool *tool,
				perf_event__handler_t process,
				struct machine *machine);

#endif
