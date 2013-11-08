#ifndef TOOLS_PERF_ARCH_X86_UTIL_TSC_H__
#define TOOLS_PERF_ARCH_X86_UTIL_TSC_H__

#include "../../util/types.h"

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

#endif /* TOOLS_PERF_ARCH_X86_UTIL_TSC_H__ */
