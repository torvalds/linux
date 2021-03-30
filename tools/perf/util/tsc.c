// SPDX-License-Identifier: GPL-2.0
#include <errno.h>

#include <linux/compiler.h>
#include <linux/perf_event.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include <asm/barrier.h>

#include "event.h"
#include "synthetic-events.h"
#include "debug.h"
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

	if (tc->cap_user_time_short)
		cyc = tc->time_cycles +
			((cyc - tc->time_cycles) & tc->time_mask);

	quot = cyc >> tc->time_shift;
	rem  = cyc & (((u64)1 << tc->time_shift) - 1);
	return tc->time_zero + quot * tc->time_mult +
	       ((rem * tc->time_mult) >> tc->time_shift);
}

int perf_read_tsc_conversion(const struct perf_event_mmap_page *pc,
			     struct perf_tsc_conversion *tc)
{
	u32 seq;
	int i = 0;

	while (1) {
		seq = pc->lock;
		rmb();
		tc->time_mult = pc->time_mult;
		tc->time_shift = pc->time_shift;
		tc->time_zero = pc->time_zero;
		tc->time_cycles = pc->time_cycles;
		tc->time_mask = pc->time_mask;
		tc->cap_user_time_zero = pc->cap_user_time_zero;
		tc->cap_user_time_short	= pc->cap_user_time_short;
		rmb();
		if (pc->lock == seq && !(seq & 1))
			break;
		if (++i > 10000) {
			pr_debug("failed to get perf_event_mmap_page lock\n");
			return -EINVAL;
		}
	}

	if (!tc->cap_user_time_zero)
		return -EOPNOTSUPP;

	return 0;
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
				.size = sizeof(struct perf_record_time_conv),
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
	event.time_conv.time_cycles = tc.time_cycles;
	event.time_conv.time_mask = tc.time_mask;
	event.time_conv.cap_user_time_zero = tc.cap_user_time_zero;
	event.time_conv.cap_user_time_short = tc.cap_user_time_short;

	return process(tool, &event, NULL, machine);
}

u64 __weak rdtsc(void)
{
	return 0;
}
