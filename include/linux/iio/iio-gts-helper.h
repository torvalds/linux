/* SPDX-License-Identifier: GPL-2.0-only */
/* gain-time-scale conversion helpers for IIO light sensors
 *
 * Copyright (c) 2023 Matti Vaittinen <mazziesaccount@gmail.com>
 */

#ifndef __IIO_GTS_HELPER__
#define __IIO_GTS_HELPER__

#include <linux/types.h>

struct device;

/**
 * struct iio_gain_sel_pair - gain - selector values
 *
 * In many cases devices like light sensors allow setting signal amplification
 * (gain) using a register interface. This structure describes amplification
 * and corresponding selector (register value)
 *
 * @gain:	Gain (multiplication) value. Gain must be positive, negative
 *		values are reserved for error handling.
 * @sel:	Selector (usually register value) used to indicate this gain.
 *		NOTE: Only selectors >= 0 supported.
 */
struct iio_gain_sel_pair {
	int gain;
	int sel;
};

/**
 * struct iio_itime_sel_mul - integration time description
 *
 * In many cases devices like light sensors allow setting the duration of
 * collecting data. Typically this duration has also an impact to the magnitude
 * of measured values (gain). This structure describes the relation of
 * integration time and amplification as well as corresponding selector
 * (register value).
 *
 * An example could be a sensor allowing 50, 100, 200 and 400 mS times. The
 * respective multiplication values could be 50 mS => 1, 100 mS => 2,
 * 200 mS => 4 and 400 mS => 8 assuming the impact of integration time would be
 * linear in a way that when collecting data for 50 mS caused value X, doubling
 * the data collection time caused value 2X etc.
 *
 * @time_us:	Integration time in microseconds. Time values must be positive,
 *		negative values are reserved for error handling.
 * @sel:	Selector (usually register value) used to indicate this time
 *		NOTE: Only selectors >= 0 supported.
 * @mul:	Multiplication to the values caused by this time.
 *		NOTE: Only multipliers > 0 supported.
 */
struct iio_itime_sel_mul {
	int time_us;
	int sel;
	int mul;
};

struct iio_gts {
	u64 max_scale;
	const struct iio_gain_sel_pair *hwgain_table;
	int num_hwgain;
	const struct iio_itime_sel_mul *itime_table;
	int num_itime;
	int **per_time_avail_scale_tables;
	int *avail_all_scales_table;
	int num_avail_all_scales;
	int *avail_time_tables;
	int num_avail_time_tables;
};

#define GAIN_SCALE_GAIN(_gain, _sel)			\
{							\
	.gain = (_gain),				\
	.sel = (_sel),					\
}

#define GAIN_SCALE_ITIME_US(_itime, _sel, _mul)		\
{							\
	.time_us = (_itime),				\
	.sel = (_sel),					\
	.mul = (_mul),					\
}

static inline const struct iio_itime_sel_mul *
iio_gts_find_itime_by_time(struct iio_gts *gts, int time)
{
	int i;

	if (!gts->num_itime)
		return NULL;

	for (i = 0; i < gts->num_itime; i++)
		if (gts->itime_table[i].time_us == time)
			return &gts->itime_table[i];

	return NULL;
}

static inline const struct iio_itime_sel_mul *
iio_gts_find_itime_by_sel(struct iio_gts *gts, int sel)
{
	int i;

	for (i = 0; i < gts->num_itime; i++)
		if (gts->itime_table[i].sel == sel)
			return &gts->itime_table[i];

	return NULL;
}

int devm_iio_init_iio_gts(struct device *dev, int max_scale_int, int max_scale_nano,
			  const struct iio_gain_sel_pair *gain_tbl, int num_gain,
			  const struct iio_itime_sel_mul *tim_tbl, int num_times,
			  struct iio_gts *gts);
/**
 * iio_gts_find_int_time_by_sel - find integration time matching a selector
 * @gts:	Gain time scale descriptor
 * @sel:	selector for which matching integration time is searched for
 *
 * Return:	integration time matching given selector or -EINVAL if
 *		integration time was not found.
 */
static inline int iio_gts_find_int_time_by_sel(struct iio_gts *gts, int sel)
{
	const struct iio_itime_sel_mul *itime;

	itime = iio_gts_find_itime_by_sel(gts, sel);
	if (!itime)
		return -EINVAL;

	return itime->time_us;
}

/**
 * iio_gts_find_sel_by_int_time - find selector matching integration time
 * @gts:	Gain time scale descriptor
 * @time:	Integration time for which matching selector is searched for
 *
 * Return:	a selector matching given integration time or -EINVAL if
 *		selector was not found.
 */
static inline int iio_gts_find_sel_by_int_time(struct iio_gts *gts, int time)
{
	const struct iio_itime_sel_mul *itime;

	itime = iio_gts_find_itime_by_time(gts, time);
	if (!itime)
		return -EINVAL;

	return itime->sel;
}

/**
 * iio_gts_valid_time - check if given integration time is valid
 * @gts:	Gain time scale descriptor
 * @time_us:	Integration time to check
 *
 * Return:	True if given time is supported by device. False if not.
 */
static inline bool iio_gts_valid_time(struct iio_gts *gts, int time_us)
{
	return iio_gts_find_itime_by_time(gts, time_us) != NULL;
}

int iio_gts_find_sel_by_gain(struct iio_gts *gts, int gain);

/**
 * iio_gts_valid_gain - check if given HW-gain is valid
 * @gts:	Gain time scale descriptor
 * @gain:	HW-gain to check
 *
 * Return:	True if given time is supported by device. False if not.
 */
static inline bool iio_gts_valid_gain(struct iio_gts *gts, int gain)
{
	return iio_gts_find_sel_by_gain(gts, gain) >= 0;
}

int iio_find_closest_gain_low(struct iio_gts *gts, int gain, bool *in_range);
int iio_gts_find_gain_by_sel(struct iio_gts *gts, int sel);
int iio_gts_get_min_gain(struct iio_gts *gts);
int iio_gts_find_int_time_by_sel(struct iio_gts *gts, int sel);
int iio_gts_find_sel_by_int_time(struct iio_gts *gts, int time);

int iio_gts_total_gain_to_scale(struct iio_gts *gts, int total_gain,
				int *scale_int, int *scale_nano);
int iio_gts_find_gain_sel_for_scale_using_time(struct iio_gts *gts, int time_sel,
					       int scale_int, int scale_nano,
					       int *gain_sel);
int iio_gts_find_gain_time_sel_for_scale(struct iio_gts *gts, int scale_int,
					 int scale_nano, int *gain_sel,
					 int *time_sel);
int iio_gts_get_scale(struct iio_gts *gts, int gain, int time, int *scale_int,
		      int *scale_nano);
int iio_gts_find_new_gain_sel_by_old_gain_time(struct iio_gts *gts,
					       int old_gain, int old_time_sel,
					       int new_time_sel, int *new_gain);
int iio_gts_find_new_gain_by_old_gain_time(struct iio_gts *gts, int old_gain,
					   int old_time, int new_time,
					   int *new_gain);
int iio_gts_find_new_gain_by_gain_time_min(struct iio_gts *gts, int old_gain,
					   int old_time, int new_time,
					   int *new_gain, bool *in_range);
int iio_gts_avail_times(struct iio_gts *gts,  const int **vals, int *type,
			int *length);
int iio_gts_all_avail_scales(struct iio_gts *gts, const int **vals, int *type,
			     int *length);
int iio_gts_avail_scales_for_time(struct iio_gts *gts, int time,
				  const int **vals, int *type, int *length);

#endif
