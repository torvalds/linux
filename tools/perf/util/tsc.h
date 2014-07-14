#ifndef __PERF_TSC_H
#define __PERF_TSC_H

#include <linux/types.h>

#include "../arch/x86/util/tsc.h"

u64 perf_time_to_tsc(u64 ns, struct perf_tsc_conversion *tc);
u64 tsc_to_perf_time(u64 cyc, struct perf_tsc_conversion *tc);

#endif
