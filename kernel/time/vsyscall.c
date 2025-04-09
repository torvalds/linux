// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 ARM Ltd.
 *
 * Generic implementation of update_vsyscall and update_vsyscall_tz.
 *
 * Based on the x86 specific implementation.
 */

#include <linux/hrtimer.h>
#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>

#include "timekeeping_internal.h"

static inline void update_vdso_time_data(struct vdso_time_data *vdata, struct timekeeper *tk)
{
	struct vdso_clock *vc = vdata->clock_data;
	struct vdso_timestamp *vdso_ts;
	u64 nsec, sec;

	vc[CS_HRES_COARSE].cycle_last	= tk->tkr_mono.cycle_last;
#ifdef CONFIG_GENERIC_VDSO_OVERFLOW_PROTECT
	vc[CS_HRES_COARSE].max_cycles	= tk->tkr_mono.clock->max_cycles;
#endif
	vc[CS_HRES_COARSE].mask		= tk->tkr_mono.mask;
	vc[CS_HRES_COARSE].mult		= tk->tkr_mono.mult;
	vc[CS_HRES_COARSE].shift	= tk->tkr_mono.shift;
	vc[CS_RAW].cycle_last		= tk->tkr_raw.cycle_last;
#ifdef CONFIG_GENERIC_VDSO_OVERFLOW_PROTECT
	vc[CS_RAW].max_cycles		= tk->tkr_raw.clock->max_cycles;
#endif
	vc[CS_RAW].mask			= tk->tkr_raw.mask;
	vc[CS_RAW].mult			= tk->tkr_raw.mult;
	vc[CS_RAW].shift		= tk->tkr_raw.shift;

	/* CLOCK_MONOTONIC */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_MONOTONIC];
	vdso_ts->sec	= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;

	nsec = tk->tkr_mono.xtime_nsec;
	nsec += ((u64)tk->wall_to_monotonic.tv_nsec << tk->tkr_mono.shift);
	while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		nsec -= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
		vdso_ts->sec++;
	}
	vdso_ts->nsec	= nsec;

	/* Copy MONOTONIC time for BOOTTIME */
	sec	= vdso_ts->sec;
	/* Add the boot offset */
	sec	+= tk->monotonic_to_boot.tv_sec;
	nsec	+= (u64)tk->monotonic_to_boot.tv_nsec << tk->tkr_mono.shift;

	/* CLOCK_BOOTTIME */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_BOOTTIME];
	vdso_ts->sec	= sec;

	while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		nsec -= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
		vdso_ts->sec++;
	}
	vdso_ts->nsec	= nsec;

	/* CLOCK_MONOTONIC_RAW */
	vdso_ts		= &vc[CS_RAW].basetime[CLOCK_MONOTONIC_RAW];
	vdso_ts->sec	= tk->raw_sec;
	vdso_ts->nsec	= tk->tkr_raw.xtime_nsec;

	/* CLOCK_TAI */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_TAI];
	vdso_ts->sec	= tk->xtime_sec + (s64)tk->tai_offset;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec;
}

void update_vsyscall(struct timekeeper *tk)
{
	struct vdso_time_data *vdata = vdso_k_time_data;
	struct vdso_clock *vc = vdata->clock_data;
	struct vdso_timestamp *vdso_ts;
	s32 clock_mode;
	u64 nsec;

	/* copy vsyscall data */
	vdso_write_begin(vdata);

	clock_mode = tk->tkr_mono.clock->vdso_clock_mode;
	vc[CS_HRES_COARSE].clock_mode	= clock_mode;
	vc[CS_RAW].clock_mode		= clock_mode;

	/* CLOCK_REALTIME also required for time() */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_REALTIME];
	vdso_ts->sec	= tk->xtime_sec;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec;

	/* CLOCK_REALTIME_COARSE */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_REALTIME_COARSE];
	vdso_ts->sec	= tk->xtime_sec;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;

	/* CLOCK_MONOTONIC_COARSE */
	vdso_ts		= &vc[CS_HRES_COARSE].basetime[CLOCK_MONOTONIC_COARSE];
	vdso_ts->sec	= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
	nsec		= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;
	nsec		= nsec + tk->wall_to_monotonic.tv_nsec;
	vdso_ts->sec	+= __iter_div_u64_rem(nsec, NSEC_PER_SEC, &vdso_ts->nsec);

	/*
	 * Read without the seqlock held by clock_getres().
	 */
	WRITE_ONCE(vdata->hrtimer_res, hrtimer_resolution);

	/*
	 * If the current clocksource is not VDSO capable, then spare the
	 * update of the high resolution parts.
	 */
	if (clock_mode != VDSO_CLOCKMODE_NONE)
		update_vdso_time_data(vdata, tk);

	__arch_update_vsyscall(vdata);

	vdso_write_end(vdata);

	__arch_sync_vdso_time_data(vdata);
}

void update_vsyscall_tz(void)
{
	struct vdso_time_data *vdata = vdso_k_time_data;

	vdata->tz_minuteswest = sys_tz.tz_minuteswest;
	vdata->tz_dsttime = sys_tz.tz_dsttime;

	__arch_sync_vdso_time_data(vdata);
}

/**
 * vdso_update_begin - Start of a VDSO update section
 *
 * Allows architecture code to safely update the architecture specific VDSO
 * data. Disables interrupts, acquires timekeeper lock to serialize against
 * concurrent updates from timekeeping and invalidates the VDSO data
 * sequence counter to prevent concurrent readers from accessing
 * inconsistent data.
 *
 * Returns: Saved interrupt flags which need to be handed in to
 * vdso_update_end().
 */
unsigned long vdso_update_begin(void)
{
	struct vdso_time_data *vdata = vdso_k_time_data;
	unsigned long flags = timekeeper_lock_irqsave();

	vdso_write_begin(vdata);
	return flags;
}

/**
 * vdso_update_end - End of a VDSO update section
 * @flags:	Interrupt flags as returned from vdso_update_begin()
 *
 * Pairs with vdso_update_begin(). Marks vdso data consistent, invokes data
 * synchronization if the architecture requires it, drops timekeeper lock
 * and restores interrupt flags.
 */
void vdso_update_end(unsigned long flags)
{
	struct vdso_time_data *vdata = vdso_k_time_data;

	vdso_write_end(vdata);
	__arch_sync_vdso_time_data(vdata);
	timekeeper_unlock_irqrestore(flags);
}
