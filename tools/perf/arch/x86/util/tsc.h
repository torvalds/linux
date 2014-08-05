#ifndef TOOLS_PERF_ARCH_X86_UTIL_TSC_H__
#define TOOLS_PERF_ARCH_X86_UTIL_TSC_H__

#include <linux/types.h>

struct perf_tsc_conversion {
	u16 time_shift;
	u32 time_mult;
	u64 time_zero;
};

struct perf_event_mmap_page;

int perf_read_tsc_conversion(const struct perf_event_mmap_page *pc,
			     struct perf_tsc_conversion *tc);

#endif /* TOOLS_PERF_ARCH_X86_UTIL_TSC_H__ */
