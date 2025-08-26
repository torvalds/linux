// SPDX-License-Identifier: GPL-2.0
/*
 * Generic userspace implementations of gettimeofday() and similar.
 */
#include <vdso/auxclock.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

/* Bring in default accessors */
#include <vdso/vsyscall.h>

#ifndef vdso_calc_ns

#ifdef VDSO_DELTA_NOMASK
# define VDSO_DELTA_MASK(vd)	ULLONG_MAX
#else
# define VDSO_DELTA_MASK(vd)	(vd->mask)
#endif

#ifdef CONFIG_GENERIC_VDSO_OVERFLOW_PROTECT
static __always_inline bool vdso_delta_ok(const struct vdso_clock *vc, u64 delta)
{
	return delta < vc->max_cycles;
}
#else
static __always_inline bool vdso_delta_ok(const struct vdso_clock *vc, u64 delta)
{
	return true;
}
#endif

#ifndef vdso_shift_ns
static __always_inline u64 vdso_shift_ns(u64 ns, u32 shift)
{
	return ns >> shift;
}
#endif

/*
 * Default implementation which works for all sane clocksources. That
 * obviously excludes x86/TSC.
 */
static __always_inline u64 vdso_calc_ns(const struct vdso_clock *vc, u64 cycles, u64 base)
{
	u64 delta = (cycles - vc->cycle_last) & VDSO_DELTA_MASK(vc);

	if (likely(vdso_delta_ok(vc, delta)))
		return vdso_shift_ns((delta * vc->mult) + base, vc->shift);

	return mul_u64_u32_add_u64_shr(delta, vc->mult, base, vc->shift);
}
#endif /* vdso_calc_ns */

#ifndef __arch_vdso_hres_capable
static inline bool __arch_vdso_hres_capable(void)
{
	return true;
}
#endif

#ifndef vdso_clocksource_ok
static inline bool vdso_clocksource_ok(const struct vdso_clock *vc)
{
	return vc->clock_mode != VDSO_CLOCKMODE_NONE;
}
#endif

#ifndef vdso_cycles_ok
static inline bool vdso_cycles_ok(u64 cycles)
{
	return true;
}
#endif

static __always_inline bool vdso_clockid_valid(clockid_t clock)
{
	/* Check for negative values or invalid clocks */
	return likely((u32) clock <= CLOCK_AUX_LAST);
}

/*
 * Must not be invoked within the sequence read section as a race inside
 * that loop could result in __iter_div_u64_rem() being extremely slow.
 */
static __always_inline void vdso_set_timespec(struct __kernel_timespec *ts, u64 sec, u64 ns)
{
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;
}

static __always_inline
bool vdso_get_timestamp(const struct vdso_time_data *vd, const struct vdso_clock *vc,
			unsigned int clkidx, u64 *sec, u64 *ns)
{
	const struct vdso_timestamp *vdso_ts = &vc->basetime[clkidx];
	u64 cycles;

	if (unlikely(!vdso_clocksource_ok(vc)))
		return false;

	cycles = __arch_get_hw_counter(vc->clock_mode, vd);
	if (unlikely(!vdso_cycles_ok(cycles)))
		return false;

	*ns = vdso_calc_ns(vc, cycles, vdso_ts->nsec);
	*sec = vdso_ts->sec;

	return true;
}

static __always_inline
const struct vdso_time_data *__arch_get_vdso_u_timens_data(const struct vdso_time_data *vd)
{
	return (void *)vd + PAGE_SIZE;
}

static __always_inline
bool do_hres_timens(const struct vdso_time_data *vdns, const struct vdso_clock *vcns,
		    clockid_t clk, struct __kernel_timespec *ts)
{
	const struct vdso_time_data *vd = __arch_get_vdso_u_timens_data(vdns);
	const struct timens_offset *offs = &vcns->offset[clk];
	const struct vdso_clock *vc = vd->clock_data;
	u32 seq;
	s64 sec;
	u64 ns;

	if (clk != CLOCK_MONOTONIC_RAW)
		vc = &vc[CS_HRES_COARSE];
	else
		vc = &vc[CS_RAW];

	do {
		seq = vdso_read_begin(vc);

		if (!vdso_get_timestamp(vd, vc, clk, &sec, &ns))
			return false;
	} while (unlikely(vdso_read_retry(vc, seq)));

	/* Add the namespace offset */
	sec += offs->sec;
	ns += offs->nsec;

	vdso_set_timespec(ts, sec, ns);

	return true;
}

static __always_inline
bool do_hres(const struct vdso_time_data *vd, const struct vdso_clock *vc,
	     clockid_t clk, struct __kernel_timespec *ts)
{
	u64 sec, ns;
	u32 seq;

	/* Allows to compile the high resolution parts out */
	if (!__arch_vdso_hres_capable())
		return false;

	do {
		/*
		 * Open coded function vdso_read_begin() to handle
		 * VDSO_CLOCKMODE_TIMENS. Time namespace enabled tasks have a
		 * special VVAR page installed which has vc->seq set to 1 and
		 * vc->clock_mode set to VDSO_CLOCKMODE_TIMENS. For non time
		 * namespace affected tasks this does not affect performance
		 * because if vc->seq is odd, i.e. a concurrent update is in
		 * progress the extra check for vc->clock_mode is just a few
		 * extra instructions while spin waiting for vc->seq to become
		 * even again.
		 */
		while (unlikely((seq = READ_ONCE(vc->seq)) & 1)) {
			if (IS_ENABLED(CONFIG_TIME_NS) &&
			    vc->clock_mode == VDSO_CLOCKMODE_TIMENS)
				return do_hres_timens(vd, vc, clk, ts);
			cpu_relax();
		}
		smp_rmb();

		if (!vdso_get_timestamp(vd, vc, clk, &sec, &ns))
			return false;
	} while (unlikely(vdso_read_retry(vc, seq)));

	vdso_set_timespec(ts, sec, ns);

	return true;
}

static __always_inline
bool do_coarse_timens(const struct vdso_time_data *vdns, const struct vdso_clock *vcns,
		      clockid_t clk, struct __kernel_timespec *ts)
{
	const struct vdso_time_data *vd = __arch_get_vdso_u_timens_data(vdns);
	const struct timens_offset *offs = &vcns->offset[clk];
	const struct vdso_clock *vc = vd->clock_data;
	const struct vdso_timestamp *vdso_ts;
	u64 nsec;
	s64 sec;
	s32 seq;

	vdso_ts = &vc->basetime[clk];

	do {
		seq = vdso_read_begin(vc);
		sec = vdso_ts->sec;
		nsec = vdso_ts->nsec;
	} while (unlikely(vdso_read_retry(vc, seq)));

	/* Add the namespace offset */
	sec += offs->sec;
	nsec += offs->nsec;

	vdso_set_timespec(ts, sec, nsec);

	return true;
}

static __always_inline
bool do_coarse(const struct vdso_time_data *vd, const struct vdso_clock *vc,
	       clockid_t clk, struct __kernel_timespec *ts)
{
	const struct vdso_timestamp *vdso_ts = &vc->basetime[clk];
	u32 seq;

	do {
		/*
		 * Open coded function vdso_read_begin() to handle
		 * VDSO_CLOCK_TIMENS. See comment in do_hres().
		 */
		while ((seq = READ_ONCE(vc->seq)) & 1) {
			if (IS_ENABLED(CONFIG_TIME_NS) &&
			    vc->clock_mode == VDSO_CLOCKMODE_TIMENS)
				return do_coarse_timens(vd, vc, clk, ts);
			cpu_relax();
		}
		smp_rmb();

		ts->tv_sec = vdso_ts->sec;
		ts->tv_nsec = vdso_ts->nsec;
	} while (unlikely(vdso_read_retry(vc, seq)));

	return true;
}

static __always_inline
bool do_aux(const struct vdso_time_data *vd, clockid_t clock, struct __kernel_timespec *ts)
{
	const struct vdso_clock *vc;
	u32 seq, idx;
	u64 sec, ns;

	if (!IS_ENABLED(CONFIG_POSIX_AUX_CLOCKS))
		return false;

	idx = clock - CLOCK_AUX;
	vc = &vd->aux_clock_data[idx];

	do {
		/*
		 * Open coded function vdso_read_begin() to handle
		 * VDSO_CLOCK_TIMENS. See comment in do_hres().
		 */
		while ((seq = READ_ONCE(vc->seq)) & 1) {
			if (IS_ENABLED(CONFIG_TIME_NS) && vc->clock_mode == VDSO_CLOCKMODE_TIMENS) {
				vd = __arch_get_vdso_u_timens_data(vd);
				vc = &vd->aux_clock_data[idx];
				/* Re-read from the real time data page */
				continue;
			}
			cpu_relax();
		}
		smp_rmb();

		/* Auxclock disabled? */
		if (vc->clock_mode == VDSO_CLOCKMODE_NONE)
			return false;

		if (!vdso_get_timestamp(vd, vc, VDSO_BASE_AUX, &sec, &ns))
			return false;
	} while (unlikely(vdso_read_retry(vc, seq)));

	vdso_set_timespec(ts, sec, ns);

	return true;
}

static __always_inline bool
__cvdso_clock_gettime_common(const struct vdso_time_data *vd, clockid_t clock,
			     struct __kernel_timespec *ts)
{
	const struct vdso_clock *vc = vd->clock_data;
	u32 msk;

	if (!vdso_clockid_valid(clock))
		return false;

	/*
	 * Convert the clockid to a bitmask and use it to check which
	 * clocks are handled in the VDSO directly.
	 */
	msk = 1U << clock;
	if (likely(msk & VDSO_HRES))
		vc = &vc[CS_HRES_COARSE];
	else if (msk & VDSO_COARSE)
		return do_coarse(vd, &vc[CS_HRES_COARSE], clock, ts);
	else if (msk & VDSO_RAW)
		vc = &vc[CS_RAW];
	else if (msk & VDSO_AUX)
		return do_aux(vd, clock, ts);
	else
		return false;

	return do_hres(vd, vc, clock, ts);
}

static __maybe_unused int
__cvdso_clock_gettime_data(const struct vdso_time_data *vd, clockid_t clock,
			   struct __kernel_timespec *ts)
{
	bool ok;

	ok = __cvdso_clock_gettime_common(vd, clock, ts);

	if (unlikely(!ok))
		return clock_gettime_fallback(clock, ts);
	return 0;
}

static __maybe_unused int
__cvdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime_data(__arch_get_vdso_u_time_data(), clock, ts);
}

#ifdef BUILD_VDSO32
static __maybe_unused int
__cvdso_clock_gettime32_data(const struct vdso_time_data *vd, clockid_t clock,
			     struct old_timespec32 *res)
{
	struct __kernel_timespec ts;
	bool ok;

	ok = __cvdso_clock_gettime_common(vd, clock, &ts);

	if (unlikely(!ok))
		return clock_gettime32_fallback(clock, res);

	/* For ok == true */
	res->tv_sec = ts.tv_sec;
	res->tv_nsec = ts.tv_nsec;

	return 0;
}

static __maybe_unused int
__cvdso_clock_gettime32(clockid_t clock, struct old_timespec32 *res)
{
	return __cvdso_clock_gettime32_data(__arch_get_vdso_u_time_data(), clock, res);
}
#endif /* BUILD_VDSO32 */

static __maybe_unused int
__cvdso_gettimeofday_data(const struct vdso_time_data *vd,
			  struct __kernel_old_timeval *tv, struct timezone *tz)
{
	const struct vdso_clock *vc = vd->clock_data;

	if (likely(tv != NULL)) {
		struct __kernel_timespec ts;

		if (!do_hres(vd, &vc[CS_HRES_COARSE], CLOCK_REALTIME, &ts))
			return gettimeofday_fallback(tv, tz);

		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = (u32)ts.tv_nsec / NSEC_PER_USEC;
	}

	if (unlikely(tz != NULL)) {
		if (IS_ENABLED(CONFIG_TIME_NS) &&
		    vc->clock_mode == VDSO_CLOCKMODE_TIMENS)
			vd = __arch_get_vdso_u_timens_data(vd);

		tz->tz_minuteswest = vd[CS_HRES_COARSE].tz_minuteswest;
		tz->tz_dsttime = vd[CS_HRES_COARSE].tz_dsttime;
	}

	return 0;
}

static __maybe_unused int
__cvdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	return __cvdso_gettimeofday_data(__arch_get_vdso_u_time_data(), tv, tz);
}

#ifdef VDSO_HAS_TIME
static __maybe_unused __kernel_old_time_t
__cvdso_time_data(const struct vdso_time_data *vd, __kernel_old_time_t *time)
{
	const struct vdso_clock *vc = vd->clock_data;
	__kernel_old_time_t t;

	if (IS_ENABLED(CONFIG_TIME_NS) &&
	    vc->clock_mode == VDSO_CLOCKMODE_TIMENS) {
		vd = __arch_get_vdso_u_timens_data(vd);
		vc = vd->clock_data;
	}

	t = READ_ONCE(vc[CS_HRES_COARSE].basetime[CLOCK_REALTIME].sec);

	if (time)
		*time = t;

	return t;
}

static __maybe_unused __kernel_old_time_t __cvdso_time(__kernel_old_time_t *time)
{
	return __cvdso_time_data(__arch_get_vdso_u_time_data(), time);
}
#endif /* VDSO_HAS_TIME */

#ifdef VDSO_HAS_CLOCK_GETRES
static __maybe_unused
bool __cvdso_clock_getres_common(const struct vdso_time_data *vd, clockid_t clock,
				 struct __kernel_timespec *res)
{
	const struct vdso_clock *vc = vd->clock_data;
	u32 msk;
	u64 ns;

	if (!vdso_clockid_valid(clock))
		return false;

	if (IS_ENABLED(CONFIG_TIME_NS) &&
	    vc->clock_mode == VDSO_CLOCKMODE_TIMENS)
		vd = __arch_get_vdso_u_timens_data(vd);

	/*
	 * Convert the clockid to a bitmask and use it to check which
	 * clocks are handled in the VDSO directly.
	 */
	msk = 1U << clock;
	if (msk & (VDSO_HRES | VDSO_RAW)) {
		/*
		 * Preserves the behaviour of posix_get_hrtimer_res().
		 */
		ns = READ_ONCE(vd->hrtimer_res);
	} else if (msk & VDSO_COARSE) {
		/*
		 * Preserves the behaviour of posix_get_coarse_res().
		 */
		ns = LOW_RES_NSEC;
	} else if (msk & VDSO_AUX) {
		ns = aux_clock_resolution_ns();
	} else {
		return false;
	}

	if (likely(res)) {
		res->tv_sec = 0;
		res->tv_nsec = ns;
	}
	return true;
}

static __maybe_unused
int __cvdso_clock_getres_data(const struct vdso_time_data *vd, clockid_t clock,
			      struct __kernel_timespec *res)
{
	bool ok;

	ok =  __cvdso_clock_getres_common(vd, clock, res);

	if (unlikely(!ok))
		return clock_getres_fallback(clock, res);
	return 0;
}

static __maybe_unused
int __cvdso_clock_getres(clockid_t clock, struct __kernel_timespec *res)
{
	return __cvdso_clock_getres_data(__arch_get_vdso_u_time_data(), clock, res);
}

#ifdef BUILD_VDSO32
static __maybe_unused int
__cvdso_clock_getres_time32_data(const struct vdso_time_data *vd, clockid_t clock,
				 struct old_timespec32 *res)
{
	struct __kernel_timespec ts;
	bool ok;

	ok = __cvdso_clock_getres_common(vd, clock, &ts);

	if (unlikely(!ok))
		return clock_getres32_fallback(clock, res);

	if (likely(res)) {
		res->tv_sec = ts.tv_sec;
		res->tv_nsec = ts.tv_nsec;
	}
	return 0;
}

static __maybe_unused int
__cvdso_clock_getres_time32(clockid_t clock, struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32_data(__arch_get_vdso_u_time_data(),
						clock, res);
}
#endif /* BUILD_VDSO32 */
#endif /* VDSO_HAS_CLOCK_GETRES */
