/*
 * Utility code which helps transforming between two different time
 * bases, called "source" and "target" time in this code.
 *
 * Source time has to be provided via the timecounter API while target
 * time is accessed via a function callback whose prototype
 * intentionally matches ktime_get() and ktime_get_real(). These
 * interfaces where chosen like this so that the code serves its
 * initial purpose without additional glue code.
 *
 * This purpose is synchronizing a hardware clock in a NIC with system
 * time, in order to implement the Precision Time Protocol (PTP,
 * IEEE1588) with more accurate hardware assisted time stamping.  In
 * that context only synchronization against system time (=
 * ktime_get_real()) is currently needed. But this utility code might
 * become useful in other situations, which is why it was written as
 * general purpose utility code.
 *
 * The source timecounter is assumed to return monotonically
 * increasing time (but this code does its best to compensate if that
 * is not the case) whereas target time may jump.
 *
 * The target time corresponding to a source time is determined by
 * reading target time, reading source time, reading target time
 * again, then assuming that average target time corresponds to source
 * time. In other words, the assumption is that reading the source
 * time is slow and involves equal time for sending the request and
 * receiving the reply, whereas reading target time is assumed to be
 * fast.
 *
 * Copyright (C) 2009 Intel Corporation.
 * Author: Patrick Ohly <patrick.ohly@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. * See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _LINUX_TIMECOMPARE_H
#define _LINUX_TIMECOMPARE_H

#include <linux/clocksource.h>
#include <linux/ktime.h>

/**
 * struct timecompare - stores state and configuration for the two clocks
 *
 * Initialize to zero, then set source/target/num_samples.
 *
 * Transformation between source time and target time is done with:
 * target_time = source_time + offset +
 *               (source_time - last_update) * skew /
 *               TIMECOMPARE_SKEW_RESOLUTION
 *
 * @source:          used to get source time stamps via timecounter_read()
 * @target:          function returning target time (for example, ktime_get
 *                   for monotonic time, or ktime_get_real for wall clock)
 * @num_samples:     number of times that source time and target time are to
 *                   be compared when determining their offset
 * @offset:          (target time - source time) at the time of the last update
 * @skew:            average (target time - source time) / delta source time *
 *                   TIMECOMPARE_SKEW_RESOLUTION
 * @last_update:     last source time stamp when time offset was measured
 */
struct timecompare {
	struct timecounter *source;
	ktime_t (*target)(void);
	int num_samples;

	s64 offset;
	s64 skew;
	u64 last_update;
};

/**
 * timecompare_transform - transform source time stamp into target time base
 * @sync:            context for time sync
 * @source_tstamp:   the result of timecounter_read() or
 *                   timecounter_cyc2time()
 */
extern ktime_t timecompare_transform(struct timecompare *sync,
				     u64 source_tstamp);

/**
 * timecompare_offset - measure current (target time - source time) offset
 * @sync:            context for time sync
 * @offset:          average offset during sample period returned here
 * @source_tstamp:   average source time during sample period returned here
 *
 * Returns number of samples used. Might be zero (= no result) in the
 * unlikely case that target time was monotonically decreasing for all
 * samples (= broken).
 */
extern int timecompare_offset(struct timecompare *sync,
			      s64 *offset,
			      u64 *source_tstamp);

extern void __timecompare_update(struct timecompare *sync,
				 u64 source_tstamp);

/**
 * timecompare_update - update offset and skew by measuring current offset
 * @sync:            context for time sync
 * @source_tstamp:   the result of timecounter_read() or
 *                   timecounter_cyc2time(), pass zero to force update
 *
 * Updates are only done at most once per second.
 */
static inline void timecompare_update(struct timecompare *sync,
				      u64 source_tstamp)
{
	if (!source_tstamp ||
	    (s64)(source_tstamp - sync->last_update) >= NSEC_PER_SEC)
		__timecompare_update(sync, source_tstamp);
}

#endif /* _LINUX_TIMECOMPARE_H */
