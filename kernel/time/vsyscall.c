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

static inline void update_vdso_data(struct vdso_data *vdata,
				    struct timekeeper *tk)
{
	struct vdso_timestamp *vdso_ts;
	u64 nsec;

	vdata[CS_HRES_COARSE].cycle_last	= tk->tkr_mono.cycle_last;
	vdata[CS_HRES_COARSE].mask		= tk->tkr_mono.mask;
	vdata[CS_HRES_COARSE].mult		= tk->tkr_mono.mult;
	vdata[CS_HRES_COARSE].shift		= tk->tkr_mono.shift;
	vdata[CS_RAW].cycle_last		= tk->tkr_raw.cycle_last;
	vdata[CS_RAW].mask			= tk->tkr_raw.mask;
	vdata[CS_RAW].mult			= tk->tkr_raw.mult;
	vdata[CS_RAW].shift			= tk->tkr_raw.shift;

	/* CLOCK_REALTIME */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_REALTIME];
	vdso_ts->sec	= tk->xtime_sec;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec;

	/* CLOCK_MONOTONIC */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_MONOTONIC];
	vdso_ts->sec	= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;

	nsec = tk->tkr_mono.xtime_nsec;
	nsec += ((u64)tk->wall_to_monotonic.tv_nsec << tk->tkr_mono.shift);
	while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		nsec -= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
		vdso_ts->sec++;
	}
	vdso_ts->nsec	= nsec;

	/* CLOCK_MONOTONIC_RAW */
	vdso_ts		= &vdata[CS_RAW].basetime[CLOCK_MONOTONIC_RAW];
	vdso_ts->sec	= tk->raw_sec;
	vdso_ts->nsec	= tk->tkr_raw.xtime_nsec;

	/* CLOCK_BOOTTIME */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_BOOTTIME];
	vdso_ts->sec	= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
	nsec = tk->tkr_mono.xtime_nsec;
	nsec += ((u64)(tk->wall_to_monotonic.tv_nsec +
		       ktime_to_ns(tk->offs_boot)) << tk->tkr_mono.shift);
	while (nsec >= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		nsec -= (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift);
		vdso_ts->sec++;
	}
	vdso_ts->nsec	= nsec;

	/* CLOCK_TAI */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_TAI];
	vdso_ts->sec	= tk->xtime_sec + (s64)tk->tai_offset;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec;

	/*
	 * Read without the seqlock held by clock_getres().
	 * Note: No need to have a second copy.
	 */
	WRITE_ONCE(vdata[CS_HRES_COARSE].hrtimer_res, hrtimer_resolution);
}

void update_vsyscall(struct timekeeper *tk)
{
	struct vdso_data *vdata = __arch_get_k_vdso_data();
	struct vdso_timestamp *vdso_ts;
	u64 nsec;

	if (__arch_update_vdso_data()) {
		/*
		 * Some architectures might want to skip the update of the
		 * data page.
		 */
		return;
	}

	/* copy vsyscall data */
	vdso_write_begin(vdata);

	vdata[CS_HRES_COARSE].clock_mode	= __arch_get_clock_mode(tk);
	vdata[CS_RAW].clock_mode		= __arch_get_clock_mode(tk);

	/* CLOCK_REALTIME_COARSE */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_REALTIME_COARSE];
	vdso_ts->sec	= tk->xtime_sec;
	vdso_ts->nsec	= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;

	/* CLOCK_MONOTONIC_COARSE */
	vdso_ts		= &vdata[CS_HRES_COARSE].basetime[CLOCK_MONOTONIC_COARSE];
	vdso_ts->sec	= tk->xtime_sec + tk->wall_to_monotonic.tv_sec;
	nsec		= tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift;
	nsec		= nsec + tk->wall_to_monotonic.tv_nsec;
	vdso_ts->sec	+= __iter_div_u64_rem(nsec, NSEC_PER_SEC, &vdso_ts->nsec);

	if (__arch_use_vsyscall(vdata))
		update_vdso_data(vdata, tk);

	__arch_update_vsyscall(vdata, tk);

	vdso_write_end(vdata);

	__arch_sync_vdso_data(vdata);
}

void update_vsyscall_tz(void)
{
	struct vdso_data *vdata = __arch_get_k_vdso_data();

	if (__arch_use_vsyscall(vdata)) {
		vdata[CS_HRES_COARSE].tz_minuteswest = sys_tz.tz_minuteswest;
		vdata[CS_HRES_COARSE].tz_dsttime = sys_tz.tz_dsttime;
	}

	__arch_sync_vdso_data(vdata);
}
