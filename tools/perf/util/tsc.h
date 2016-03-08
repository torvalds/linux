#ifndef __PERF_TSC_H
#define __PERF_TSC_H

#include <linux/types.h>

#include "event.h"
#include "../arch/x86/util/tsc.h"

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
