/*
 *  linux/include/linux/clock_cooling.h
 *
 *  Copyright (C) 2014 Eduardo Valentin <edubezval@gmail.com>
 *
 *  Copyright (C) 2013	Texas Instruments Inc.
 *  Contact:  Eduardo Valentin <eduardo.valentin@ti.com>
 *
 *  Highly based on cpu_cooling.c.
 *  Copyright (C) 2012	Samsung Electronics Co., Ltd(http://www.samsung.com)
 *  Copyright (C) 2012  Amit Daniel <amit.kachhap@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 */

#ifndef __CPU_COOLING_H__
#define __CPU_COOLING_H__

#include <linux/of.h>
#include <linux/thermal.h>
#include <linux/cpumask.h>

#ifdef CONFIG_CLOCK_THERMAL
/**
 * clock_cooling_register - function to create clock cooling device.
 * @dev: struct device pointer to the device used as clock cooling device.
 * @clock_name: string containing the clock used as cooling mechanism.
 */
struct thermal_cooling_device *
clock_cooling_register(struct device *dev, const char *clock_name);

/**
 * clock_cooling_unregister - function to remove clock cooling device.
 * @cdev: thermal cooling device pointer.
 */
void clock_cooling_unregister(struct thermal_cooling_device *cdev);

unsigned long clock_cooling_get_level(struct thermal_cooling_device *cdev,
				      unsigned long freq);
#else /* !CONFIG_CLOCK_THERMAL */
static inline struct thermal_cooling_device *
clock_cooling_register(struct device *dev, const char *clock_name)
{
	return NULL;
}
static inline
void clock_cooling_unregister(struct thermal_cooling_device *cdev)
{
}
static inline
unsigned long clock_cooling_get_level(struct thermal_cooling_device *cdev,
				      unsigned long freq)
{
	return THERMAL_CSTATE_INVALID;
}
#endif	/* CONFIG_CLOCK_THERMAL */

#endif /* __CPU_COOLING_H__ */
