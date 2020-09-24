// SPDX-License-Identifier: GPL-2.0
/*
 * Timer tick function for architectures that lack generic clockevents,
 * consolidated here from m68k/ia64/parisc/arm.
 */

#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/timekeeper_internal.h>

#include "tick-internal.h"

void legacy_timer_tick(unsigned long ticks)
{
	if (ticks)
		xtime_update(ticks);
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);
}
