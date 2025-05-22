/* SPDX-License-Identifier: GPL-2.0 */
/*
 * You SHOULD NOT be including this unless you're vsyscall
 * handling code or timekeeping internal code!
 */

#ifndef _LINUX_TIMEKEEPER_INTERNAL_H
#define _LINUX_TIMEKEEPER_INTERNAL_H

#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/time.h>

/**
 * struct tk_read_base - base structure for timekeeping readout
 * @clock:	Current clocksource used for timekeeping.
 * @mask:	Bitmask for two's complement subtraction of non 64bit clocks
 * @cycle_last: @clock cycle value at last update
 * @mult:	(NTP adjusted) multiplier for scaled math conversion
 * @shift:	Shift value for scaled math conversion
 * @xtime_nsec: Shifted (fractional) nano seconds offset for readout
 * @base:	ktime_t (nanoseconds) base time for readout
 * @base_real:	Nanoseconds base value for clock REALTIME readout
 *
 * This struct has size 56 byte on 64 bit. Together with a seqcount it
 * occupies a single 64byte cache line.
 *
 * The struct is separate from struct timekeeper as it is also used
 * for the fast NMI safe accessors.
 *
 * @base_real is for the fast NMI safe accessor to allow reading clock
 * realtime from any context.
 */
struct tk_read_base {
	struct clocksource	*clock;
	u64			mask;
	u64			cycle_last;
	u32			mult;
	u32			shift;
	u64			xtime_nsec;
	ktime_t			base;
	u64			base_real;
};

/**
 * struct timekeeper - Structure holding internal timekeeping values.
 * @tkr_mono:			The readout base structure for CLOCK_MONOTONIC
 * @xtime_sec:			Current CLOCK_REALTIME time in seconds
 * @ktime_sec:			Current CLOCK_MONOTONIC time in seconds
 * @wall_to_monotonic:		CLOCK_REALTIME to CLOCK_MONOTONIC offset
 * @offs_real:			Offset clock monotonic -> clock realtime
 * @offs_boot:			Offset clock monotonic -> clock boottime
 * @offs_tai:			Offset clock monotonic -> clock tai
 * @coarse_nsec:		The nanoseconds part for coarse time getters
 * @tkr_raw:			The readout base structure for CLOCK_MONOTONIC_RAW
 * @raw_sec:			CLOCK_MONOTONIC_RAW  time in seconds
 * @clock_was_set_seq:		The sequence number of clock was set events
 * @cs_was_changed_seq:		The sequence number of clocksource change events
 * @monotonic_to_boot:		CLOCK_MONOTONIC to CLOCK_BOOTTIME offset
 * @cycle_interval:		Number of clock cycles in one NTP interval
 * @xtime_interval:		Number of clock shifted nano seconds in one NTP
 *				interval.
 * @xtime_remainder:		Shifted nano seconds left over when rounding
 *				@cycle_interval
 * @raw_interval:		Shifted raw nano seconds accumulated per NTP interval.
 * @next_leap_ktime:		CLOCK_MONOTONIC time value of a pending leap-second
 * @ntp_tick:			The ntp_tick_length() value currently being
 *				used. This cached copy ensures we consistently
 *				apply the tick length for an entire tick, as
 *				ntp_tick_length may change mid-tick, and we don't
 *				want to apply that new value to the tick in
 *				progress.
 * @ntp_error:			Difference between accumulated time and NTP time in ntp
 *				shifted nano seconds.
 * @ntp_error_shift:		Shift conversion between clock shifted nano seconds and
 *				ntp shifted nano seconds.
 * @ntp_err_mult:		Multiplication factor for scaled math conversion
 * @skip_second_overflow:	Flag used to avoid updating NTP twice with same second
 * @tai_offset:			The current UTC to TAI offset in seconds
 *
 * Note: For timespec(64) based interfaces wall_to_monotonic is what
 * we need to add to xtime (or xtime corrected for sub jiffy times)
 * to get to monotonic time.  Monotonic is pegged at zero at system
 * boot time, so wall_to_monotonic will be negative, however, we will
 * ALWAYS keep the tv_nsec part positive so we can use the usual
 * normalization.
 *
 * wall_to_monotonic is moved after resume from suspend for the
 * monotonic time not to jump. We need to add total_sleep_time to
 * wall_to_monotonic to get the real boot based time offset.
 *
 * wall_to_monotonic is no longer the boot time, getboottime must be
 * used instead.
 *
 * @monotonic_to_boottime is a timespec64 representation of @offs_boot to
 * accelerate the VDSO update for CLOCK_BOOTTIME.
 *
 * The cacheline ordering of the structure is optimized for in kernel usage of
 * the ktime_get() and ktime_get_ts64() family of time accessors. Struct
 * timekeeper is prepended in the core timekeeping code with a sequence count,
 * which results in the following cacheline layout:
 *
 * 0:	seqcount, tkr_mono
 * 1:	xtime_sec ... coarse_nsec
 * 2:	tkr_raw, raw_sec
 * 3,4: Internal variables
 *
 * Cacheline 0,1 contain the data which is used for accessing
 * CLOCK_MONOTONIC/REALTIME/BOOTTIME/TAI, while cacheline 2 contains the
 * data for accessing CLOCK_MONOTONIC_RAW.  Cacheline 3,4 are internal
 * variables which are only accessed during timekeeper updates once per
 * tick.
 */
struct timekeeper {
	/* Cacheline 0 (together with prepended seqcount of timekeeper core): */
	struct tk_read_base	tkr_mono;

	/* Cacheline 1: */
	u64			xtime_sec;
	unsigned long		ktime_sec;
	struct timespec64	wall_to_monotonic;
	ktime_t			offs_real;
	ktime_t			offs_boot;
	ktime_t			offs_tai;
	u32			coarse_nsec;

	/* Cacheline 2: */
	struct tk_read_base	tkr_raw;
	u64			raw_sec;

	/* Cachline 3 and 4 (timekeeping internal variables): */
	unsigned int		clock_was_set_seq;
	u8			cs_was_changed_seq;

	struct timespec64	monotonic_to_boot;

	u64			cycle_interval;
	u64			xtime_interval;
	s64			xtime_remainder;
	u64			raw_interval;

	ktime_t			next_leap_ktime;
	u64			ntp_tick;
	s64			ntp_error;
	u32			ntp_error_shift;
	u32			ntp_err_mult;
	u32			skip_second_overflow;
	s32			tai_offset;
};

#ifdef CONFIG_GENERIC_TIME_VSYSCALL

extern void update_vsyscall(struct timekeeper *tk);
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
