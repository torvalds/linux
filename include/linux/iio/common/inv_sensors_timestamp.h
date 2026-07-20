/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#ifndef INV_SENSORS_TIMESTAMP_H_
#define INV_SENSORS_TIMESTAMP_H_

/**
 * struct inv_sensors_timestamp_chip - chip internal properties
 * @clock_period:	internal clock period in ns
 * @jitter:		acceptable jitter in per-mille
 * @init_period:	chip initial period at reset in ns
 */
struct inv_sensors_timestamp_chip {
	u32 clock_period;
	u32 jitter;
	u32 init_period;
};

/**
 * struct inv_sensors_timestamp_interval - timestamps interval
 * @lo:	interval lower bound
 * @up:	interval upper bound
 */
struct inv_sensors_timestamp_interval {
	s64 lo;
	s64 up;
};

/**
 * struct inv_sensors_timestamp_acc - accumulator for computing an estimation
 * @val:	current estimation of the value, the mean of all values
 * @idx:	current index of the next free place in values table
 * @values:	table of all measured values, use for computing the mean
 */
struct inv_sensors_timestamp_acc {
	u32 val;
	size_t idx;
	u32 values[32];
};

/**
 * struct inv_sensors_timestamp - timestamp management states
 * @chip:		chip internal characteristics
 * @min_period:		minimal acceptable clock period
 * @max_period:		maximal acceptable clock period
 * @it:			interrupts interval timestamps
 * @delta:		interval timestamps between several interrupts
 * @delta_counter:	number of data samples in the delta interval
 * @timestamp:		store last timestamp for computing next data timestamp
 * @mult:		current internal period multiplier
 * @new_mult:		new set internal period multiplier (not yet effective)
 * @period:		measured current period of the sensor
 * @chip_period:	accumulator for computing internal chip period
 */
struct inv_sensors_timestamp {
	struct inv_sensors_timestamp_chip chip;
	u32 min_period;
	u32 max_period;
	struct inv_sensors_timestamp_interval it;
	struct inv_sensors_timestamp_interval delta;
	u32 delta_counter;
	s64 timestamp;
	u32 mult;
	u32 new_mult;
	u32 period;
	struct inv_sensors_timestamp_acc chip_period;
};

void inv_sensors_timestamp_init(struct inv_sensors_timestamp *ts,
				const struct inv_sensors_timestamp_chip *chip);

int inv_sensors_timestamp_update_odr(struct inv_sensors_timestamp *ts,
				     u32 period, bool fifo);

void inv_sensors_timestamp_interrupt(struct inv_sensors_timestamp *ts,
				     size_t sample_nb, s64 timestamp);

static inline s64 inv_sensors_timestamp_pop(struct inv_sensors_timestamp *ts)
{
	ts->timestamp += ts->period;
	return ts->timestamp;
}

void inv_sensors_timestamp_apply_odr(struct inv_sensors_timestamp *ts,
				     u32 fifo_period, size_t fifo_nb,
				     unsigned int fifo_no);

static inline void inv_sensors_timestamp_reset(struct inv_sensors_timestamp *ts)
{
	const struct inv_sensors_timestamp_interval interval_init = {0LL, 0LL};

	ts->it = interval_init;
	ts->delta = interval_init;
	ts->delta_counter = 0;
	ts->timestamp = 0;
}

#endif
