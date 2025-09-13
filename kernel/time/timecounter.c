// SPDX-License-Identifier: GPL-2.0+
/*
 * Based on clocksource code. See commit 74d23cc704d1
 */
#include <linux/export.h>
#include <linux/timecounter.h>
#include <linux/bits.h>

/**
 * timecounter_init - initialize a time counter
 * @tc:         Pointer to timecounter to be initialized
 * @cc:         Pointer to cycle counter
 * @start_tstamp: Initial timestamp in nanoseconds
 *
 * Initializes the time counter with the given cycle counter and start timestamp.
 */
void timecounter_init(struct timecounter *tc,
		      struct cyclecounter *cc,
		      u64 start_tstamp)
{
	tc->cc = cc;
	tc->cycle_last = cc->read(cc);
	tc->nsec = start_tstamp;
	tc->mask = BIT_ULL(cc->shift) - 1;
	tc->frac = 0;
}
EXPORT_SYMBOL_GPL(timecounter_init);

/**
 * timecounter_read_delta - get nanoseconds since last call
 * @tc:         Pointer to time counter
 *
 * Returns: nanoseconds since last call, handles cycle counter overflow correctly
 * as long as it doesn't overflow more than once between calls.
 */
static u64 timecounter_read_delta(struct timecounter *tc)
{
	const struct cyclecounter *cc = tc->cc;
	u64 cycle_now, cycle_delta;

	cycle_now = cc->read(cc);
	cycle_delta = (cycle_now - tc->cycle_last) & cc->mask;

	/* Update last cycle count immediately */
	tc->cycle_last = cycle_now;

	return cyclecounter_cyc2ns(cc, cycle_delta, tc->mask, &tc->frac);
}

/**
 * timecounter_read - read the current time counter value
 * @tc:         Pointer to time counter
 *
 * Returns: current time in nanoseconds
 */
u64 timecounter_read(struct timecounter *tc)
{
	tc->nsec += timecounter_read_delta(tc);
	return tc->nsec;
}
EXPORT_SYMBOL_GPL(timecounter_read);

/**
 * cc_cyc2ns_backwards - convert cycles to nanoseconds for past timestamps
 * @cc:         Pointer to cycle counter
 * @cycles:     Cycles to convert
 * @mask:       Bitmask for cycle counter
 * @frac:       Fractional nanoseconds accumulator
 *
 * Returns: nanoseconds value for cycles in the past
 */
static u64 cc_cyc2ns_backwards(const struct cyclecounter *cc,
			       u64 cycles, u64 mask, u64 frac)
{
	return ((cycles * cc->mult) - frac) >> cc->shift;
}

/**
 * timecounter_cyc2time - convert a cycle timestamp to nanoseconds
 * @tc:         Pointer to time counter
 * @cycle_tstamp: Cycle timestamp to convert
 *
 * Returns: corresponding nanoseconds value for the cycle timestamp
 */
u64 timecounter_cyc2time(const struct timecounter *tc,
			 u64 cycle_tstamp)
{
	const struct cyclecounter *cc = tc->cc;
	u64 delta = (cycle_tstamp - tc->cycle_last) & cc->mask;
	u64 nsec = tc->nsec;

	/*
	 * Handle both cases where cycle_tstamp is before or after cycle_last
	 * by checking if delta is more than half the counter range
	 */
	if (delta > (cc->mask >> 1)) {
		delta = (tc->cycle_last - cycle_tstamp) & cc->mask;
		nsec -= cc_cyc2ns_backwards(cc, delta, tc->mask, tc->frac);
	} else {
		u64 frac = tc->frac;
		nsec += cyclecounter_cyc2ns(cc, delta, tc->mask, &frac);
	}

	return nsec;
}
EXPORT_SYMBOL_GPL(timecounter_cyc2time);
