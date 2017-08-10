/*
 * devfreq_cooling: Thermal cooling device implementation for devices using
 *                  devfreq
 *
 * Copyright (C) 2014-2015 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DEVFREQ_COOLING_H__
#define __DEVFREQ_COOLING_H__

#include <linux/devfreq.h>
#include <linux/thermal.h>


/**
 * struct devfreq_cooling_power - Devfreq cooling power ops
 * @get_static_power:	Take voltage, in mV, and return the static power
 *			in mW.  If NULL, the static power is assumed
 *			to be 0.
 * @get_dynamic_power:	Take voltage, in mV, and frequency, in HZ, and
 *			return the dynamic power draw in mW.  If NULL,
 *			a simple power model is used.
 * @dyn_power_coeff:	Coefficient for the simple dynamic power model in
 *			mW/(MHz mV mV).
 *			If get_dynamic_power() is NULL, then the
 *			dynamic power is calculated as
 *			@dyn_power_coeff * frequency * voltage^2
 * @get_real_power:	When this is set, the framework uses it to ask the
 *			device driver for the actual power.
 *			Some devices have more sophisticated methods
 *			(like power counters) to approximate the actual power
 *			that they use.
 *			This function provides more accurate data to the
 *			thermal governor. When the driver does not provide
 *			such function, framework just uses pre-calculated
 *			table and scale the power by 'utilization'
 *			(based on 'busy_time' and 'total_time' taken from
 *			devfreq 'last_status').
 *			The value returned by this function must be lower
 *			or equal than the maximum power value
 *			for the current	state
 *			(which can be found in power_table[state]).
 *			When this interface is used, the power_table holds
 *			max total (static + dynamic) power value for each OPP.
 */
struct devfreq_cooling_power {
	unsigned long (*get_static_power)(struct devfreq *devfreq,
					  unsigned long voltage);
	unsigned long (*get_dynamic_power)(struct devfreq *devfreq,
					   unsigned long freq,
					   unsigned long voltage);
	int (*get_real_power)(struct devfreq *df, u32 *power,
			      unsigned long freq, unsigned long voltage);
	unsigned long dyn_power_coeff;
};

#ifdef CONFIG_DEVFREQ_THERMAL

struct thermal_cooling_device *
of_devfreq_cooling_register_power(struct device_node *np, struct devfreq *df,
				  struct devfreq_cooling_power *dfc_power);
struct thermal_cooling_device *
of_devfreq_cooling_register(struct device_node *np, struct devfreq *df);
struct thermal_cooling_device *devfreq_cooling_register(struct devfreq *df);
void devfreq_cooling_unregister(struct thermal_cooling_device *dfc);

#else /* !CONFIG_DEVFREQ_THERMAL */

struct thermal_cooling_device *
of_devfreq_cooling_register_power(struct device_node *np, struct devfreq *df,
				  struct devfreq_cooling_power *dfc_power)
{
	return ERR_PTR(-EINVAL);
}

static inline struct thermal_cooling_device *
of_devfreq_cooling_register(struct device_node *np, struct devfreq *df)
{
	return ERR_PTR(-EINVAL);
}

static inline struct thermal_cooling_device *
devfreq_cooling_register(struct devfreq *df)
{
	return ERR_PTR(-EINVAL);
}

static inline void
devfreq_cooling_unregister(struct thermal_cooling_device *dfc)
{
}

#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* __DEVFREQ_COOLING_H__ */
