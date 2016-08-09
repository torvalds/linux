#include <stdbool.h>
#include <errno.h>

#include <linux/perf_event.h>

#include "../../perf.h"
#include <linux/types.h>
#include "../../util/debug.h"
#include "../../util/tsc.h"

int perf_read_tsc_conversion(const struct perf_event_mmap_page *pc,
			     struct perf_tsc_conversion *tc)
{
	bool cap_user_time_zero;
	u32 seq;
	int i = 0;

	while (1) {
		seq = pc->lock;
		rmb();
		tc->time_mult = pc->time_mult;
		tc->time_shift = pc->time_shift;
		tc->time_zero = pc->time_zero;
		cap_user_time_zero = pc->cap_user_time_zero;
		rmb();
		if (pc->lock == seq && !(seq & 1))
			break;
		if (++i > 10000) {
			pr_debug("failed to get perf_event_mmap_page lock\n");
			return -EINVAL;
		}
	}

	if (!cap_user_time_zero)
		return -EOPNOTSUPP;

	return 0;
}

u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((u64)high) << 32;
}

int perf_event__synth_time_conv(const struct perf_event_mmap_page *pc,
				struct perf_tool *tool,
				perf_event__handler_t process,
				struct machine *machine)
{
	union perf_event event = {
		.time_conv = {
			.header = {
				.type = PERF_RECORD_TIME_CONV,
				.size = sizeof(struct time_conv_event),
			},
		},
	};
	struct perf_tsc_conversion tc;
	int err;

	if (!pc)
		return 0;
	err = perf_read_tsc_conversion(pc, &tc);
	if (err == -EOPNOTSUPP)
		return 0;
	if (err)
		return err;

	pr_debug2("Synthesizing TSC conversion information\n");

	event.time_conv.time_mult  = tc.time_mult;
	event.time_conv.time_shift = tc.time_shift;
	event.time_conv.time_zero  = tc.time_zero;

	return process(tool, &event, NULL, machine);
}
