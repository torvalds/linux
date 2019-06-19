/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/include/linux/timecounter.h
 *
 * based on code that migrated away from
 * linux/include/linux/clocksource.h
 */
#ifndef _LINUX_TIMECOUNTER_H
#define _LINUX_TIMECOUNTER_H

#include <linux/types.h>

/* simplify initialization of mask field */
#define CYCLECOUNTER_MASK(bits) (u64)((bits) < 64 ? ((1ULL<<(bits))-1) : -1)

/**
 * struct cyclecounter - hardware abstraction for a free running counter
 *	Provides completely state-free accessors to the underlying hardware.
 *	Depending on which hardware it reads, the cycle counter may wrap
 *	around quickly. Locking rules (if necessary) have to be defined
 *	by the implementor and user of specific instances of this API.
 *
 * @read:		returns the current cycle value
 * @mask:		bitmask for two's complement
 *			subtraction of non 64 bit counters,
 *			see CYCLECOUNTER_MASK() helper macro
 * @mult:		cycle to nanosecond multiplier
 * @shift:		cycle to nanosecond divisor (power of two)
 */
struct cyclecounter {
	u64 (*read)(const struct cyclecounter *cc);
	u64 mask;
	u32 mult;
	u32 shift;
};

/**
 * struct timecounter - layer above a %struct cyclecounter which counts nanoseconds
 *	Contains the state needed by timecounter_read() to detect
 *	cycle counter wrap around. Initialize with
 *	timecounter_init(). Also used to convert cycle counts into the
 *	corresponding nanosecond counts with timecounter_cyc2time(). Users
 *	of this code are responsible for initializing the underlying
 *	cycle counter hardware, locking issues and reading the time
 *	more often than the cycle counter wraps around. The nanosecond
 *	counter will only wrap around after ~585 years.
 *
 * @cc:			the cycle counter used by this instance
 * @cycle_last:		most recent cycle counter value seen by
 *			timecounter_read()
 * @nsec:		continuously increasing count
 * @mask:		bit mask for maintaining the 'frac' field
 * @frac:		accumulated fractional nanoseconds
 */
struct timecounter {
	const struct cyclecounter *cc;
	u64 cycle_last;
	u64 nsec;
	u64 mask;
	u64 frac;
};

/**
 * cyclecounter_cyc2ns - converts cycle counter cycles to nanoseconds
 * @cc:		Pointer to cycle counter.
 * @cycles:	Cycles
 * @mask:	bit mask for maintaining the 'frac' field
 * @frac:	pointer to storage for the fractional nanoseconds.
 */
static inline u64 cyclecounter_cyc2ns(const struct cyclecounter *cc,
				      u64 cycles, u64 mask, u64 *frac)
{
	u64 ns = (u64) cycles;

	ns = (ns * cc->mult) + *frac;
	*frac = ns & mask;
	return ns >> cc->shift;
}

/**
 * timecounter_adjtime - Shifts the time of the clock.
 * @delta:	Desired change in nanoseconds.
 */
static inline void timecounter_adjtime(struct timecounter *tc, s64 delta)
{
	tc->nsec += delta;
}

/**
 * timecounter_init - initialize a time counter
 * @tc:			Pointer to time counter which is to be initialized/reset
 * @cc:			A cycle counter, ready to be used.
 * @start_tstamp:	Arbitrary initial time stamp.
 *
 * After this call the current cycle register (roughly) corresponds to
 * the initial time stamp. Every call to timecounter_read() increments
 * the time stamp counter by the number of elapsed nanoseconds.
 */
extern void timecounter_init(struct timecounter *tc,
			     const struct cyclecounter *cc,
			     u64 start_tstamp);

/**
 * timecounter_read - return nanoseconds elapsed since timecounter_init()
 *                    plus the initial time stamp
 * @tc:          Pointer to time counter.
 *
 * In other words, keeps track of time since the same epoch as
 * the function which generated the initial time stamp.
 */
extern u64 timecounter_read(struct timecounter *tc);

/**
 * timecounter_cyc2time - convert a cycle counter to same
 *                        time base as values returned by
 *                        timecounter_read()
 * @tc:		Pointer to time counter.
 * @cycle_tstamp:	a value returned by tc->cc->read()
 *
 * Cycle counts that are converted correctly as long as they
 * fall into the interval [-1/2 max cycle count, +1/2 max cycle count],
 * with "max cycle count" == cs->mask+1.
 *
 * This allows conversion of cycle counter values which were generated
 * in the past.
 */
extern u64 timecounter_cyc2time(struct timecounter *tc,
				u64 cycle_tstamp);

#endif
