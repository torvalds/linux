// SPDX-License-Identifier: GPL-2.0
/*
 * Generic userspace implementations of gettimeofday() and similar.
 */
#include <linux/compiler.h>
#include <linux/math64.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/hrtimer_defs.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

/*
 * The generic vDSO implementation requires that gettimeofday.h
 * provides:
 * - __arch_get_vdso_data(): to get the vdso datapage.
 * - __arch_get_hw_counter(): to get the hw counter based on the
 *   clock_mode.
 * - gettimeofday_fallback(): fallback for gettimeofday.
 * - clock_gettime_fallback(): fallback for clock_gettime.
 * - clock_getres_fallback(): fallback for clock_getres.
 */
#ifdef ENABLE_COMPAT_VDSO
#include <asm/vdso/compat_gettimeofday.h>
#else
#include <asm/vdso/gettimeofday.h>
#endif /* ENABLE_COMPAT_VDSO */

static int do_hres(const struct vdso_data *vd, clockid_t clk,
		   struct __kernel_timespec *ts)
{
	const struct vdso_timestamp *vdso_ts = &vd->basetime[clk];
	u64 cycles, last, sec, ns;
	u32 seq;

	do {
		seq = vdso_read_begin(vd);
		cycles = __arch_get_hw_counter(vd->clock_mode) &
			vd->mask;
		ns = vdso_ts->nsec;
		last = vd->cycle_last;
		if (unlikely((s64)cycles < 0))
			return clock_gettime_fallback(clk, ts);
		if (cycles > last)
			ns += (cycles - last) * vd->mult;
		ns >>= vd->shift;
		sec = vdso_ts->sec;
	} while (unlikely(vdso_read_retry(vd, seq)));

	/*
	 * Do this outside the loop: a race inside the loop could result
	 * in __iter_div_u64_rem() being extremely slow.
	 */
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

static void do_coarse(const struct vdso_data *vd, clockid_t clk,
		      struct __kernel_timespec *ts)
{
	const struct vdso_timestamp *vdso_ts = &vd->basetime[clk];
	u32 seq;

	do {
		seq = vdso_read_begin(vd);
		ts->tv_sec = vdso_ts->sec;
		ts->tv_nsec = vdso_ts->nsec;
	} while (unlikely(vdso_read_retry(vd, seq)));
}

static __maybe_unused int
__cvdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	u32 msk;

	/* Check for negative values or invalid clocks */
	if (unlikely((u32) clock >= MAX_CLOCKS))
		goto fallback;

	/*
	 * Convert the clockid to a bitmask and use it to check which
	 * clocks are handled in the VDSO directly.
	 */
	msk = 1U << clock;
	if (likely(msk & VDSO_HRES)) {
		return do_hres(&vd[CS_HRES_COARSE], clock, ts);
	} else if (msk & VDSO_COARSE) {
		do_coarse(&vd[CS_HRES_COARSE], clock, ts);
		return 0;
	} else if (msk & VDSO_RAW) {
		return do_hres(&vd[CS_RAW], clock, ts);
	}

fallback:
	return clock_gettime_fallback(clock, ts);
}

static __maybe_unused int
__cvdso_clock_gettime32(clockid_t clock, struct old_timespec32 *res)
{
	struct __kernel_timespec ts;
	int ret;

	if (res == NULL)
		goto fallback;

	ret = __cvdso_clock_gettime(clock, &ts);

	if (ret == 0) {
		res->tv_sec = ts.tv_sec;
		res->tv_nsec = ts.tv_nsec;
	}

	return ret;

fallback:
	return clock_gettime_fallback(clock, (struct __kernel_timespec *)res);
}

static __maybe_unused int
__cvdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	const struct vdso_data *vd = __arch_get_vdso_data();

	if (likely(tv != NULL)) {
		struct __kernel_timespec ts;

		if (do_hres(&vd[CS_HRES_COARSE], CLOCK_REALTIME, &ts))
			return gettimeofday_fallback(tv, tz);

		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = (u32)ts.tv_nsec / NSEC_PER_USEC;
	}

	if (unlikely(tz != NULL)) {
		tz->tz_minuteswest = vd[CS_HRES_COARSE].tz_minuteswest;
		tz->tz_dsttime = vd[CS_HRES_COARSE].tz_dsttime;
	}

	return 0;
}

#ifdef VDSO_HAS_TIME
static __maybe_unused time_t __cvdso_time(time_t *time)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	time_t t = READ_ONCE(vd[CS_HRES_COARSE].basetime[CLOCK_REALTIME].sec);

	if (time)
		*time = t;

	return t;
}
#endif /* VDSO_HAS_TIME */

#ifdef VDSO_HAS_CLOCK_GETRES
static __maybe_unused
int __cvdso_clock_getres(clockid_t clock, struct __kernel_timespec *res)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	u64 ns;
	u32 msk;
	u64 hrtimer_res = READ_ONCE(vd[CS_HRES_COARSE].hrtimer_res);

	/* Check for negative values or invalid clocks */
	if (unlikely((u32) clock >= MAX_CLOCKS))
		goto fallback;

	/*
	 * Convert the clockid to a bitmask and use it to check which
	 * clocks are handled in the VDSO directly.
	 */
	msk = 1U << clock;
	if (msk & VDSO_HRES) {
		/*
		 * Preserves the behaviour of posix_get_hrtimer_res().
		 */
		ns = hrtimer_res;
	} else if (msk & VDSO_COARSE) {
		/*
		 * Preserves the behaviour of posix_get_coarse_res().
		 */
		ns = LOW_RES_NSEC;
	} else if (msk & VDSO_RAW) {
		/*
		 * Preserves the behaviour of posix_get_hrtimer_res().
		 */
		ns = hrtimer_res;
	} else {
		goto fallback;
	}

	if (res) {
		res->tv_sec = 0;
		res->tv_nsec = ns;
	}

	return 0;

fallback:
	return clock_getres_fallback(clock, res);
}

static __maybe_unused int
__cvdso_clock_getres_time32(clockid_t clock, struct old_timespec32 *res)
{
	struct __kernel_timespec ts;
	int ret;

	if (res == NULL)
		goto fallback;

	ret = __cvdso_clock_getres(clock, &ts);

	if (ret == 0) {
		res->tv_sec = ts.tv_sec;
		res->tv_nsec = ts.tv_nsec;
	}

	return ret;

fallback:
	return clock_getres_fallback(clock, (struct __kernel_timespec *)res);
}
#endif /* VDSO_HAS_CLOCK_GETRES */
