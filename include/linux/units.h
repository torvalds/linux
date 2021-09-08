/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNITS_H
#define _LINUX_UNITS_H

#include <linux/math.h>

#define HZ_PER_KHZ		1000UL
#define KHZ_PER_MHZ		1000UL
#define HZ_PER_MHZ		1000000UL

#define MILLIWATT_PER_WATT	1000UL
#define MICROWATT_PER_MILLIWATT	1000UL
#define MICROWATT_PER_WATT	1000000UL

#define ABSOLUTE_ZERO_MILLICELSIUS -273150

static inline long milli_kelvin_to_millicelsius(long t)
{
	return t + ABSOLUTE_ZERO_MILLICELSIUS;
}

static inline long millicelsius_to_milli_kelvin(long t)
{
	return t - ABSOLUTE_ZERO_MILLICELSIUS;
}

#define MILLIDEGREE_PER_DEGREE 1000
#define MILLIDEGREE_PER_DECIDEGREE 100

static inline long kelvin_to_millicelsius(long t)
{
	return milli_kelvin_to_millicelsius(t * MILLIDEGREE_PER_DEGREE);
}

static inline long millicelsius_to_kelvin(long t)
{
	t = millicelsius_to_milli_kelvin(t);

	return DIV_ROUND_CLOSEST(t, MILLIDEGREE_PER_DEGREE);
}

static inline long deci_kelvin_to_celsius(long t)
{
	t = milli_kelvin_to_millicelsius(t * MILLIDEGREE_PER_DECIDEGREE);

	return DIV_ROUND_CLOSEST(t, MILLIDEGREE_PER_DEGREE);
}

static inline long celsius_to_deci_kelvin(long t)
{
	t = millicelsius_to_milli_kelvin(t * MILLIDEGREE_PER_DEGREE);

	return DIV_ROUND_CLOSEST(t, MILLIDEGREE_PER_DECIDEGREE);
}

/**
 * deci_kelvin_to_millicelsius_with_offset - convert Kelvin to Celsius
 * @t: temperature value in decidegrees Kelvin
 * @offset: difference between Kelvin and Celsius in millidegrees
 *
 * Return: temperature value in millidegrees Celsius
 */
static inline long deci_kelvin_to_millicelsius_with_offset(long t, long offset)
{
	return t * MILLIDEGREE_PER_DECIDEGREE - offset;
}

static inline long deci_kelvin_to_millicelsius(long t)
{
	return milli_kelvin_to_millicelsius(t * MILLIDEGREE_PER_DECIDEGREE);
}

static inline long millicelsius_to_deci_kelvin(long t)
{
	t = millicelsius_to_milli_kelvin(t);

	return DIV_ROUND_CLOSEST(t, MILLIDEGREE_PER_DECIDEGREE);
}

static inline long kelvin_to_celsius(long t)
{
	return t + DIV_ROUND_CLOSEST(ABSOLUTE_ZERO_MILLICELSIUS,
				     MILLIDEGREE_PER_DEGREE);
}

static inline long celsius_to_kelvin(long t)
{
	return t - DIV_ROUND_CLOSEST(ABSOLUTE_ZERO_MILLICELSIUS,
				     MILLIDEGREE_PER_DEGREE);
}

#endif /* _LINUX_UNITS_H */
