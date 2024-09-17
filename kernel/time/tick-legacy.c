// SPDX-License-Identifier: GPL-2.0
/*
 * Timer tick function for architectures that lack generic clockevents,
 * consolidated here from m68k/ia64/parisc/arm.
 */

#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/timekeeper_internal.h>

#include "tick-internal.h"

/**
 * legacy_timer_tick() - advances the timekeeping infrastructure
 * @ticks:	number of ticks, that have elapsed since the last call.
 *
 * This is used by platforms that have not been converted to
 * generic clockevents.
 *
 * If 'ticks' is zero, the CPU is not handling timekeeping, so
 * only perform process accounting and profiling.
 *
 * Must be called with interrupts disabled.
 */
void legacy_timer_tick(unsigned long ticks)
{
	if (ticks) {
		raw_spin_lock(&jiffies_lock);
		write_seqcount_begin(&jiffies_seq);
		do_timer(ticks);
		write_seqcount_end(&jiffies_seq);
		raw_spin_unlock(&jiffies_lock);
		update_wall_time();
	}
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);
}
