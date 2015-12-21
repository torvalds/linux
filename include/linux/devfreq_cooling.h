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

#ifdef CONFIG_DEVFREQ_THERMAL

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
 */
struct devfreq_cooling_power {
	unsigned long (*get_static_power)(unsigned long voltage);
	unsigned long (*get_dynamic_power)(unsigned long freq,
					   unsigned long voltage);
	unsigned long dyn_power_coeff;
};

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
