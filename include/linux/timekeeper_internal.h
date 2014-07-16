/*
 * You SHOULD NOT be including this unless you're vsyscall
 * handling code or timekeeping internal code!
 */

#ifndef _LINUX_TIMEKEEPER_INTERNAL_H
#define _LINUX_TIMEKEEPER_INTERNAL_H

#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/time.h>

/*
 * Structure holding internal timekeeping values.
 *
 * Note: wall_to_monotonic is what we need to add to xtime (or xtime
 * corrected for sub jiffie times) to get to monotonic time.
 * Monotonic is pegged at zero at system boot time, so
 * wall_to_monotonic will be negative, however, we will ALWAYS keep
 * the tv_nsec part positive so we can use the usual normalization.
 *
 * wall_to_monotonic is moved after resume from suspend for the
 * monotonic time not to jump. To calculate the real boot time offset
 * we need to do offs_real - offs_boot.
 *
 * - wall_to_monotonic is no longer the boot time, getboottime must be
 * used instead.
 */
struct timekeeper {
	/* Current clocksource used for timekeeping. */
	struct clocksource	*clock;
	/* NTP adjusted clock multiplier */
	u32			mult;
	/* The shift value of the current clocksource. */
	u32			shift;
	/* Clock shifted nano seconds */
	u64			xtime_nsec;

	/* Monotonic base time */
	ktime_t			base_mono;

	/* Current CLOCK_REALTIME time in seconds */
	u64			xtime_sec;
	/* CLOCK_REALTIME to CLOCK_MONOTONIC offset */
	struct timespec64	wall_to_monotonic;

	/* Offset clock monotonic -> clock realtime */
	ktime_t			offs_real;
	/* Offset clock monotonic -> clock boottime */
	ktime_t			offs_boot;
	/* Offset clock monotonic -> clock tai */
	ktime_t			offs_tai;

	/* The current UTC to TAI offset in seconds */
	s32			tai_offset;

	/* The raw monotonic time for the CLOCK_MONOTONIC_RAW posix clock. */
	struct timespec64	raw_time;

	/* Number of clock cycles in one NTP interval. */
	cycle_t			cycle_interval;
	/* Last cycle value (also stored in clock->cycle_last) */
	cycle_t			cycle_last;
	/* Number of clock shifted nano seconds in one NTP interval. */
	u64			xtime_interval;
	/* shifted nano seconds left over when rounding cycle_interval */
	s64			xtime_remainder;
	/* Raw nano seconds accumulated per NTP interval. */
	u32			raw_interval;

	/*
	 * Difference between accumulated time and NTP time in ntp
	 * shifted nano seconds.
	 */
	s64			ntp_error;
	/*
	 * Shift conversion between clock shifted nano seconds and
	 * ntp shifted nano seconds.
	 */
	u32			ntp_error_shift;
};

#ifdef CONFIG_GENERIC_TIME_VSYSCALL

extern void update_vsyscall(struct timekeeper *tk);
extern void update_vsyscall_tz(void);

#elif defined(CONFIG_GENERIC_TIME_VSYSCALL_OLD)

extern void update_vsyscall_old(struct timespec *ts, struct timespec *wtm,
				struct clocksource *c, u32 mult);
extern void update_vsyscall_tz(void);

#else

static inline void update_vsyscall(struct timekeeper *tk)
{
}
static inline void update_vsyscall_tz(void)
{
}
#endif

#endif /* _LINUX_TIMEKEEPER_INTERNAL_H */
