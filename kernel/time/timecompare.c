/*
 * Copyright (C) 2009 Intel Corporation.
 * Author: Patrick Ohly <patrick.ohly@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/timecompare.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/kernel.h>

/*
 * fixed point arithmetic scale factor for skew
 *
 * Usually one would measure skew in ppb (parts per billion, 1e9), but
 * using a factor of 2 simplifies the math.
 */
#define TIMECOMPARE_SKEW_RESOLUTION (((s64)1)<<30)

ktime_t timecompare_transform(struct timecompare *sync,
			      u64 source_tstamp)
{
	u64 nsec;

	nsec = source_tstamp + sync->offset;
	nsec += (s64)(source_tstamp - sync->last_update) * sync->skew /
		TIMECOMPARE_SKEW_RESOLUTION;

	return ns_to_ktime(nsec);
}
EXPORT_SYMBOL_GPL(timecompare_transform);

int timecompare_offset(struct timecompare *sync,
		       s64 *offset,
		       u64 *source_tstamp)
{
	u64 start_source = 0, end_source = 0;
	struct {
		s64 offset;
		s64 duration_target;
	} buffer[10], sample, *samples;
	int counter = 0, i;
	int used;
	int index;
	int num_samples = sync->num_samples;

	if (num_samples > ARRAY_SIZE(buffer)) {
		samples = kmalloc(sizeof(*samples) * num_samples, GFP_ATOMIC);
		if (!samples) {
			samples = buffer;
			num_samples = ARRAY_SIZE(buffer);
		}
	} else {
		samples = buffer;
	}

	/* run until we have enough valid samples, but do not try forever */
	i = 0;
	counter = 0;
	while (1) {
		u64 ts;
		ktime_t start, end;

		start = sync->target();
		ts = timecounter_read(sync->source);
		end = sync->target();

		if (!i)
			start_source = ts;

		/* ignore negative durations */
		sample.duration_target = ktime_to_ns(ktime_sub(end, start));
		if (sample.duration_target >= 0) {
			/*
			 * assume symetric delay to and from source:
			 * average target time corresponds to measured
			 * source time
			 */
			sample.offset =
				(ktime_to_ns(end) + ktime_to_ns(start)) / 2 -
				ts;

			/* simple insertion sort based on duration */
			index = counter - 1;
			while (index >= 0) {
				if (samples[index].duration_target <
				    sample.duration_target)
					break;
				samples[index + 1] = samples[index];
				index--;
			}
			samples[index + 1] = sample;
			counter++;
		}

		i++;
		if (counter >= num_samples || i >= 100000) {
			end_source = ts;
			break;
		}
	}

	*source_tstamp = (end_source + start_source) / 2;

	/* remove outliers by only using 75% of the samples */
	used = counter * 3 / 4;
	if (!used)
		used = counter;
	if (used) {
		/* calculate average */
		s64 off = 0;
		for (index = 0; index < used; index++)
			off += samples[index].offset;
		*offset = div_s64(off, used);
	}

	if (samples && samples != buffer)
		kfree(samples);

	return used;
}
EXPORT_SYMBOL_GPL(timecompare_offset);

void __timecompare_update(struct timecompare *sync,
			  u64 source_tstamp)
{
	s64 offset;
	u64 average_time;

	if (!timecompare_offset(sync, &offset, &average_time))
		return;

	if (!sync->last_update) {
		sync->last_update = average_time;
		sync->offset = offset;
		sync->skew = 0;
	} else {
		s64 delta_nsec = average_time - sync->last_update;

		/* avoid division by negative or small deltas */
		if (delta_nsec >= 10000) {
			s64 delta_offset_nsec = offset - sync->offset;
			s64 skew; /* delta_offset_nsec *
				     TIMECOMPARE_SKEW_RESOLUTION /
				     delta_nsec */
			u64 divisor;

			/* div_s64() is limited to 32 bit divisor */
			skew = delta_offset_nsec * TIMECOMPARE_SKEW_RESOLUTION;
			divisor = delta_nsec;
			while (unlikely(divisor >= ((s64)1) << 32)) {
				/* divide both by 2; beware, right shift
				   of negative value has undefined
				   behavior and can only be used for
				   the positive divisor */
				skew = div_s64(skew, 2);
				divisor >>= 1;
			}
			skew = div_s64(skew, divisor);

			/*
			 * Calculate new overall skew as 4/16 the
			 * old value and 12/16 the new one. This is
			 * a rather arbitrary tradeoff between
			 * only using the latest measurement (0/16 and
			 * 16/16) and even more weight on past measurements.
			 */
#define TIMECOMPARE_NEW_SKEW_PER_16 12
			sync->skew =
				div_s64((16 - TIMECOMPARE_NEW_SKEW_PER_16) *
					sync->skew +
					TIMECOMPARE_NEW_SKEW_PER_16 * skew,
					16);
			sync->last_update = average_time;
			sync->offset = offset;
		}
	}
}
EXPORT_SYMBOL_GPL(__timecompare_update);
